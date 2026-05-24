#include <unordered_map>

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

    // Group node indices by layer ordinal, preserving first-seen layer order.
    std::vector<int> layer_order;
    std::unordered_map<int, std::vector<std::size_t>> by_layer;
    by_layer.reserve(8);
    for (std::size_t i = 0; i < mesh.nodes.size(); ++i) {
        const int ord = mesh.nodes[i].layer_ordinal;
        auto [it, inserted] = by_layer.try_emplace(ord);
        if (inserted) layer_order.push_back(ord);
        it->second.push_back(i);
    }

    out.vertices.reserve(mesh.nodes.size() * 12);  // 4 verts × 3 floats
    out.indices.reserve(mesh.nodes.size() * 6);    // 2 tris × 3 indices

    const float hs = static_cast<float>(0.5 * cell_size);

    for (int ord : layer_order) {
        IrResultMesh::LayerRange range;
        range.ordinal = ord;
        range.index_start = static_cast<int>(out.indices.size());

        for (std::size_t i : by_layer[ord]) {
            const auto& n = mesh.nodes[i];
            const float cx = static_cast<float>(n.x);
            const float cy = static_cast<float>(n.y);
            const double v = solution.voltages[i];
            const float t = static_cast<float>((v - out.v_min) * inv_span);
            const auto base = static_cast<std::uint32_t>(out.vertex_count());

            out.vertices.insert(out.vertices.end(),
                                {cx - hs, cy - hs, t,
                                 cx + hs, cy - hs, t,
                                 cx + hs, cy + hs, t,
                                 cx - hs, cy + hs, t});

            out.indices.insert(out.indices.end(),
                               {base + 0, base + 1, base + 2,
                                base + 0, base + 2, base + 3});
        }

        range.index_count = static_cast<int>(out.indices.size()) - range.index_start;
        out.layer_ranges.push_back(range);
    }

    // Markers: prefer the per-node currents (multi-pad case); fall back to
    // source/sink lists with synthetic +/-1 (the v0 split-current case).
    if (!mesh.node_currents.empty()) {
        for (const auto& [nid, cur] : mesh.node_currents) {
            if (nid < 0 || nid >= static_cast<int>(mesh.nodes.size())) continue;
            const auto& n = mesh.nodes[nid];
            out.markers.push_back({n.x, n.y, cur});
        }
    } else {
        for (int nid : mesh.source_node_ids) {
            if (nid < 0 || nid >= static_cast<int>(mesh.nodes.size())) continue;
            const auto& n = mesh.nodes[nid];
            out.markers.push_back({n.x, n.y, +1.0});
        }
        for (int nid : mesh.sink_node_ids) {
            if (nid < 0 || nid >= static_cast<int>(mesh.nodes.size())) continue;
            const auto& n = mesh.nodes[nid];
            out.markers.push_back({n.x, n.y, -1.0});
        }
    }

    return out;
}

}  // namespace pdnkit::render
