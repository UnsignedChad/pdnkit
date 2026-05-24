#include "pi/IrMesher.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <numbers>
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

namespace {

// Mesh one copper layer for the target net. Appends nodes to `mesh.nodes`,
// adds sheet-conductance resistors, and returns a (cell -> node id) table
// along with the grid layout for later via-wiring lookups.
struct LayerSubmesh {
    int layer_ordinal = 0;
    std::vector<int> cell_to_node;  // size nx*ny, -1 if outside copper
    int nx = 0;
    int ny = 0;
    double lo_x = 0.0, lo_y = 0.0;
    double cell_size = 0.0;
};

LayerSubmesh mesh_one_layer(const model::Board& board, const MeshConfig& cfg,
                             int layer_ord, IrMesh& mesh, double g_per_square) {
    LayerSubmesh sm;
    sm.layer_ordinal = layer_ord;
    sm.cell_size = cfg.cell_size;

    double lo_x = 0, lo_y = 0, hi_x = 0, hi_y = 0;
    if (!target_bbox(board, cfg.net_id, layer_ord, lo_x, lo_y, hi_x, hi_y)) {
        return sm;
    }
    sm.lo_x = lo_x;
    sm.lo_y = lo_y;
    sm.nx = std::max(1, static_cast<int>((hi_x - lo_x) / cfg.cell_size));
    sm.ny = std::max(1, static_cast<int>((hi_y - lo_y) / cfg.cell_size));
    sm.cell_to_node.assign(static_cast<std::size_t>(sm.nx) * sm.ny, -1);

    auto cell_index = [&sm](int i, int j) { return j * sm.nx + i; };

    for (int j = 0; j < sm.ny; ++j) {
        for (int i = 0; i < sm.nx; ++i) {
            const double cx = lo_x + (i + 0.5) * cfg.cell_size;
            const double cy = lo_y + (j + 0.5) * cfg.cell_size;
            if (!point_in_target_copper(board, cfg.net_id, layer_ord, cx, cy)) {
                continue;
            }
            Node n;
            n.id = static_cast<int>(mesh.nodes.size());
            n.x = cx;
            n.y = cy;
            n.grid_i = i;
            n.grid_j = j;
            n.layer_ordinal = layer_ord;
            mesh.nodes.push_back(n);
            sm.cell_to_node[cell_index(i, j)] = n.id;
        }
    }
    for (int j = 0; j < sm.ny; ++j) {
        for (int i = 0; i < sm.nx; ++i) {
            const int a = sm.cell_to_node[cell_index(i, j)];
            if (a < 0) continue;
            if (i + 1 < sm.nx) {
                const int b = sm.cell_to_node[cell_index(i + 1, j)];
                if (b >= 0) mesh.resistors.push_back({a, b, g_per_square});
            }
            if (j + 1 < sm.ny) {
                const int b = sm.cell_to_node[cell_index(i, j + 1)];
                if (b >= 0) mesh.resistors.push_back({a, b, g_per_square});
            }
        }
    }
    return sm;
}

// Nearest node on a specific layer to a world point, or -1 if none.
int nearest_node_on_layer(const IrMesh& mesh, double px, double py, int layer) {
    int best = -1;
    double best_d2 = std::numeric_limits<double>::infinity();
    for (const auto& n : mesh.nodes) {
        if (n.layer_ordinal != layer) continue;
        const double dx = n.x - px;
        const double dy = n.y - py;
        const double d2 = dx * dx + dy * dy;
        if (d2 < best_d2) {
            best_d2 = d2;
            best = n.id;
        }
    }
    return best;
}

}  // namespace

