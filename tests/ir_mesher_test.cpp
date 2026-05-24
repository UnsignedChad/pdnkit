#include <cmath>
#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "model/Board.h"
#include "pi/IrMesher.h"

using pdnkit::pi::IrMesher;
using pdnkit::pi::IrMesh;
using pdnkit::pi::MeshConfig;
using namespace pdnkit::model;
using Catch::Approx;

namespace {

Board with_square_zone(int net, int layer, double side) {
    Board b;
    b.stackup.layers.push_back({layer, "F.Cu", "signal"});
    b.nets.push_back({net, "VCC"});
    Zone z;
    z.net_id = net;
    z.layer_ordinal = layer;
    Polygon p;
    p.outline = {{0, 0}, {side, 0}, {side, side}, {0, side}};
    z.filled.push_back(p);
    b.zones.push_back(z);
    return b;
}

}  // namespace

TEST_CASE("mesher: no matching geometry returns empty mesh", "[irmesh]") {
    Board b;
    MeshConfig cfg;
    cfg.net_id = 99;
    cfg.layer_ordinal = 0;
    auto m = IrMesher::build(b, cfg);
    REQUIRE(m.nodes.empty());
    REQUIRE(m.resistors.empty());
}

TEST_CASE("mesher: 10mm square zone with 1mm cells → ~100 nodes", "[irmesh]") {
    Board b = with_square_zone(1, 0, 0.010);  // 10mm × 10mm
    MeshConfig cfg;
    cfg.cell_size = 1.0e-3;
    cfg.net_id = 1;
    cfg.layer_ordinal = 0;

    auto m = IrMesher::build(b, cfg);
    // 10mm / 1mm = 10 cells per axis → 10*10 = 100 nodes.
    REQUIRE(m.nodes.size() == 100);
    // Edge resistors: 2 * 10 * 9 = 180 (horizontal + vertical interior bonds).
    REQUIRE(m.resistors.size() == 180);
}

TEST_CASE("mesher: invalid config returns empty mesh", "[irmesh]") {
    Board b = with_square_zone(1, 0, 0.010);
    MeshConfig cfg;
    cfg.net_id = 1;
    cfg.layer_ordinal = 0;
    cfg.cell_size = 0.0;  // invalid
    REQUIRE(IrMesher::build(b, cfg).nodes.empty());
}

TEST_CASE("mesher: per-resistor conductance = t/ρ for square cells", "[irmesh]") {
    Board b = with_square_zone(1, 0, 0.010);
    MeshConfig cfg;
    cfg.cell_size = 1.0e-3;
    cfg.copper_thickness = 35.0e-6;
    cfg.copper_rho = 1.68e-8;
    cfg.net_id = 1;
    cfg.layer_ordinal = 0;

    auto m = IrMesher::build(b, cfg);
    REQUIRE_FALSE(m.resistors.empty());
    const double expected = 35.0e-6 / 1.68e-8;  // ≈ 2083 S
    REQUIRE(m.resistors.front().conductance == Approx(expected));
}

TEST_CASE("mesher: mesh respects zone hole (no nodes inside)", "[irmesh]") {
    Board b;
    b.stackup.layers.push_back({0, "F.Cu", "signal"});
    b.nets.push_back({1, "VCC"});
    Zone z;
    z.net_id = 1;
    z.layer_ordinal = 0;
    Polygon p;
    p.outline = {{0, 0}, {0.010, 0}, {0.010, 0.010}, {0, 0.010}};
    p.holes.push_back({{0.003, 0.003}, {0.007, 0.003}, {0.007, 0.007}, {0.003, 0.007}});
    z.filled.push_back(p);
    b.zones.push_back(z);

    MeshConfig cfg;
    cfg.cell_size = 1.0e-3;
    cfg.net_id = 1;
    cfg.layer_ordinal = 0;

    auto m = IrMesher::build(b, cfg);
    // 100 - 16 (4x4 hole cells) = 84 expected nodes.
    REQUIRE(m.nodes.size() == 84);
}

TEST_CASE("mesher: leftmost and rightmost pads become source and sink", "[irmesh]") {
    Board b = with_square_zone(1, 0, 0.010);
    // Two pads on the target net+layer at opposite ends.
    Pad p1; p1.at = {0.001, 0.005}; p1.net_id = 1; p1.layer_ordinals = {0};
    Pad p2; p2.at = {0.009, 0.005}; p2.net_id = 1; p2.layer_ordinals = {0};
    b.pads.push_back(p1);
    b.pads.push_back(p2);

    MeshConfig cfg;
    cfg.cell_size = 1.0e-3;
    cfg.net_id = 1;
    cfg.layer_ordinal = 0;

    auto m = IrMesher::build(b, cfg);
    REQUIRE(m.source_node_ids.size() == 1);
    REQUIRE(m.sink_node_ids.size() == 1);

    const auto& src = m.nodes[m.source_node_ids[0]];
    const auto& snk = m.nodes[m.sink_node_ids[0]];
    REQUIRE(src.x < snk.x);  // leftmost pad → leftmost node
}

