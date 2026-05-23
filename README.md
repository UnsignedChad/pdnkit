# pdnkit

Open-source Power Integrity (PI) analysis for KiCad PCBs.

## Status

Pre-alpha. Empty window today. Goal: static IR drop from a `.kicad_pcb` with heat-map overlay, in under 10 minutes per board.

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

```
sudo apt install -y qt6-base-dev qt6-base-dev-tools libqt6opengl6-dev libqt6openglwidgets6                     libeigen3-dev libsuitesparse-dev libcgal-dev                     libspdlog-dev libcli11-dev libboost-dev                     ninja-build cmake g++
```

```
cmake -B build -G Ninja
cmake --build build
./build/pdnkit
```

## License

GPL-3.0 — see [LICENSE](LICENSE).
