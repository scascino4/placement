# Placement parser and renderer

This project converts Bookshelf row-based placement problems into a compact,
format-neutral binary representation and renders that representation as SVG.
It uses C++23 and the standard library only.

## Build and run

```sh
make
build/bin/placement_parse [--input-format bookshelf] \
  [--serialization-format binary] input.dp.aux output.placebin
build/bin/placement_render [--serialization-format binary] \
  [--output-format svg|utilization-svg|pin-density-svg] [--bin-size size] [--dark-mode] output.placebin output.svg
make test
make valgrind
make outputs
# Run up to four benchmark pipelines concurrently.
make -j 4 outputs
```

`make outputs` parses the legalized `*.dp.aux` manifest in each
`data/ispd2005` benchmark and creates `placement.placebin`, `placement.svg`, and
`utilization.svg` and `pin-density.svg` under `out/ispd2005/<design>`. It uses one job by default;
pass `-j N` to process up to `N` benchmarks concurrently. Parsing and rendering
remain ordered within each benchmark. `make clean` removes compiled files;
`make clean-outputs` removes generated benchmark results.

`make valgrind` builds debug-symbol variants of both applications in
`build/valgrind`, then checks parsing, placement, utilization, and pin-density
SVG rendering with Valgrind Memcheck using `adaptec1`, the smallest ISPD
benchmark. It is an opt-in smoke test because it requires Valgrind and is
substantially slower than `make test`.
Temporary placement and SVG outputs are removed automatically. Set
`VALGRIND_FLAGS` to replace the default Memcheck arguments or
`VALGRIND_CXXFLAGS` to replace the debug build flags.

## Architecture

`placement::Board` is independent of any input or output syntax. It stores
cells and placements, rows and subrows, nets and flattened pins, directions,
offsets, orientations, fixed status, and weights. `Parser` and `Renderer` are
backend interfaces selected through factories. `Serializer` independently
maps a `Board` to and from a persistent representation. The current backends
are Bookshelf input, binary serialization, and SVG output. Each application
links only the backends it uses, and components communicate exclusively through
`Board`.

The source tree follows those architectural boundaries:

- `include/placement/parsing` and `src/parsing` contain parser interfaces and
  input-format backends.
- `include/placement/rendering` and `src/rendering` contain renderer interfaces
  and output-format backends.
- `include/placement/serialization` defines the format-neutral serialization
  interface, while `src/serialization` contains persistence backends.
- `src/apps` contains the thin command-line entry points.

The Bookshelf reader consumes `.aux`, `.nodes`, `.nets`, optional `.wts`,
`.scl`, and `.pl` components. It supports terminal variants, the eight spatial
orientations, fixed variants, multiple subrows, optional placement dimensions,
and I/O/bidirectional pins. Diagnostics identify the component and source line.

## Binary format version 1.0

All integers are fixed-width little-endian values and all real values are
IEEE-754 binary64. The file contains:

1. `PLACEBIN` magic, 16-bit major/minor version, and 32-bit flags.
2. A length-prefixed design name and 64-bit cell, row, net, and pin counts.
3. Cell records: name, dimensions, kind, optional placement, and weights.
4. Row records: geometry, orientation/symmetry, and subrows.
5. Net records: name, flattened pin range, and weights.
6. Pin records: cell index, direction, and two offsets.

Strings and weight vectors have 32-bit lengths. Readers reject unknown major
versions, flags, enum values, invalid references/ranges, excessive counts,
truncation, and trailing bytes. Writers use a temporary file and rename it only
after a successful write.

## SVG output

SVGs show row regions, movable cells, fixed terminals/macros, and
non-interacting fixed objects with separate styles. The writer preserves the
placement convention that Y increases upward and swaps dimensions for rotated
orientations. Cells are grouped into paths to keep multi-million-cell outputs
manageable. Connectivity is retained in the binary but is intentionally not
drawn in this first renderer. Light colors are used by default; pass
`--dark-mode` to any SVG output format for a dark background and matching
high-contrast colors.

The `utilization-svg` renderer divides the placement region into square bins
and colors them from green (low utilization) through yellow to red (100% or
greater). Utilization is movable-cell overlap divided by legal row area after
subtracting fixed-object overlap. Fixed objects remain visible in dark gray so
macro outlines can be interpreted directly, while white interiors mask the bin
colors over non-placeable macro footprints. With no `--bin-size`, the bin size
is 1% of the placement region's longest dimension; an explicit positive size
overrides that default.

The `pin-density-svg` renderer assigns each placed pin to a square bin using
the cell orientation and the pin's center-relative offset. Bin values are pins
per square placement unit. Like utilization, colors run from green through
yellow to red; pin-density colors saturate at the design's 95th percentile so
isolated hotspots do not flatten the rest of the heatmap. Bins without legal
placement area are gray. Exact pin counts and densities are embedded in SVG
tooltips.