TEST_CASE("mesher: pad on wrong net or layer is ignored", "[irmesh]") {
    Board b = with_square_zone(1, 0, 0.010);
    Pad p1; p1.at = {0.001, 0.005}; p1.net_id = 2 /* wrong net */; p1.layer_ordinals = {0};
    Pad p2; p2.at = {0.009, 0.005}; p2.net_id = 1; p2.layer_ordinals = {31} /* wrong layer */;
    b.pads.push_back(p1);
    b.pads.push_back(p2);

    MeshConfig cfg;
    cfg.cell_size = 1.0e-3;
    cfg.net_id = 1;
    cfg.layer_ordinal = 0;

    auto m = IrMesher::build(b, cfg);
    REQUIRE(m.source_node_ids.empty());
    REQUIRE(m.sink_node_ids.empty());
}

TEST_CASE("mesher: bbox matches the source polygon bbox", "[irmesh]") {
    Board b = with_square_zone(1, 0, 0.010);
    MeshConfig cfg;
    cfg.cell_size = 1.0e-3;
    cfg.net_id = 1;
    cfg.layer_ordinal = 0;
    auto m = IrMesher::build(b, cfg);
    REQUIRE(m.bbox_lo_x == Approx(0.0));
    REQUIRE(m.bbox_lo_y == Approx(0.0));
    REQUIRE(m.bbox_hi_x == Approx(0.010));
    REQUIRE(m.bbox_hi_y == Approx(0.010));
}

TEST_CASE("mesher: explicit source/sink pad names override auto-pick", "[irmesh]") {
    Board b = with_square_zone(1, 0, 0.010);
    // Three pads — auto-pick would pick A (leftmost) and C (rightmost).
    // Explicit config should pick B and C instead.
    Pad pa; pa.at = {0.001, 0.005}; pa.net_id = 1; pa.layer_ordinals = {0}; pa.name = "A";
    Pad pb; pb.at = {0.005, 0.005}; pb.net_id = 1; pb.layer_ordinals = {0}; pb.name = "B";
    Pad pc; pc.at = {0.009, 0.005}; pc.net_id = 1; pc.layer_ordinals = {0}; pc.name = "C";
    b.pads.push_back(pa);
    b.pads.push_back(pb);
    b.pads.push_back(pc);

    MeshConfig cfg;
    cfg.cell_size = 1.0e-3;
    cfg.net_id = 1;
    cfg.layer_ordinal = 0;
    cfg.source_pad_names = {"B"};
    cfg.sink_pad_names = {"C"};

    auto m = IrMesher::build(b, cfg);
    REQUIRE(m.source_node_ids.size() == 1);
    REQUIRE(m.sink_node_ids.size() == 1);

    const auto& src = m.nodes[m.source_node_ids[0]];
    const auto& snk = m.nodes[m.sink_node_ids[0]];
    // Source picked is the one nearest pad B (x ~= 0.005), not pad A
    // (x = 0.001) — the grid snaps to the nearest cell center, which is
    // 0.0045 or 0.0055 depending on tie-break.
    REQUIRE(std::abs(src.x - 0.005) < 0.001);
    REQUIRE(std::abs(snk.x - 0.009) < 0.001);
}

TEST_CASE("mesher: missing-name explicit list falls back to auto-pick", "[irmesh]") {
    Board b = with_square_zone(1, 0, 0.010);
    Pad p1; p1.at = {0.001, 0.005}; p1.net_id = 1; p1.layer_ordinals = {0}; p1.name = "L";
    Pad p2; p2.at = {0.009, 0.005}; p2.net_id = 1; p2.layer_ordinals = {0}; p2.name = "R";
    b.pads.push_back(p1);
    b.pads.push_back(p2);

    MeshConfig cfg;
    cfg.cell_size = 1.0e-3;
    cfg.net_id = 1;
    cfg.layer_ordinal = 0;
    cfg.source_pad_names = {"DoesNotExist"};  // explicit but no match

    auto m = IrMesher::build(b, cfg);
    // source auto-fills to leftmost (L); sink also auto-fills to rightmost (R).
    REQUIRE(m.source_node_ids.size() == 1);
    REQUIRE(m.sink_node_ids.size() == 1);
}
