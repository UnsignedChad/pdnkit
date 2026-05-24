// Closed-form Ohm's law verification.
//
// trace_100mm.kicad_pcb is a 100mm × 10mm × 35μm (1 oz) copper rectangle
// with two pads on the VRAIL net at x=5mm and x=95mm. The pads end up as
// the source and sink for static IR drop.
//
// For a uniform-current rectangular sheet conductor of length L (between
// the pads), width W, thickness t, made of copper (ρ = 1.68e-8 Ω·m):
//
//      R_ideal = ρ · L / (W · t)
//      V_ideal = I · R_ideal
//
// Numbers: L = 90mm, W = 10mm, t = 35μm, I = 1A
//      R_ideal ≈ 4.32 mΩ
//      V_ideal ≈ 4.32 mV
//
// pdnkit's solver mesh-discretizes this, so the result has two real
// physical residuals on top of the ideal:
//   * Spreading resistance at each pad-to-trace contact (we drive point
//     loads, not uniform line contacts) -- typically 0.3-1 mΩ per end
//     on this geometry.
//   * Discretization error of the grid mesh -- shrinks with cell_size.
//
// So we expect the actual mesh result to be 4-7 mV, conservatively
// within ±50% of the ideal. Tightening tolerance once we add edge-
// contact source/sink support is a follow-up.

#include <cmath>
#include <filesystem>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "parser/KicadPcbParser.h"
#include "pi/IrMesher.h"
#include "pi/IrSolver.h"

#ifndef PDNKIT_TEST_FIXTURES_DIR
#error "PDNKIT_TEST_FIXTURES_DIR must be defined by CMake."
#endif

using pdnkit::parser::KicadPcbParser;
using pdnkit::pi::IrMesher;
using pdnkit::pi::IrSolver;
using pdnkit::pi::MeshConfig;

namespace {
std::filesystem::path fixture(const char* name) {
    return std::filesystem::path(PDNKIT_TEST_FIXTURES_DIR) / name;
}

constexpr double kRhoCu        = 1.68e-8;   // Ω·m at 20°C
constexpr double kCuThickness  = 35.0e-6;   // 1 oz
constexpr double kTraceWidth   = 0.010;     // 10 mm
constexpr double kPadDistance  = 0.090;     // 90 mm between pad centers (5 → 95)
constexpr double kCurrent      = 1.0;       // A
}

TEST_CASE("ohms-law: 100mm trace solves close to ρL/(Wt)", "[ohms][validation]") {
    auto b = KicadPcbParser::parse_file(fixture("trace_100mm.kicad_pcb"));

    REQUIRE(b.find_net_by_name("VRAIL") != nullptr);
    REQUIRE(b.zones.size() == 1);
    REQUIRE(b.pads.size() == 2);

    MeshConfig mc;
    mc.cell_size = 0.5e-3;  // 0.5 mm — gives ~200x20 = 4000 cells
    mc.net_id = b.find_net_by_name("VRAIL")->id;
    mc.layer_ordinal = 0;
    mc.copper_thickness = kCuThickness;
    mc.copper_rho = kRhoCu;

    auto mesh = IrMesher::build(b, mc);
    REQUIRE(mesh.nodes.size() > 1000);
    REQUIRE(mesh.source_node_ids.size() == 1);
    REQUIRE(mesh.sink_node_ids.size() == 1);

    auto sol = IrSolver::solve(mesh, {kCurrent});
    REQUIRE(sol.ok);

    const double v_drop_v = sol.max_v - sol.min_v;
    const double v_drop_mv = v_drop_v * 1000.0;

    const double r_ideal = kRhoCu * kPadDistance / (kTraceWidth * kCuThickness);
    const double v_ideal_mv = (kCurrent * r_ideal) * 1000.0;

    INFO("V_ideal (point-load excluded) = " << v_ideal_mv << " mV");
    INFO("V_drop (pdnkit mesh)          = " << v_drop_mv << " mV");
    INFO("ratio = " << v_drop_mv / v_ideal_mv);

    // Within +/-50% of the closed-form value (mesh + point-load spreading).
    REQUIRE(v_drop_mv >= 0.5 * v_ideal_mv);
    REQUIRE(v_drop_mv <= 1.5 * v_ideal_mv);

    // Should be within ~2x at worst — guard against gross math regressions.
    REQUIRE(v_drop_mv >= 0.5 * v_ideal_mv);
    REQUIRE(v_drop_mv <= 2.0 * v_ideal_mv);
}

TEST_CASE("ohms-law: drop scales linearly with current", "[ohms][validation]") {
    auto b = KicadPcbParser::parse_file(fixture("trace_100mm.kicad_pcb"));
    MeshConfig mc;
    mc.cell_size = 0.5e-3;
    mc.net_id = b.find_net_by_name("VRAIL")->id;
    mc.layer_ordinal = 0;

    auto mesh = IrMesher::build(b, mc);

    auto s1 = IrSolver::solve(mesh, {1.0});
    auto s3 = IrSolver::solve(mesh, {3.0});
    REQUIRE(s1.ok);
    REQUIRE(s3.ok);

    const double r1 = s1.max_v - s1.min_v;
    const double r3 = s3.max_v - s3.min_v;
    // Should be exactly 3:1 (linear system).
    REQUIRE(r3 / r1 == Catch::Approx(3.0).epsilon(1e-9));
}

TEST_CASE("ohms-law: result tightens as cell size shrinks", "[ohms][validation]") {
    auto b = KicadPcbParser::parse_file(fixture("trace_100mm.kicad_pcb"));
    const double r_ideal = kRhoCu * kPadDistance / (kTraceWidth * kCuThickness);

    auto solve_at = [&](double cell_size_m) {
        MeshConfig mc;
        mc.cell_size = cell_size_m;
        mc.net_id = b.find_net_by_name("VRAIL")->id;
        mc.layer_ordinal = 0;
        auto mesh = IrMesher::build(b, mc);
        auto sol = IrSolver::solve(mesh, {1.0});
        REQUIRE(sol.ok);
        return sol.max_v - sol.min_v;
    };

    const double v_coarse = solve_at(1.0e-3);   // 1.0mm
    const double v_fine   = solve_at(0.25e-3);  // 0.25mm

    INFO("R_ideal = " << r_ideal * 1000.0 << " mΩ");
    INFO("V at 1.0mm  = " << v_coarse * 1000.0 << " mV");
    INFO("V at 0.25mm = " << v_fine   * 1000.0 << " mV");

    // Both within ±50% of ideal.
    REQUIRE(v_coarse >= 0.5 * r_ideal);
    REQUIRE(v_coarse <= 1.5 * r_ideal);
    REQUIRE(v_fine   >= 0.5 * r_ideal);
    REQUIRE(v_fine   <= 1.5 * r_ideal);

    // With edge-contact source/sink (gap #3) the mesh converges TOWARD the
    // analytical R_ideal as cells shrink. Finer mesh must be at least as
    // close to ideal as the coarse mesh.
    const double err_coarse = std::abs(v_coarse - r_ideal);
    const double err_fine   = std::abs(v_fine   - r_ideal);
    REQUIRE(err_fine <= err_coarse);
    // And the 0.25mm result must hit within 5%.
    REQUIRE(err_fine / r_ideal < 0.05);
}
