# pdnkit

[![build](https://github.com/UnsignedChad/pdnkit/actions/workflows/build.yml/badge.svg)](https://github.com/UnsignedChad/pdnkit/actions/workflows/build.yml)

Open-source **Power Integrity (PI)** analysis for KiCad PCBs.

## Status

Pre-alpha but functional end-to-end. The three pillars of PI analysis are all working:

- **Static IR drop** — multi-layer sparse Cholesky on a grid mesh, with through-via wiring, connectivity-checked, edge-contact source/sink, automatic layer selection, and a track-based 1D fallback for boards routed without copper pours.
- **Frequency-domain plane Z(f)** — closed-form cavity-model self/transfer impedance for rectangular plane pairs, multi-port treatment for decoupling capacitors, greedy decap auto-suggest from a 26-entry MLCC library, target-impedance overlay, plane-shape diagnostic.
- **Time-domain transient** — backward-Euler step-response on the existing mesh with distributed per-node capacitance from the plane substrate plus lumped decap C, validated against analytical RC.

All three run from the Qt 6 GUI and headlessly from the CLI.

**KiCad demo sweep:** 19/19 parse, 15/19 complete a full static-IR-drop analysis (the 4 remaining are boards with no power-rail copper at all, not even tracks).

**Validation:** closed-form Ohm's law trace fixture (4.32 mΩ ideal vs 4.38 mV at 0.25mm cells = 1.4% error), plus tier-2 cavity-resonance frequency anchors against the analytical TM_mn formula.

## Why

The OSS EDA stack has no PI tool. OpenEMS is a general full-wave EM solver, Ngspice is a circuit simulator, FEMM is 2D — none of them answer *"is my PDN good enough?"* without weeks of setup. Commercial tools (Sigrity, SIwave, HyperLynx PI) cost \$\$\$\$.

pdnkit aims to give EEs a working PDN analysis tool that runs on their KiCad project, in minutes, for free.

## Approach

No full-wave EM. PDN problems below ~5 GHz are solved analytically:

| Method                | Module                  | Reference                                                       |
|-----------------------|-------------------------|-----------------------------------------------------------------|
| Static IR drop        | `pi/IrSolver`           | Sheet-conductance + sparse Cholesky (`G·v = i`)                |
| Track-based 1D IR     | `pi/IrMesher` fallback  | `R = ρ·L/(W·t)` per segment, endpoint dedup                    |
| Multi-layer via R     | `pi/IrMesher`           | `R = ρ·t / (π·(d/2)²)` per via barrel                          |
| Connectivity prune    | `pi/IrMesher`           | Union-find over the resistor graph                              |
| Plane Z(f)            | `pi/CavityModel`        | Okoshi 1972; Swaminathan, *Power Integrity Modeling*, eq. 5.21 |
| Decap network         | `pi/CavityModel`        | Multi-port Y-matrix with decap admittances on the diagonal      |
| Greedy decap opt      | `pi/DecapOptimizer`     | Iterative library selection minimizing target-Z excess          |
| Transient             | `pi/Transient`          | Backward Euler on `(G + C/dt)·v_{k+1} = (C/dt)·v_k + i_k`     |
| Distributed C         | `pi/Transient`          | `ε_r·ε_0·A/d` per cell + lumped decap C                        |

When SuiteSparse is available, the IR solver and transient solver both use `Eigen::CholmodSupernodalLLT` (5-20× faster than the pure-Eigen fallback for boards with thousands of nodes).

## Build (Debian / Ubuntu)

```
sudo apt install -y qt6-base-dev qt6-base-dev-tools libqt6opengl6-dev libqt6openglwidgets6 \
                    libeigen3-dev libsuitesparse-dev libcgal-dev \
                    libspdlog-dev libcli11-dev libboost-dev \
                    ninja-build cmake clang catch2
```

```
CC=clang CXX=clang++ cmake -B build -G Ninja
cmake --build build
ctest --test-dir build
./build/pdnkit                                   # empty window
./build/pdnkit my_board.kicad_pcb                # load board
```

## GUI workflow

1. **File > Open KiCad PCB...** — parses zones, tracks, vias, pads, board outline (Edge.Cuts), stackup. Ctrl+R reloads.
2. **Layers dock** — toggle visibility per copper layer. Layer thickness column reads from the parsed stackup.
3. **Analysis dock** — pick net + primary layer, check extra layers for multi-layer through-vias, edit per-pad currents in a table (live sum-balance indicator). Run.
4. **Plane Z(f) dock** — pick net (auto-fits plane bbox), set ports / dielectric / sweep range, add decoupling capacitors in a table or use **Auto-suggest** to pick from the library. Plot has bare-plane overlay and target-impedance line. **Show mode shape at peak** overlays the standing-wave pattern on the canvas.
5. **Transient dock** — step current, dt, n_steps, eps_r, substrate thickness. Plot V(t) at the observation node + max |V| over the mesh.
6. **Net Stats dock** — sortable per-net summary: pads, segments, length, copper area.
7. **Hover anywhere** — status bar shows net + layer + geometry. When a heat map is active, also shows the voltage at the cursor position.
8. **File > Save Canvas as Image / Export Results as CSV** — share results.
9. **View > Fit to Board (Home)**, Ctrl+I runs IR drop.

Window geometry, dock state, and camera position persist across launches.

## Headless / CLI

**Static IR drop:**
```
pdnkit --analyze --net +3V3 --layer F.Cu --current 2.0 --cell-size 0.3 board.kicad_pcb
```

**Cavity-model Z(f) sweep:**
```
pdnkit --zf --net +3V3 --layer F.Cu \
       --port1-x 5 --port1-y 5 --port2-x 15 --port2-y 5 \
       --f-min 1e6 --f-max 5e9 --points 300 \
       board.kicad_pcb
```

**Time-domain transient:**
```
pdnkit --transient --net +3V3 --layer F.Cu --current 1.0 \
       --dt-ns 10 --n-steps 1000 \
       --cell-size 0.5 --eps-r 4.3 --thickness 1.6 \
       board.kicad_pcb
```

All print CSV to stdout. Same exit codes (2=parse fail, 3=no net, 4=no layer, 5=empty mesh, 6=missing pads, 7=solve fail).

## Test fixtures

- `tests/fixtures/tiny_pdn.kicad_pcb` — hand-written end-to-end test board with two-layer copper, multiple nets, through-hole + SMD pads, filled zones, board outline.
- `tests/fixtures/trace_100mm.kicad_pcb` — synthetic 100mm × 10mm × 35µm trace for the closed-form Ohm's law verification (`R = ρ·L/(W·t)`).

`tests/e2e_test.cpp` and `tests/ohms_law_test.cpp` exercise these with the full pipeline.

## Architecture

See [ARCHITECTURE.md](ARCHITECTURE.md) for the module layering, data flow, and conventions.

## License

GPL-3.0 — see [LICENSE](LICENSE).
