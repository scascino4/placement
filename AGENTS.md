# Repository guide

This is a dependency-free C++23 pipeline for physical-design placement data.
It currently parses Bookshelf row-based placement problems into a versioned,
format-neutral binary model and renders that binary model as SVG. Preserve the
format-neutral architecture so future LEF/DEF parsers and non-SVG renderers can
be added without changing the core model or command-line applications.

## Commands

- `make`: build `build/bin/placement_parse` and
  `build/bin/placement_render`.
- `make test`: build and run the standard-library-only unit tests.
- `make valgrind`: build debug-symbol variants in `build/valgrind` and run
  parsing and SVG rendering for `adaptec1` under Valgrind Memcheck.
- `make outputs`: parse all eight legalized `data/ispd2005/*/*.dp.aux`
  benchmarks, the movable-macro `data/ispd2005free/*/*_allfree.aux` variants,
  and their available DreamPlace placements into matching directories under
  `out`.
- `make clean`: remove compiled artifacts.
- `make clean-outputs`: remove generated benchmark outputs.

Before handing off changes, run at least `make` and `make test`. For changes to
parsing, serialization, or rendering behavior, also run `make outputs`, verify
all eight binaries can be read back, and qualitatively inspect rasterized SVG
previews for clipping, inversion, or implausible geometry. For changes that
affect memory ownership, allocation, or large-data processing, also run the
opt-in `make valgrind` smoke test when Valgrind is available.

## Code organization and conventions

- `include/placement/model.hpp` defines the format-neutral `placement::Board`.
- `Parser`, `Renderer`, and `Serializer` are independent extension interfaces
  selected through factories and connected only through `placement::Board`.
- `src/parsing`, `src/rendering`, and `src/serialization` contain the current
  backends; their public interfaces mirror this structure under
  `include/placement`.
- `src/apps` contains the two `*_main.cpp` files, which should remain thin CLIs.
- Tests and synthetic Bookshelf fixtures are in `test/test_main.cpp`.
- Keep production code and tests limited to the C++23 standard library.
- Follow `.clang-format`, retain path-and-line parser diagnostics, validate all
  declared counts and references, and use atomic temporary-file output.
- Keep parsing streaming-friendly: `bigblue4` has over two million cells and
  nearly nine million pins. Avoid per-record structures with unnecessary
  overhead and avoid expanding SVG connectivity.

`data/`, `build/`, and `out/` are local/generated and ignored by Git. Do not
modify benchmark inputs or commit generated binaries/SVGs. The binary format is
documented in `README.md`; incompatible changes require a new major version,
while backward-compatible additions require a minor-version update.
