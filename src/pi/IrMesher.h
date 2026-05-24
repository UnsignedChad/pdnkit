// Regular-grid mesher for static IR-drop analysis.
//
// Takes the filled copper polygons of a single (layer, net) pair and produces
// a 2D grid mesh where each grid cell that lies inside the copper becomes a
// node, and each pair of adjacent in-copper nodes becomes a resistor.
//
// For square cells, the resistance between adjacent cell centers reduces to
// the sheet resistance ρ/t (Ω per square), independent of grid spacing —
// see Wadell, "Transmission Line Design Handbook," §3.2 for the derivation.
// So conductance per resistor: G = t / ρ  (Siemens).
//
// Source/sink nodes are taken from pads on (layer, net): leftmost pad → source,
// rightmost pad → sink. The full per-pad current-source UI lands in a later
// commit; this is the minimal viable IR-drop input.

#pragma once

#include <string>
#include <vector>

#include "model/Board.h"

namespace pdnkit::pi {

struct MeshConfig {
    double cell_size = 0.5e-3;          // grid spacing in meters (default 0.5 mm)
    double copper_thickness = 35.0e-6;  // 1 oz copper (35 µm)
    double copper_rho = 1.68e-8;        // copper resistivity Ω·m at 20°C
    int net_id = 0;                     // target net (e.g., a power rail)
    int layer_ordinal = 0;              // target copper layer

    // Optional explicit source/sink pad selection by pad name. When non-empty,
    // these override the v0 leftmost/rightmost auto-pick. Pads must match
    // (net_id, layer_ordinal) and have the given name.
    std::vector<std::string> source_pad_names;
    std::vector<std::string> sink_pad_names;
};

struct Node {
    int id = 0;
    double x = 0.0;
    double y = 0.0;
    int grid_i = 0;  // column in the source grid
    int grid_j = 0;  // row in the source grid
};

struct Resistor {
    int from_node = 0;
    int to_node = 0;
    double conductance = 0.0;  // Siemens
};

struct IrMesh {
    std::vector<Node> nodes;
    std::vector<Resistor> resistors;
    std::vector<int> source_node_ids;
    std::vector<int> sink_node_ids;
    // World-space bbox of the meshed copper (handy for renderers).
    double bbox_lo_x = 0.0, bbox_lo_y = 0.0;
    double bbox_hi_x = 0.0, bbox_hi_y = 0.0;
};

class IrMesher {
public:
    // Build the grid mesh. Returns an empty mesh if the (net, layer) pair has
    // no zone fill.
    static IrMesh build(const model::Board& board, const MeshConfig& cfg);
};

}  // namespace pdnkit::pi