IrMesh IrMesher::build(const model::Board& board, const MeshConfig& cfg) {
    IrMesh mesh;
    if (cfg.cell_size <= 0.0 || cfg.copper_thickness <= 0.0 ||
        cfg.copper_rho <= 0.0) {
        return mesh;
    }

    // Build the ordered list of layers (primary first, then extras).
    std::vector<int> layers = {cfg.layer_ordinal};
    for (int l : cfg.extra_layer_ordinals) {
        bool seen = false;
        for (int e : layers) if (e == l) { seen = true; break; }
        if (!seen) layers.push_back(l);
    }

    const double g_per_square = cfg.copper_thickness / cfg.copper_rho;

    std::vector<LayerSubmesh> submeshes;
    for (int layer : layers) {
        submeshes.push_back(mesh_one_layer(board, cfg, layer, mesh, g_per_square));
    }

    // Overall bbox = union of per-layer submeshes.
    bool any_bbox = false;
    for (const auto& sm : submeshes) {
        if (sm.nx == 0 || sm.ny == 0) continue;
        const double hi_x = sm.lo_x + sm.nx * sm.cell_size;
        const double hi_y = sm.lo_y + sm.ny * sm.cell_size;
        if (!any_bbox) {
            mesh.bbox_lo_x = sm.lo_x;
            mesh.bbox_lo_y = sm.lo_y;
            mesh.bbox_hi_x = hi_x;
            mesh.bbox_hi_y = hi_y;
            any_bbox = true;
        } else {
            mesh.bbox_lo_x = std::min(mesh.bbox_lo_x, sm.lo_x);
            mesh.bbox_lo_y = std::min(mesh.bbox_lo_y, sm.lo_y);
            mesh.bbox_hi_x = std::max(mesh.bbox_hi_x, hi_x);
            mesh.bbox_hi_y = std::max(mesh.bbox_hi_y, hi_y);
        }
    }

    if (mesh.nodes.empty()) return mesh;

    // Via wiring: for each via on the target net whose from/to layers are
    // both in our meshed set, add a via-resistor between nearest nodes on
    // each side. Via barrel resistance R = rho * L / A where L is the board
    // thickness between the two layers (approximated as total_thickness for
    // through-vias) and A = pi * (drill/2)^2.
    if (submeshes.size() >= 2) {
        for (const auto& via : board.vias) {
            if (via.net_id != cfg.net_id) continue;
            if (via.drill <= 0.0) continue;

            const LayerSubmesh* from_sm = nullptr;
            const LayerSubmesh* to_sm = nullptr;
            for (const auto& sm : submeshes) {
                if (sm.layer_ordinal == via.from_layer) from_sm = &sm;
                if (sm.layer_ordinal == via.to_layer)   to_sm   = &sm;
            }
            if (!from_sm || !to_sm || from_sm == to_sm) continue;

            const int a = nearest_node_on_layer(mesh, via.at.x, via.at.y,
                                                from_sm->layer_ordinal);
            const int b = nearest_node_on_layer(mesh, via.at.x, via.at.y,
                                                to_sm->layer_ordinal);
            if (a < 0 || b < 0) continue;

            const double L = board.stackup.total_thickness;
            const double r = 0.5 * via.drill;
            const double area = std::numbers::pi * r * r;
            if (area <= 0.0 || L <= 0.0) continue;
            const double R = cfg.copper_rho * L / area;
            const double G = 1.0 / R;
            mesh.resistors.push_back({a, b, G});
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

    auto pad_on_target = [&](const model::Pad& p) {
        if (p.net_id != cfg.net_id) return false;
        for (int o : p.layer_ordinals) {
            if (o == cfg.layer_ordinal) return true;
        }
        return false;
    };
    auto name_in = [](const std::vector<std::string>& names,
                       const std::string& n) {
        for (const auto& s : names) {
            if (s == n) return true;
        }
        return false;
    };

    const bool explicit_src = !cfg.source_pad_names.empty();
    const bool explicit_snk = !cfg.sink_pad_names.empty();

    if (explicit_src || explicit_snk) {
        // Use explicit pad lists.
        for (const auto& pad : board.pads) {
            if (!pad_on_target(pad)) continue;
            const int nid = nearest_node(pad.at.x, pad.at.y);
            if (nid < 0) continue;
            if (explicit_src && name_in(cfg.source_pad_names, pad.name)) {
                mesh.source_node_ids.push_back(nid);
            }
            if (explicit_snk && name_in(cfg.sink_pad_names, pad.name)) {
                mesh.sink_node_ids.push_back(nid);
            }
        }
    }

    // If per-pad currents are specified, map them to nodes via nearest-node.
    // This takes priority over source/sink lists in the solver.
    if (!cfg.pad_currents.empty()) {
        for (const auto& pad : board.pads) {
            if (!pad_on_target(pad)) continue;
            auto it = cfg.pad_currents.find(pad.name);
            if (it == cfg.pad_currents.end()) continue;
            const int nid = nearest_node(pad.at.x, pad.at.y);
            if (nid >= 0) mesh.node_currents.emplace_back(nid, it->second);
        }
    }

    // Auto-fill anything still missing with leftmost / rightmost pad.
    if (mesh.source_node_ids.empty() || mesh.sink_node_ids.empty()) {
        const model::Pad* src = nullptr;
        const model::Pad* snk = nullptr;
        for (const auto& pad : board.pads) {
            if (!pad_on_target(pad)) continue;
            if (!src || pad.at.x < src->at.x) src = &pad;
            if (!snk || pad.at.x > snk->at.x) snk = &pad;
        }
        if (mesh.source_node_ids.empty() && src && !mesh.nodes.empty()) {
            const int nid = nearest_node(src->at.x, src->at.y);
            if (nid >= 0) mesh.source_node_ids.push_back(nid);
        }
        if (mesh.sink_node_ids.empty() && snk && snk != src && !mesh.nodes.empty()) {
            const int nid = nearest_node(snk->at.x, snk->at.y);
            if (nid >= 0) mesh.sink_node_ids.push_back(nid);
        }
    }

    return mesh;
}

}  // namespace pdnkit::pi
