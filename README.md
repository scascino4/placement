# Placement parser and renderer

A dependency-free C++23 pipeline for physical-design placement data. It parses
Bookshelf row-based and LEF/DEF designs into a format-neutral in-memory model,
serializes that model to a compact binary file, and renders the result as a
placement view or density heatmap.

| Placement | Utilization |
| :---: | :---: |
| ![Placed cells and macros](docs/images/placement.png) | ![Placement utilization heatmap](docs/images/utilization.png) |
| **Pin density** | **Cell density** |
| ![Pin-density heatmap](docs/images/pin-density.png) | ![Cell-density heatmap](docs/images/cell-density.png) |

_The same adaptec1 placement rendered four ways._

The parser, serializer, and renderer communicate only through the
format-neutral `placement::Board` model. New input formats, persistent formats,
and renderers can therefore be added independently.

## Build

Building requires Make and a C++23 compiler; production code and tests use only
the C++ standard library. The Makefile defaults to `clang++`, which can be
overridden with `CXX`.

```sh
make
make test
```

This creates:

- `build/bin/placement_parse` — Bookshelf or LEF/DEF to binary
- `build/bin/placement_render` — binary to SVG

## Use

Parse a Bookshelf AUX manifest:

```sh
build/bin/placement_parse design.dp.aux design.placebin
```

Parse a DEF design with one or more technology and cell-library LEFs (repeat
`--lef-file` for each file):

```sh
build/bin/placement_parse --input-format lefdef \
  --lef-file tech.lef --lef-file cells.lef \
  after_legalized.def design.placebin
```

Render the placement:

```sh
build/bin/placement_render design.placebin placement.svg
```

Render any of the available analysis views:

```sh
build/bin/placement_render --output-format utilization-svg \
  design.placebin utilization.svg
build/bin/placement_render --output-format pin-density-svg \
  design.placebin pin-density.svg
build/bin/placement_render --output-format cell-density-svg \
  design.placebin cell-density.svg
```

Input defaults to Bookshelf and output defaults to the placement SVG. Use
`--placement-file placement.pl` with Bookshelf input to replace the placement
named by the AUX manifest. Use `--bin-size SIZE` to set the heatmap bin width
or `--dark-mode` to select the alternate palette. The only serialization format
currently implemented is `binary`. Run either executable with `--help` for its
complete syntax.

## Supported data

The Bookshelf backend reads `.aux`, `.nodes`, `.nets`, optional `.wts`, `.scl`,
and `.pl` files. It preserves cells, macros, fixed and non-interacting objects,
rows and subrows, nets, pins, weights, placements, orientations, optional
placement dimensions, and pin offsets.

The LEF/DEF backend reads placement sites, cell and block macros, rectangular
signal-pin geometry, rows, components, top-level pins, normal nets, and
rectangular placement blockages. Geometry is normalized to microns and
placement blockages are represented as gaps in row subrows. Routing layers,
vias, tracks, special nets, obstructions, regions, groups, and other routing
detail are recognized but omitted because they have no representation in
`placement::Board`.

Both backends validate declared section counts and cross-references. Text-parser
errors identify the source path and line.

SVG output supports:

- `svg`: rows, movable cells, macros, and fixed objects
- `utilization-svg`: movable area relative to available legal row area
- `pin-density-svg`: oriented pin locations, saturated at the 95th percentile
- `cell-density-svg`: exact movable-object overlap per available bin area

Heatmaps use green, yellow, and red for increasing density. Macro footprints
remain visible, and detailed bin values are embedded as SVG tooltips. Every
view uses the full design bounds, while heatmap bins remain confined to the
legal placement region.

## Binary format

All integers are fixed-width little-endian values and all real values are
IEEE-754 binary64. A `PLACEBIN` file contains, in order:

1. The eight-byte `PLACEBIN` magic.
2. A length-prefixed design name and 64-bit cell, row, net, and pin counts.
3. Cell records: name, dimensions, kind, macro flag, optional placement (with
   coordinates, orientation, status, and optional overridden dimensions), and
   weights.
4. Row records: geometry, orientation, symmetry, and subrows.
5. Net records: name, flattened pin range, and weights.
6. Pin records: cell index, direction, and two offsets.

Strings and weight vectors have 32-bit lengths. The magic currently has no
separate format-version field. Readers validate counts, references, enum and
boolean values, truncation, and trailing data; writers replace outputs
atomically.

## Benchmarks

Download the ISPD 2005, movable-macro, and ISPD 2015 benchmark sets, then
generate every currently supported binary and SVG view:

```sh
./scripts/prepare_data.sh
make -j 8 outputs
```

Results are grouped by benchmark family under `out/ispd2005`,
`out/ispd2005free`, and `out/ispd2015`. If matching DREAMPlace `.gp.pl` or
`.gp.def` files are available, `make outputs` also renders them under the
corresponding `*-dreamplace` directory. An individual ISPD 2015 DREAMPlace
result can be regenerated with, for example,
`make ispd2015-dreamplace-output-mgc_fft_1`.

To generate the ISPD 2005 DREAMPlace placements while preparing the data,
supply an explicit DREAMPlace launcher and configuration directory; see
`./scripts/prepare_data.sh --help`. ISPD 2015 `.gp.def` placements are external
inputs: the repository does not include the DREAMPlace configuration needed to
recreate them, but the Makefile fully recreates their PLACEBIN and SVG outputs.

The ISPD 2015 designs are installed under `data/ispd2015` in their original
LEF, DEF, and Verilog form. `make outputs` parses their legalized DEF files and
writes all four views under `out/ispd2015`. It discovers matching
`data/ispd2015-dreamplace/<design>.gp.def` files and writes their four views
under `out/ispd2015-dreamplace/<design>`.

Useful maintenance targets are `make valgrind`, `make fuzz-run`, `make clean`,
and `make clean-outputs`.

## Project layout

```text
include/placement/   Public model and extension interfaces
src/parsing/         Bookshelf and LEF/DEF backends plus shared text utilities
src/serialization/   Binary serializer
src/rendering/       Placement and density SVG renderers
src/apps/            Thin command-line applications
test/                Component test suites and shared synthetic fixtures
fuzz/                libFuzzer component targets and corpus
scripts/             Benchmark download and optional DREAMPlace preparation
```
