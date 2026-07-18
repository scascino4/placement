# Repository guide

This is a dependency-free C++23 pipeline for physical-design placement data.
It parses Bookshelf row-based placement problems and LEF/DEF designs into the
format-neutral `placement::Board`, serializes boards in the `PLACEBIN` binary
format, and renders placement, utilization, pin-density, and cell-density SVGs.
Preserve the separation between input backends, the core model, serialization,
and rendering so new formats can be added without changing unrelated backends
or the command-line applications.

## Commands

- `make`: build `build/bin/placement_parse` and
  `build/bin/placement_render`.
- `make test`: build and run the standard-library-only unit tests.
- `make format`: format production and test C++ sources with `clang-format`.
- `make valgrind`: build debug-symbol variants in `build/valgrind` and run
  parsing and SVG rendering for `adaptec1` under Valgrind Memcheck.
- `make outputs`: parse all eight legalized `data/ispd2005/*/*.dp.aux`
  benchmarks, the movable-macro `data/ispd2005free/*/*_allfree.aux` variants,
  all 20 legalized `data/ispd2015/*/after_legalized.ntup.fix.def` designs with
  their technology and cell LEFs, and any available DreamPlace placements.
  It writes a binary and all four SVG views to matching directories under
  `out`.
- `./scripts/prepare_data.sh`: download the three supported benchmark families;
  with explicit launcher and configuration options, optionally run DREAMPlace
  for the ISPD 2005 families.
- `make clean`: remove compiled artifacts.
- `make clean-outputs`: remove generated benchmark outputs.

Before handing off changes, run at least `make` and `make test`. For changes to
parsing, serialization, or rendering behavior, also run `make outputs`, verify
the generated binaries can be read back, and qualitatively inspect
representative Bookshelf and LEF/DEF SVG previews for clipping, inversion, or
implausible geometry. For changes that affect memory ownership, allocation, or
large-data processing, also run the opt-in `make valgrind` smoke test when
Valgrind is available.

## Code organization and conventions

- `include/placement/model.hpp` defines the format-neutral `placement::Board`.
- `Parser`, `Renderer`, and `Serializer` are independent extension interfaces
  selected through factories and connected only through `placement::Board`.
- `src/parsing/bookshelf.cpp` and `src/parsing/lefdef.cpp` are the current input
  backends. Shared streaming text/token utilities belong in
  `src/parsing/common.hpp`; backend-specific state should stay in its backend.
- `src/rendering` and `src/serialization` contain the current SVG and binary
  backends; public interfaces mirror this structure under `include/placement`.
- `src/apps` contains the two `*_main.cpp` files, which should remain thin CLIs.
- Tests are split by component under `test/parsing`, `test/model`,
  `test/serialization`, and `test/rendering`; shared synthetic fixtures and
  assertions are in `test/support.cpp` and `test/support.hpp`.
- Keep production code and tests limited to the C++23 standard library.
- Follow `.clang-format`, retain path-and-line text-parser diagnostics, validate
  all declared counts and references, and use atomic temporary-file output.
- Keep backend-specific options typed: Bookshelf accepts an optional placement
  override, while LEF/DEF accepts one or more LEF paths and a DEF input. Do not
  leak either set of options into `placement::Board`.
- LEF/DEF geometry is normalized to microns. Placement blockages become gaps in
  row subrows; unsupported routing-oriented constructs must be skipped with
  grammar-aware parsing and must not be added to the core model solely to mirror
  an input format.
- Keep parsing streaming-friendly: `bigblue4` has over two million cells and
  nearly nine million pins. Avoid per-record structures with unnecessary
  overhead, retain flattened net-to-pin ranges, and avoid expanding SVG
  connectivity.

`data/`, `build/`, and `out/` are local/generated and ignored by Git. Do not
modify benchmark inputs or commit generated binaries/SVGs. The binary format is
documented in `README.md`.
