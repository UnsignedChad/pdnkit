# pdnkit

[![build](https://github.com/UnsignedChad/pdnkit/actions/workflows/build.yml/badge.svg)](https://github.com/UnsignedChad/pdnkit/actions/workflows/build.yml)

Open-source **Power Integrity (PI)** analysis for KiCad PCBs.

## Status

Pre-alpha but functional. Two pillars of PI analysis are working end-to-end:

- **Static IR drop** — sparse Cholesky solve on a multi-layer grid mesh, vias wire the layers, heat-map overlay with viridis colormap, per-pad current sources.
- **Frequency-domain plane Z(f)** — closed-form cavity-model self/transfer impedance for rectangular plane pairs, multi-port treatment for decoupling capacitors, log-log plot with target-impedance line and bare-plane overlay.

Both run from the GUI (Qt 6) and headlessly from the CLI.

## Why

The OSS EDA stack has no PI tool. OpenEMS is a general full-wave EM solver, Ngspice is a circuit simulator, FEMM is 2D — none of them answer *"is my PDN good enough?"* without weeks of setup. Commercial tools (Sigrity, SIwave, HyperLynx PI) cost \$\$\$\$.

pdnkit aims to give EEs a working PDN analysis tool that runs on their KiCad project, in minutes, for free.

## Approach

No full-wave EM. PDN problems below ~5 GHz are solved analytically:

| Method                | Module           | Reference                                                       |
|-----------------------|------------------|-----------------------------------------------------------------|
| Static IR drop        | `pi/IrSolver`    | Sheet-conductance + sparse Cholesky (`G·v = i`)                |
| Multi-layer via R     | `pi/IrMesher`    | `R = ρ·t / (π·(d/2)²)` per via barrel                          |
| Plane Z(f)            | `pi/CavityModel` | Okoshi 1972; Swaminathan, *Power Integrity Modeling*, eq. 5.21 |
| Decap network         | `pi/CavityModel` | Multi-port Y-matrix with decap admittances on the diagonal      |

When SuiteSparse is available, the IR solver uses `Eigen::CholmodSupernodalLLT` (5-20x faster than the pure-Eigen fallback for boards with thousands of nodes).

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

g++ also works; both are CI-tested.

## Workflow

### GUI

1. **File > Open KiCad PCB...** — parses zones, tracks, vias, pads, board outline (Edge.Cuts).
2. **Layers dock (right)** — toggle visibility per copper layer; works for both board geometry and the heat-map overlay.
3. **Analysis dock (right, tab)** — pick net + primary layer, check extra layers for multi-layer through-vias, edit per-pad current table, hit Run.
4. **Net Stats dock (right, tab)** — sortable per-net summary: pads, segments, length, copper area.
5. **Plane Z(f) dock (right, tab)** — pick net for plane bounds, set ports / dielectric / sweep range, add decoupling capacitors in a table, hit Run.
6. **Color legend (right)** — viridis scale with min/mid/max voltage labels.
7. **Hover anywhere on the canvas** — status bar shows net + layer + geometry kind under the cursor.
8. **File > Save Canvas as Image / Export Results as CSV** — share results.
9. **View > Fit to Board (Home)** — re-fit the camera; **Ctrl+I** runs static IR drop.

Window geometry, dock layout, and camera position persist across launches via QSettings.

### Headless / CLI

**Static IR drop:**
```
pdnkit --analyze --net +3V3 --layer F.Cu --current 2.0 --cell-size 0.3 board.kicad_pcb
```
Output (single line):
```
pdnkit IR drop  net=+3V3  layer=F.Cu  current=2.000A  nodes=684  resistors=1312
                Vmax=1.130mV  Vmin=0.000mV  drop=1.130mV
```

**Cavity-model Z(f) sweep:**
```
pdnkit --zf --net +3V3 --layer F.Cu \
       --port1-x 5 --port1-y 5 --port2-x 15 --port2-y 5 \
       --f-min 1e6 --f-max 5e9 --points 300 \
       board.kicad_pcb
```
Output: CSV (`freq_hz,abs_z_ohm`) to stdout.

## Test fixtures

`tests/fixtures/tiny_pdn.kicad_pcb` is a hand-written but real-format KiCad PCB used for end-to-end tests. Distill any real-board parser bugs into a new fixture there.

## Architecture

See [ARCHITECTURE.md](ARCHITECTURE.md) for the module layering, data flow, and conventions.

## License

GPL-3.0 — see [LICENSE](LICENSE).
