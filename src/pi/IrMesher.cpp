#include "pi/IrMesher.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <vector>

namespace pdnkit::pi {

namespace {

// Ray-casting point-in-polygon (duplicated from HitTest to avoid pulling the
// hittest module — they are independent concerns and this is 10 lines).
bool point_in_ring(const std::vector<model::Point2>& ring, double px, double py) {
    bool inside = false;
    const std::size_t n = ring.size();
    if (n < 3) return false;
    for (std::size_t i = 0, j = n - 1; i < n; j = i++) {
        const auto& pi_ = ring[i];
        const auto& pj_ = ring[j];
        if (((pi_.y > py) != (pj_.y > py)) &&
            (px < (pj_.x - pi_.x) * (py - pi_.y) / (pj_.y - pi_.y) + pi_.x)) {
            inside = !inside;
        }
    }
    return inside;
}

bool point_in_polygon(const model::Polygon& poly, double px, double py) {
    if (!point_in_ring(poly.outline, px, py)) return false;
    for (const auto& h : poly.holes) {
        if (point_in_ring(h, px, py)) return false;
    }
    return true;
}

// Returns true if (px, py) lies in any filled polygon of any matching zone.
bool point_in_target_copper(const model::Board& board, int net, int layer,
                            double px, double py) {
    for (const auto& z : board.zones) {
        if (z.net_id != net) continue;
        if (z.layer_ordinal != layer) continue;
        for (const auto& fp : z.filled) {
            if (point_in_polygon(fp, px, py)) return true;
        }
    }
    return false;
}

// World bbox of all filled polygons on the target (net, layer). Returns false
// if there is no matching geometry.
bool target_bbox(const model::Board& board, int net, int layer,
                 double& lo_x, double& lo_y, double& hi_x, double& hi_y) {
    bool have_any = false;
    for (const auto& z : board.zones) {
        if (z.net_id != net || z.layer_ordinal != layer) continue;
        for (const auto& fp : z.filled) {
            for (const auto& p : fp.outline) {
                if (!have_any) {
                    lo_x = hi_x = p.x;
                    lo_y = hi_y = p.y;
                    have_any = true;
                } else {
                    if (p.x < lo_x) lo_x = p.x;
                    if (p.x > hi_x) hi_x = p.x;
                    if (p.y < lo_y) lo_y = p.y;
                    if (p.y > hi_y) hi_y = p.y;
                }
            }
        }
    }
    return have_any;
}

}  // namespace

IrMesh IrMesher::build(const model::Board& board, const MeshConfig& cfg) {
    IrMesh mesh;
    if (cfg.cell_size <= 0.0 || cfg.copper_thickness <= 0.0 ||
        cfg.copper_rho <= 0.0) {
        return mesh;
    }

    double lo_x = 0, lo_y = 0, hi_x = 0, hi_y = 0;
    if (!target_bbox(board, cfg.net_id, cfg.layer_ordinal, lo_x, lo_y, hi_x,
                     hi_y)) {
        return mesh;
    }

    // bbox is the polygon bbox; cell centers fall strictly inside it at
    // lo + (i + 0.5) * cell_size. Floor on the count: a fractional cell at the
    // boundary is ignored (its center would be outside the bbox and likely
    // outside the polygon). Acceptable since sheet resistance scales with
    // missing area proportionally for PI purposes.
    mesh.bbox_lo_x = lo_x;
    mesh.bbox_lo_y = lo_y;
    mesh.bbox_hi_x = hi_x;
    mesh.bbox_hi_y = hi_y;

    const int nx = std::max(1, static_cast<int>((hi_x - lo_x) / cfg.cell_size));
    const int ny = std::max(1, static_cast<int>((hi_y - lo_y) / cfg.cell_size));

    // Cell (i, j) → node id (or -1 if outside copper).
    std::vector<int> cell_to_node(static_cast<std::size_t>(nx) * ny, -1);

    auto cell_index = [nx](int i, int j) {
        return j * nx + i;
    };

    for (int j = 0; j < ny; ++j) {
        for (int i = 0; i < nx; ++i) {
            const double cx = lo_x + (i + 0.5) * cfg.cell_size;
            const double cy = lo_y + (j + 0.5) * cfg.cell_size;
            if (!point_in_target_copper(board, cfg.net_id, cfg.layer_ordinal,
                                        cx, cy)) {
                continue;
            }
            Node n;
            n.id = static_cast<int>(mesh.nodes.size());
            n.x = cx;
            n.y = cy;
            n.grid_i = i;
            n.grid_j = j;
            mesh.nodes.push_back(n);
            cell_to_node[cell_index(i, j)] = n.id;
        }
    }

    // Resistors: connect each in-copper node to its right and down neighbors
    // (if those neighbors also exist). Sheet-resistance conductance per square:
    const double g_per_square = cfg.copper_thickness / cfg.copper_rho;

    for (int j = 0; j < ny; ++j) {
        for (int i = 0; i < nx; ++i) {
            const int a = cell_to_node[cell_index(i, j)];
            if (a < 0) continue;
            if (i + 1 < nx) {
                const int b = cell_to_node[cell_index(i + 1, j)];
                if (b >= 0) mesh.resistors.push_back({a, b, g_per_square});
            }
            if (j + 1 < ny) {
                const int b = cell_to_node[cell_index(i, j + 1)];
                if (b >= 0) mesh.resistors.push_back({a, b, g_per_square});
            }
        }
    }

    // Source/sink: leftmost vs rightmost pad on (net, layer). Match the pad to
    // the closest in-copper node by Euclidean distance.
    auto nearest_node = [&mesh](double px, double py) -> int {
        int best = -1;
        double best_d2 = std::numeric_limits<double>::infinity();
        for (const auto& n : mesh.nodes) {
            const double dx = n.x - px;
            const double dy = n.y - py;
            const double d2 = dx * dx + dy * dy;
            if (d2 < best_d2) {
                best_d2 = d2;
                best = n.id;
            }
        }
        return best;
    };

    const model::Pad* src = nullptr;
    const model::Pad* snk = nullptr;
    for (const auto& p : board.pads) {
        if (p.net_id != cfg.net_id) continue;
        bool on_layer = false;
        for (int o : p.layer_ordinals) {
            if (o == cfg.layer_ordinal) { on_layer = true; break; }
        }
        if (!on_layer) continue;
        if (!src || p.at.x < src->at.x) src = &p;
        if (!snk || p.at.x > snk->at.x) snk = &p;
    }
    if (src && !mesh.nodes.empty()) {
        const int nid = nearest_node(src->at.x, src->at.y);
        if (nid >= 0) mesh.source_node_ids.push_back(nid);
    }
    if (snk && snk != src && !mesh.nodes.empty()) {
        const int nid = nearest_node(snk->at.x, snk->at.y);
        if (nid >= 0) mesh.sink_node_ids.push_back(nid);
    }

    return mesh;
}

}  // namespace pdnkit::pi
