#include "pi/Transient.h"

#include <algorithm>
#include <cmath>

#include <Eigen/SparseCholesky>
#include <Eigen/SparseCore>

#ifdef PDNKIT_HAVE_CHOLMOD
#include <Eigen/CholmodSupport>
#endif

namespace pdnkit::pi {

TransientResult solve_step_transient(const IrMesh& mesh,
                                      const TransientConfig& cfg) {
    TransientResult out;
    const int N = static_cast<int>(mesh.nodes.size());

    if (N == 0) { out.error = "empty mesh"; return out; }
    if (cfg.n_steps < 1 || cfg.dt <= 0.0 || cfg.per_node_capacitance <= 0.0) {
        out.error = "invalid TransientConfig (need n_steps>=1, dt>0, C>0)";
        return out;
    }
    if (mesh.source_node_ids.empty()) {
        out.error = "no source nodes -- nothing to step";
        return out;
    }
    if (mesh.sink_node_ids.empty()) {
        out.error = "no sink nodes -- matrix would be singular";
        return out;
    }

    // Build G (sparse conductance) + C/dt diagonal + sink pin diagonal.
    constexpr double kPinStiffness = 1.0e15;
    const double c_over_dt = cfg.per_node_capacitance / cfg.dt;

    std::vector<Eigen::Triplet<double>> triplets;
    triplets.reserve(4 * mesh.resistors.size() + 2 * N);

    for (const auto& r : mesh.resistors) {
        if (r.from_node < 0 || r.from_node >= N) continue;
        if (r.to_node   < 0 || r.to_node   >= N) continue;
        triplets.emplace_back(r.from_node, r.from_node,  r.conductance);
        triplets.emplace_back(r.to_node,   r.to_node,    r.conductance);
        triplets.emplace_back(r.from_node, r.to_node,   -r.conductance);
        triplets.emplace_back(r.to_node,   r.from_node, -r.conductance);
    }
    for (int i = 0; i < N; ++i) triplets.emplace_back(i, i, c_over_dt);
    for (int s : mesh.sink_node_ids) {
        if (s >= 0 && s < N) triplets.emplace_back(s, s, kPinStiffness);
    }

    Eigen::SparseMatrix<double> A(N, N);
    A.setFromTriplets(triplets.begin(), triplets.end());
    A.makeCompressed();

#ifdef PDNKIT_HAVE_CHOLMOD
    Eigen::CholmodSupernodalLLT<Eigen::SparseMatrix<double>> solver;
#else
    Eigen::SimplicialLLT<Eigen::SparseMatrix<double>> solver;
#endif
    solver.compute(A);
    if (solver.info() != Eigen::Success) {
        out.error = "Cholesky factorization failed (matrix may be singular)";
        return out;
    }

    // Per-step source-current vector (constant after t_zero_step).
    Eigen::VectorXd i_step = Eigen::VectorXd::Zero(N);
    const double per_source = cfg.step_current /
        static_cast<double>(mesh.source_node_ids.size());
    for (int s : mesh.source_node_ids) {
        if (s >= 0 && s < N) i_step[s] += per_source;
    }

    Eigen::VectorXd v = Eigen::VectorXd::Zero(N);

    const int obs = (cfg.obs_node_id >= 0 && cfg.obs_node_id < N)
        ? cfg.obs_node_id : mesh.source_node_ids.front();

    out.times.reserve(cfg.n_steps);
    out.max_v.reserve(cfg.n_steps);
    out.obs_v.reserve(cfg.n_steps);

    for (int k = 0; k < cfg.n_steps; ++k) {
        const double t = (k + 1) * cfg.dt;
        // RHS = C/dt * v_prev + (step on if t >= t_zero_step).
        Eigen::VectorXd rhs = c_over_dt * v;
        if (t >= cfg.t_zero_step) rhs += i_step;

        v = solver.solve(rhs);
        if (solver.info() != Eigen::Success) {
            out.error = "transient back-substitution failed";
            return out;
        }
        out.times.push_back(t);
        out.obs_v.push_back(v[obs]);
        out.max_v.push_back(v.cwiseAbs().maxCoeff());
    }

    out.ok = true;
    return out;
}

}  // namespace pdnkit::pi
