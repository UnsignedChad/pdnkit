// GL-ready quad mesh for an IR-drop solution.
//
// One small square per IrMesh node, sized to the mesher's cell_size and
// colored by per-vertex voltage. Per-vertex voltage is normalized to [0,1]
// (relative to the solution's min/max) so the fragment shader can index a
// fixed colormap without needing extra uniforms.

#pragma once

#include <cstdint>
#include <vector>

#include "pi/IrMesher.h"
#include "pi/IrSolver.h"

namespace pdnkit::render {

struct IrResultMesh {
    // Interleaved per-vertex: x, y, t  (t = voltage normalized to [0, 1]).
    std::vector<float> vertices;
    std::vector<std::uint32_t> indices;

    // Raw voltage range used for the normalization, for legend / status bar.
    double v_min = 0.0;
    double v_max = 0.0;

    // Index-range per copper layer so the renderer can skip hidden layers.
    // Layers appear in first-seen order from the source IrMesh.
    struct LayerRange {
        int ordinal = 0;
        int index_start = 0;
        int index_count = 0;
    };
    std::vector<LayerRange> layer_ranges;

    std::size_t vertex_count() const noexcept { return vertices.size() / 3; }
};

// Build a colored quad per node. cell_size in meters (matches the mesher's
// MeshConfig::cell_size). Empty result if the solution is not ok.
IrResultMesh build_ir_result_mesh(const pi::IrMesh& mesh,
                                   const pi::Solution& solution,
                                   double cell_size);

}  // namespace pdnkit::render
