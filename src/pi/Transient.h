// Time-domain step-response transient solver.
//
// Backward Euler on the implicit system  (G + C/dt) * v_{k+1} = (C/dt) * v_k + i_k
// where G is the existing conductance matrix from IrMesh, C is a diagonal
// per-node capacitance (uniform across all nodes in v0), and i_k is the
// current-injection vector at timestep k (zero before t_zero_step, then
// the static IrMesh source/sink pattern).
//
// Sink nodes are pinned to ~0 V via the same large-diagonal trick used in
// the static IrSolver so the matrix stays SPD and we can keep sparse
// Cholesky. The (G + C/dt) matrix is factored ONCE and re-used for every
// timestep; each step is just one back-substitution.
//
// This is intentionally a v0:
//   * Uniform capacitance per node (real plane has distributed C from
//     plane-pair epsilon * area / d; decap C adds at decap locations).
//   * Step-only excitation (per-node arbitrary I(t) lands in a follow-up).
//   * No UI yet -- callable from tests / CLI.

#pragma once

#include <string>
#include <vector>

#include "pi/IrMesher.h"

namespace pdnkit::pi {

struct TransientConfig {
    // Capacitance per mesh node, in Farads. v0 ships with a uniform value
    // -- a follow-up will derive this from the plane pair (eps_r, d, cell_area)
    // and the decap table.
    double per_node_capacitance = 1.0e-12;
    double dt = 1.0e-9;          // timestep (s); pick smaller for fast transients
    int    n_steps = 1000;       // length of the simulation
    double t_zero_step = 0.0;    // time at which the step turns on
    double step_current = 1.0;   // Amperes injected at t >= t_zero_step

    // Index of the mesh node whose voltage gets recorded as obs_v(t).
    // Defaults to -1 meaning "use the first source node" (the usual case).
    int obs_node_id = -1;
};

struct TransientResult {
    std::vector<double> times;  // seconds
    std::vector<double> max_v;  // max absolute voltage across the whole mesh
    std::vector<double> obs_v;  // voltage at the observation node
    bool ok = false;
    std::string error;
};

TransientResult solve_step_transient(const IrMesh& mesh,
                                      const TransientConfig& cfg);

}  // namespace pdnkit::pi
