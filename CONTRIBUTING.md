# Contributing to pdnkit

Thanks for looking at the code. pdnkit is pre-alpha but functional —
new commits land regularly, the architecture is settled enough that
external contributions are welcome.

## Quick start

```
sudo apt install -y qt6-base-dev qt6-base-dev-tools libqt6opengl6-dev libqt6openglwidgets6 \
                    libeigen3-dev libsuitesparse-dev libcgal-dev \
                    libspdlog-dev libcli11-dev libboost-dev \
                    ninja-build cmake clang catch2

CC=clang CXX=clang++ cmake -B build -G Ninja
cmake --build build
ctest --test-dir build
```

If `ctest` is green, you're set. clang is the recommended toolchain
(faster, sharper diagnostics) but CI tests both `gcc` and `clang`.

## Where new code goes

| New feature category | Belongs in |
|----------------------|------------|
| Anything that touches Qt, OpenGL, dialogs, widgets | `src/` flat (next to `MainWindow.cpp`) |
| Pure math, parsing, model types, file I/O | `src/sexpr/`, `src/parser/`, `src/model/`, `src/pi/`, `src/render/` (no Qt deps) |
| Tests | `tests/` — one file per logical unit |
| Vendored single-header deps | `third_party/<org>/<name>.hpp` |
| KiCad fixture boards | `tests/fixtures/` |
| Runnable example scripts | `examples/` |

The hard rule: **`pdnkit_core` must not depend on Qt.** Anything callable
from a unit test or from the CLI lives in `pdnkit_core`; only the GUI
shell links Qt. This keeps the math testable and portable.

## Build targets

Two CMake targets:

- `pdnkit_core` — static lib. Parsers, model, solvers, renderers (the
  data-side, no Qt).
- `pdnkit` — GUI binary. Links `pdnkit_core` + Qt + GL.

Both linked by `pdnkit_tests` via Catch2 v3.

## Testing conventions

- **Catch2 v3.** Use `TEST_CASE("description with category", "[tag]")`.
  Tags like `[parser]`, `[solver]`, `[cavity]`, `[track]`, `[validation]`
  let you filter test runs by area.
- **Tiny in-memory fixtures.** Most tests build a `model::Board` by hand
  with 2–4 nodes. Don't pull in a real `.kicad_pcb` unless you're
  specifically testing the parser end-to-end.
- **Two real fixtures** live in `tests/fixtures/`:
    - `tiny_pdn.kicad_pcb` — end-to-end test across parser → mesher →
      solver → render.
    - `trace_100mm.kicad_pcb` — closed-form Ohm's law verification
      anchor.
- **Validation tests get a `[validation]` tag.** These are anchored
  against analytical formulas (Ohm's law, cavity TM_mn frequency,
  parallel-plate capacitance, RC step response). When you add a new
  physical feature, add a closed-form check.

`ctest --output-on-failure -R '\\[tag\\]'` runs just the tagged tests.

## How to add a feature

1. **Domain code first.** Build types/functions in `pdnkit_core` with no
   Qt deps. Compile.
2. **Tests.** Cover happy path + at least one error case. If you're
   adding new physics, add an analytical-anchor test.
3. **CMake wiring.** Add the new `.cpp` to `add_library(pdnkit_core)` in
   the root `CMakeLists.txt`, and the test to `tests/CMakeLists.txt`.
4. **GUI plumbing.** Add a menu/panel/signal in the appropriate `src/`
   widget. Wrap solver/parser entrypoints in
   `try { ... } catch (const std::exception& e) { QMessageBox::critical(...); }`.
5. **Run `ctest`.** Must be green before commit.
6. **Commit.** Small, focused, one logical change. See the existing
   commit history for the style (subject describes what + why, body
   gives technical detail and any tradeoffs).

## Parser robustness

KiCad files in the wild are messy — v6, v7, v8, v9, v10 all have format
variations, and individual boards have constructs we haven't seen.

When a real `.kicad_pcb` fails to parse, the error includes line and
column. Distill the failing form into a minimal reproduction in
`tests/fixtures/` and add a parser test. The parser deliberately tries
to be **lenient on trailing junk and unknown layer names** but **strict
on malformed core geometry** — keep that distinction when extending.

## Math references

The PI physics is rooted in published references — when you extend the
solvers, point at the same sources so reviewers can verify:

- **Static IR drop** — sheet conductance + sparse Cholesky.
  Wadell, *Transmission Line Design Handbook*, §3.2.
- **Plane Z(f)** — Okoshi 1972; Swaminathan & Engin, *Power Integrity
  Modeling and Design for Semiconductors and Systems*, Ch. 5.
- **Via inductance** — Ruehli, *Inductance Calculations in a Complex
  Integrated Circuit Environment*, IBM J. R&D 16(5), 1972.
- **Time-domain transient** — Backward Euler on `(G + C/dt)·v_{k+1} =
  (C/dt)·v_k + i_k`.

## Style

- C++20, `-Wall -Wextra -Wpedantic`. New code should compile clean
  under those.
- `clang-format` is configured (`.clang-format`); run it before
  committing.
- Headers: `#pragma once`. PIMPL only when there's a real ABI reason.
- Naming: types in `PascalCase`, functions in `snake_case`, members
  with trailing underscore. Namespaces lowercase
  (`pdnkit::sexpr`, `pdnkit::model`, `pdnkit::pi`, `pdnkit::render`,
  `pdnkit::hittest`, `pdnkit::parser`).

## Commit style

- Subject line: imperative, ≤72 chars, describes the change.
- Body: explain *why* and any tradeoffs, not just *what*. Include
  numerical impact where relevant (e.g. "test goes from 12% error to
  1.4%", "demo sweep goes from 15/19 to 16/19").
- No Co-Authored-By trailers or autogenerated lines. One human author.
- Use the GitHub noreply email (`<id>+<username>@users.noreply.github.com`)
  for commits so attribution links to your profile.

## License

GPL-3.0. By contributing you agree your changes ship under the same
license.
