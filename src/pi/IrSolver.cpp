#include "pi/IrSolver.h"

#include <algorithm>
#include <cstddef>

#include <Eigen/SparseCholesky>
#include <Eigen/SparseCore>

namespace pdnkit::pi {

Solution IrSolver::solve(const IrMesh& mesh, const SolveConfig& cfg) {
    Solution sol;

    const std::size_t N = mesh.nodes.size();
    if (N == 0) {
        sol.error = "empty mesh";
        return sol;
    }
    if (mesh.source_node_ids.empty()) {
        sol.error = "no source nodes";
        return sol;
    }
    if (mesh.sink_node_ids.empty()) {
        sol.error = "no sink nodes (the matrix would be singular without a "
                    "pinned reference)";
        return sol;
    }

    // 1) Conductance matrix G (N×N, SPSD). Each resistor between (a, b) with
    //    conductance g contributes:
    //        G[a][a] += g, G[b][b] += g, G[a][b] -= g, G[b][a] -= g
    //    setFromTriplets sums duplicate (i, j) entries, so we just emit four
    //    triplets per resistor and let Eigen do the accumulation.
    std::vector<Eigen::Triplet<double>> triplets;
    triplets.reserve(4 * mesh.resistors.size() + mesh.sink_node_ids.size());

    for (const auto& r : mesh.resistors) {
        if (r.from_node < 0 || r.from_node >= static_cast<int>(N)) continue;
        if (r.to_node   < 0 || r.to_node   >= static_cast<int>(N)) continue;
        triplets.emplace_back(r.from_node, r.from_node,  r.conductance);
        triplets.emplace_back(r.to_node,   r.to_node,    r.conductance);
        triplets.emplace_back(r.from_node, r.to_node,   -r.conductance);
        triplets.emplace_back(r.to_node,   r.from_node, -r.conductance);
    }

    // 2) Pin sinks to ~0 V via a large diagonal entry. With G typical entries
    //    around a few thousand S, 1e15 dominates by ~11 orders of magnitude
    //    while staying well clear of double-precision overflow during factor.
    constexpr double kPinStiffness = 1.0e15;
    for (int s : mesh.sink_node_ids) {
        if (s >= 0 && s < static_cast<int>(N)) {
            triplets.emplace_back(s, s, kPinStiffness);
        }
    }

    Eigen::SparseMatrix<double> G(static_cast<Eigen::Index>(N),
                                   static_cast<Eigen::Index>(N));
    G.setFromTriplets(triplets.begin(), triplets.end());
    G.makeCompressed();

    // 3) RHS: total_current split evenly across source nodes; pinned sinks
    //    have RHS = 0 (already initialized).
    Eigen::VectorXd b = Eigen::VectorXd::Zero(static_cast<Eigen::Index>(N));
    const double per_source = cfg.total_current /
                              static_cast<double>(mesh.source_node_ids.size());
    for (int s : mesh.source_node_ids) {
        if (s >= 0 && s < static_cast<int>(N)) b[s] += per_source;
    }

    // 4) Solve with sparse Cholesky.
    Eigen::SimplicialLLT<Eigen::SparseMatrix<double>> solver;
    solver.compute(G);
    if (solver.info() != Eigen::Success) {
        sol.error = "Cholesky factorization failed (matrix may be singular)";
        return sol;
    }

    Eigen::VectorXd v = solver.solve(b);
    if (solver.info() != Eigen::Success) {
        sol.error = "back-substitution failed";
        return sol;
    }

    sol.voltages.assign(v.data(), v.data() + N);
    sol.max_v = v.maxCoeff();
    sol.min_v = v.minCoeff();
    sol.ok = true;
    return sol;
}

}  // namespace pdnkit::pi
