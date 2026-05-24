# pdnkit

[![build](https://github.com/UnsignedChad/pdnkit/actions/workflows/build.yml/badge.svg)](https://github.com/UnsignedChad/pdnkit/actions/workflows/build.yml)

Open-source Power Integrity (PI) analysis for KiCad PCBs.

## Status

Pre-alpha. The GUI loads a `.kicad_pcb` and reports a summary in the status bar. Rendering and analysis are coming.

Goal: static IR drop from a `.kicad_pcb` with heat-map overlay, in under 10 minutes per board.

## Why

The OSS EDA stack has no PI tool. OpenEMS is a general EM solver, Ngspice is a circuit simulator, FEMM is 2D — none of them answer "is my PDN good enough?" without weeks of setup. Commercial tools (Sigrity, SIwave, HyperLynx PI) cost $$$$.

pdnkit aims to give EEs a working PDN analysis tool that runs on their KiCad project, in minutes, for free.

## Approach

No full-wave EM. PDN problems below ~5 GHz are solved by:

- **Static IR drop** — sparse linear solve on meshed copper (`G·v = i`)
- **Plane Z(f)** — segmentation method (Okoshi / Swaminathan) + cavity model
- **Via inductance** — partial inductance (Ruehli)
- **Decap network** — lumped R-L-C, node analysis

Reference: Swaminathan, *Power Integrity Modeling and Design for Semiconductors and Systems*.

## Build (Debian/Ubuntu)

Install deps:

```
sudo apt install -y qt6-base-dev qt6-base-dev-tools libqt6opengl6-dev libqt6openglwidgets6                     libeigen3-dev libsuitesparse-dev libcgal-dev                     libspdlog-dev libcli11-dev libboost-dev                     ninja-build cmake clang catch2
```

Configure with clang (recommended — better diagnostics, faster compiles, aligns with our clang-format/clang-tidy tooling):

```
CC=clang CXX=clang++ cmake -B build -G Ninja
cmake --build build
ctest --test-dir build
./build/pdnkit                           # empty window
./build/pdnkit --open my_board.kicad_pcb # loads on startup
```

g++ also works; both are CI-tested. Omit `CC=`/`CXX=` to use the system default.

## License

GPL-3.0 — see [LICENSE](LICENSE).
