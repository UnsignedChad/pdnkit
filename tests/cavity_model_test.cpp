#include <cmath>
#include <numbers>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "pi/CavityModel.h"

using pdnkit::pi::CavityConfig;
using pdnkit::pi::cavity_impedance;
using pdnkit::pi::cavity_impedance_magnitude_sweep;
using Catch::Approx;

namespace {
constexpr double kPi = std::numbers::pi;
constexpr double kEps0 = 8.854187817e-12;
constexpr double kC = 2.99792458e8;
}

TEST_CASE("cavity: DC behavior matches parallel-plate capacitor", "[cavity]") {
    // 100mm x 100mm x 1.6mm FR-4 plate pair.
    // C = eps_r * eps_0 * A / d = 4.3 * 8.854e-12 * 0.01 / 1.6e-3 ~ 238 pF
    CavityConfig cfg;
    cfg.a = 0.100;
    cfg.b = 0.100;
    cfg.d = 1.6e-3;
    cfg.eps_r = 4.3;
    cfg.tan_delta = 0.0;  // no loss for clean comparison
    cfg.max_modes = 50;

    const double f = 100.0;             // 100 Hz (deep DC)
    const double omega = 2.0 * kPi * f;
    // Self-impedance at the plane center.
    auto Z = cavity_impedance(cfg, 0.05, 0.05, 0.05, 0.05, omega);

    const double C_expected = cfg.eps_r * kEps0 * cfg.a * cfg.b / cfg.d;
    const double mag_expected = 1.0 / (omega * C_expected);

    // At DC, the m=n=0 term dominates: Z = j w mu d/(ab) * 1/(-w^2 mu eps) = 1/(jwC).
    REQUIRE(std::abs(Z) == Approx(mag_expected).epsilon(1e-4));
    // Capacitive: imaginary part should be negative (lossless DC).
    REQUIRE(std::imag(Z) < 0.0);
    REQUIRE(std::abs(std::real(Z)) < std::abs(std::imag(Z)) * 1.0e-6);
}

TEST_CASE("cavity: reciprocity Z(p1,p2) == Z(p2,p1)", "[cavity]") {
    CavityConfig cfg;
    cfg.a = 0.080;
    cfg.b = 0.060;
    cfg.d = 0.4e-3;
    cfg.max_modes = 20;

    const double omega = 2.0 * kPi * 1.0e8;  // 100 MHz
    auto Z12 = cavity_impedance(cfg, 0.020, 0.030, 0.070, 0.050, omega);
    auto Z21 = cavity_impedance(cfg, 0.070, 0.050, 0.020, 0.030, omega);

    REQUIRE(std::abs(Z12 - Z21) / std::abs(Z12) < 1e-12);
}

TEST_CASE("cavity: |Z| peaks near first TM10 resonance", "[cavity]") {
    // Resonant frequency of TM_mn = (c / (2 sqrt(eps_r))) * sqrt((m/a)^2 + (n/b)^2)
    // TM10: f10 = c / (2 a sqrt(eps_r))
    CavityConfig cfg;
    cfg.a = 0.100;
    cfg.b = 0.100;
    cfg.d = 1.6e-3;
    cfg.eps_r = 4.3;
    cfg.tan_delta = 0.005;   // small loss so peak is finite
    cfg.max_modes = 40;

    const double f10 = kC / (2.0 * cfg.a * std::sqrt(cfg.eps_r));
    // Self-impedance at corner where TM10 mode has max amplitude.
    const double x = 0.0, y = 0.0;

    auto Z_below = cavity_impedance(cfg, x, y, x, y, 2.0 * kPi * f10 * 0.5);
    auto Z_at    = cavity_impedance(cfg, x, y, x, y, 2.0 * kPi * f10 * 1.0);
    auto Z_above = cavity_impedance(cfg, x, y, x, y, 2.0 * kPi * f10 * 2.0);

    REQUIRE(std::abs(Z_at) > std::abs(Z_below));
    REQUIRE(std::abs(Z_at) > std::abs(Z_above));
}

TEST_CASE("cavity: sweep returns vector of expected length", "[cavity]") {
    CavityConfig cfg;
    cfg.max_modes = 10;
    std::vector<double> freqs;
    for (int i = 0; i < 20; ++i) freqs.push_back(1.0e6 * (i + 1));

    auto mags = cavity_impedance_magnitude_sweep(cfg, 0.01, 0.01, 0.02, 0.02, freqs);
    REQUIRE(mags.size() == freqs.size());
    for (double m : mags) {
        REQUIRE(std::isfinite(m));
        REQUIRE(m >= 0.0);
    }
}
