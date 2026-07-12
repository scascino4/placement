# Placement parser and renderer

This project converts Bookshelf row-based placement problems into a compact,
format-neutral binary representation and renders that representation as SVG.
It uses C++23 and the standard library only.

## Build and run

```sh
make
build/bin/placement_parse [--input-format bookshelf] input.dp.aux output.placebin
build/bin/placement_render [--output-format svg] output.placebin output.svg
make test
make outputs
```

`make outputs` parses the legalized `*.dp.aux` manifest in each
`data/ispd2005` benchmark and creates `out/parsed/<design>.placebin` and
`out/svg/<design>.svg`. `make clean` removes compiled files;
`make clean-outputs` removes generated benchmark results.

## Architecture

`placement::Board` is independent of any input or output syntax. It stores
cells and placements, rows and subrows, nets and flattened pins, directions,
offsets, orientations, fixed status, and weights. `Parser` and `Renderer` are
backend interfaces selected through factories. The current implementations are
Bookshelf input and SVG output; LEF/DEF parsing or another renderer can be
added without changing the applications or binary codec.

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
