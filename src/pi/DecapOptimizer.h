// Greedy decap selection: given a target |Z(f)| over a frequency band,
// pick the smallest set of decoupling capacitors from a library that pulls
// the plane impedance below target.
//
// Algorithm: at each step, find the candidate library cap that reduces the
// integrated above-target excess the most when added at a fixed position.
// Repeats until target met or max_caps exceeded.

#pragma once

#include <string>
#include <vector>

#include "pi/CavityModel.h"

namespace pdnkit::pi {

struct LibraryDecap {
    const char* name;
    double C;     // capacitance (F)
    double esr;   // equiv series resistance (ohm)
    double esl;   // equiv series inductance (H)
};

// Default decap library: representative X5R/X7R MLCCs at common values, with
// ESR/ESL numbers in the ballpark for typical 0402/0603/0805 cases. Values
// are intentionally rounded for clarity; replace with vendor-specific data
// when modeling a real BOM.
constexpr LibraryDecap kCommonDecaps[] = {
    {"10 uF X5R 0805",   10.0e-6,   5.0e-3, 0.7e-9},
    {"1 uF X7R 0603",     1.0e-6,  10.0e-3, 0.5e-9},
    {"100 nF X7R 0402", 100.0e-9,  25.0e-3, 0.3e-9},
    {"10 nF X7R 0402",   10.0e-9,  30.0e-3, 0.3e-9},
    {"1 nF X7R 0402",     1.0e-9,  50.0e-3, 0.3e-9},
};

struct DecapOptimizerConfig {
    double target_z = 0.025;     // ohm
    double f_min    = 1.0e6;     // Hz
    double f_max    = 1.0e9;
    int    n_points = 50;        // log-spaced sweep points used during search
    double cap_x    = 0.0;       // position (m) where all suggested caps go
    double cap_y    = 0.0;
    int    max_caps = 30;        // safety cap on iteration count
};

struct DecapOptimizerResult {
    std::vector<Decap> decaps;   // the selected decap network
    bool target_met = false;     // true if all sweep points end up below target
    double final_max_z = 0.0;    // max |Z| over the sweep after selection
};

// Library available to UIs that want to expose value-pick lists.
constexpr int kCommonDecapCount = sizeof(kCommonDecaps) / sizeof(kCommonDecaps[0]);

// Run the greedy search.
DecapOptimizerResult optimize_decaps(
    const CavityConfig& cavity,
    double obs_x, double obs_y,
    const DecapOptimizerConfig& opt);

}  // namespace pdnkit::pi
