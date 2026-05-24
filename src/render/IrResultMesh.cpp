#include "render/IrResultMesh.h"

namespace pdnkit::render {

IrResultMesh build_ir_result_mesh(const pi::IrMesh& mesh,
                                   const pi::Solution& solution,
                                   double cell_size) {
    IrResultMesh out;
    if (!solution.ok || mesh.nodes.empty() ||
        solution.voltages.size() != mesh.nodes.size()) {
        return out;
    }

    out.v_min = solution.min_v;
    out.v_max = solution.max_v;
    const double span = (out.v_max - out.v_min);
    const double inv_span = (span > 0.0) ? (1.0 / span) : 0.0;

    out.vertices.reserve(mesh.nodes.size() * 12);  // 4 verts × 3 floats
    out.indices.reserve(mesh.nodes.size() * 6);    // 2 tris × 3 indices

    const float hs = static_cast<float>(0.5 * cell_size);

    for (std::size_t i = 0; i < mesh.nodes.size(); ++i) {
        const auto& n = mesh.nodes[i];
        const float cx = static_cast<float>(n.x);
        const float cy = static_cast<float>(n.y);
        const double v = solution.voltages[i];
        const float t = static_cast<float>((v - out.v_min) * inv_span);

        const auto base = static_cast<std::uint32_t>(out.vertex_count());

        // 4 corners (CCW): TL, TR, BR, BL.
        out.vertices.insert(out.vertices.end(),
                            {cx - hs, cy - hs, t,
                             cx + hs, cy - hs, t,
                             cx + hs, cy + hs, t,
                             cx - hs, cy + hs, t});

        out.indices.insert(out.indices.end(),
                           {base + 0, base + 1, base + 2,
                            base + 0, base + 2, base + 3});
    }

    return out;
}

}  // namespace pdnkit::render
