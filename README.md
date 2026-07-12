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
  [--output-format svg] output.placebin output.svg
make test
make outputs
# Run up to four benchmark pipelines concurrently.
make -j 4 outputs
```

`make outputs` parses the legalized `*.dp.aux` manifest in each
`data/ispd2005` benchmark and creates `out/parsed/<design>.placebin` and
`out/svg/<design>.svg`. It uses one job by default; pass `-j N` to process up
to `N` benchmarks concurrently. Parsing and rendering remain ordered within
each benchmark. `make clean` removes compiled files;
`make clean-outputs` removes generated benchmark results.

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
drawn in this first renderer.
