#include "placement/error.hpp"
#include "placement/parsing/parser.hpp"
#include "placement/rendering/renderer.hpp"
#include "placement/rendering/style.hpp"
#include "placement/serialization/serializer.hpp"

#include <chrono>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <functional>
#include <initializer_list>
#include <iostream>
#include <iterator>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace {

class TemporaryDirectory {
public:
  TemporaryDirectory() {
    const auto id = std::chrono::steady_clock::now().time_since_epoch().count();
    path_ = std::filesystem::temp_directory_path() / ("placement-tests-" + std::to_string(id));
    std::filesystem::create_directories(path_);
  }
  ~TemporaryDirectory() {
    std::error_code ignored;
    std::filesystem::remove_all(path_, ignored);
  }
  [[nodiscard]] const std::filesystem::path &path() const { return path_; }

private:
  std::filesystem::path path_;
};

void write(const std::filesystem::path &path, std::string_view contents) {
  std::ofstream output(path, std::ios::binary);
  output << contents;
  if (!output)
    throw std::runtime_error("test could not write " + path.string());
}

[[nodiscard]] std::string read(const std::filesystem::path &path) {
  std::ifstream input(path, std::ios::binary);
  return {std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
}

[[nodiscard]] bool contains_parts(std::string_view contents, std::initializer_list<std::string_view> parts) {
  std::string expected;
  for (const auto part : parts)
    expected += part;
  return contents.find(expected) != std::string_view::npos;
}

void fixture(const std::filesystem::path &directory) {
  write(directory / "tiny.aux", "# arbitrary component order\n"
                                "RowBasedPlacement : tiny.pl tiny.scl tiny.wts tiny.nets tiny.nodes\n");
  write(directory / "tiny.nodes", "UCLA nodes 1.0\n"
                                  "# cells\n"
                                  "NumNodes : 4\nNumTerminals : 2\n"
                                  "a 2 4\nb 6 3 terminal\nc 5 2 terminal_NI\nd 1.5 2.5e0\n");
  write(directory / "tiny.nets", "UCLA nets 1.0\nNumNets : 2\nNumPins : 4\n"
                                 "NetDegree : 3 net0\n"
                                 "a I : -0.5 1\nb O : 2 0\nc B : 0 0\n"
                                 "NetDegree : 1 net1\nd U\n");
  write(directory / "tiny.wts", "UCLA wts 1.0\na 1 2\nb 3 4\nnet0 2.5\nnet1 1.5\n");
  write(directory / "tiny.scl", "UCLA scl 1.0\nNumRows : 1\nCoreRow Horizontal\n"
                                " Coordinate : 10\n Height : 4\n Sitespacing : 2\n"
                                " Siteorient : FS\n Sitesymmetry : X Y ROT90\n"
                                " SubrowOrigin : 5 NumSites : 3\n"
                                " SubrowOrigin : 20 NumSites : 2\nEnd\n");
  write(directory / "tiny.pl", "UCLA pl 1.0\n"
                               "a 10.5 20 : E\n"
                               "b 30 10 : N /FIXED\n"
                               "c 40 10 : FW /FIXED_NI DIMS=(7,8)\n");
}

void check(bool condition, std::string_view message) {
  if (!condition)
    throw std::runtime_error(std::string(message));
}

[[nodiscard]] bool close(double a, double b) { return std::abs(a - b) < 1e-12; }

template <typename Function> void expect_error(Function &&function, std::string_view fragment) {
  try {
    function();
  } catch (const placement::Error &error) {
    check(std::string_view(error.what()).find(fragment) != std::string_view::npos, "error did not contain expected diagnostic");
    return;
  }
  throw std::runtime_error("expected placement::Error");
}

[[nodiscard]] placement::Board parse_fixture(const std::filesystem::path &directory) {
  auto parser = placement::make_parser("BOOKSHELF");
  return parser->parse(directory / "tiny.aux");
}

void parser_test() {
  TemporaryDirectory temporary;
  fixture(temporary.path());
  const auto board = parse_fixture(temporary.path());
  check(board.name == "tiny", "design name");
  check(board.cells.size() == 4 && board.rows.size() == 1, "cell and row counts");
  check(board.nets.size() == 2 && board.pins.size() == 4, "net and pin counts");
  check(board.cells[1].kind == placement::CellKind::Terminal, "terminal kind");
  check(board.cells[1].macro, "terminal macro identity");
  check(board.cells[2].kind == placement::CellKind::TerminalNonInteracting, "terminal_NI kind");
  check(!board.cells[2].macro, "non-interacting terminal is not a macro");
  check(!board.cells[3].location, "undefined placement");
  check(board.cells[3].width == 1.5 && board.cells[3].height == 2.5, "decimal and scientific geometry");
  check(board.cells[0].location->orientation == placement::Orientation::E, "orientation");
  check(board.cells[1].location->status == placement::PlacementStatus::Fixed, "fixed status");
  check(board.cells[2].location->status == placement::PlacementStatus::FixedNonInteracting, "fixed_NI status");
  check(*board.cells[2].location->width == 7 && *board.cells[2].location->height == 8, "DIMS");
  check(board.cells[0].weights == std::vector<double>({1, 2}), "node weights");
  check(board.nets[0].weights == std::vector<double>({2.5}), "net weights");
  check(board.pins[2].direction == placement::PinDirection::Bidirectional, "bidirectional pin");
  check(board.rows[0].subrows.size() == 2 && board.rows[0].site_width == 2, "multiple subrows and default site width");
  check(board.rows[0].symmetry == 7, "row symmetry");
}

void malformed_parser_test() {
  TemporaryDirectory temporary;
  fixture(temporary.path());
  write(temporary.path() / "tiny.nets", "UCLA nets 1.0\nNumNets : 1\nNumPins : 1\n"
                                        "NetDegree : 1 broken\nmissing I : 0 0\n");
  expect_error([&] { (void)parse_fixture(temporary.path()); }, "pin references unknown cell");

  fixture(temporary.path());
  write(temporary.path() / "tiny.nodes", "UCLA nodes 1.0\nNumNodes : 2\nNumTerminals : 0\n"
                                         "duplicate 1 1\nduplicate 2 2\n");
  expect_error([&] { (void)parse_fixture(temporary.path()); }, "duplicate cell name 'duplicate'");

  fixture(temporary.path());
  write(temporary.path() / "tiny.nets", "UCLA nets 1.0\nNumNets : 2\nNumPins : 0\n"
                                        "NetDegree : 0 duplicate\nNetDegree : 0 duplicate\n");
  expect_error([&] { (void)parse_fixture(temporary.path()); }, "duplicate net name 'duplicate'");
}

void placement_override_test() {
  TemporaryDirectory temporary;
  fixture(temporary.path());
  const auto override = temporary.path() / "dreamplace.pl";
  write(override, "UCLA pl 1.0\n"
                  "a 101 202 : S\n"
                  "b 303 404 : N\n");

  // A malformed manifest placement proves that the override is selected as
  // the placement source instead of being layered on top of it.
  write(temporary.path() / "tiny.pl", "this file must not be parsed\n");
  auto parser = placement::make_parser("bookshelf", {.placement_override = override});
  const auto board = parser->parse(temporary.path() / "tiny.aux");
  check(board.cells[0].location->x == 101 && board.cells[0].location->y == 202, "placement override coordinates");
  check(board.cells[0].location->orientation == placement::Orientation::S, "placement override orientation");
  check(board.cells[1].location->status == placement::PlacementStatus::Movable && board.cells[1].macro,
        "placement override preserves movable macro identity");
  check(!board.cells[2].location && !board.cells[3].location, "placement override may leave cells unplaced");

  write(override, "UCLA pl 1.0\nunknown 1 2 : N\n");
  expect_error([&] { (void)parser->parse(temporary.path() / "tiny.aux"); }, override.string() + ":2: placement references unknown cell");
}

void binary_test() {
  TemporaryDirectory temporary;
  fixture(temporary.path());

  const auto board = parse_fixture(temporary.path());
  const auto serializer = placement::make_serializer("BINARY");
  const auto first = temporary.path() / "first.placebin";
  const auto second = temporary.path() / "second.placebin";
  serializer->write(board, first);
  serializer->write(board, second);
  check(read(first) == read(second), "binary output must be deterministic");
  const auto decoded = serializer->read(first);
  check(decoded.name == board.name && decoded.cells.size() == board.cells.size(), "binary board identity");
  check(decoded.cells[1].macro && !decoded.cells[2].macro, "binary macro identity");
  check(decoded.cells[2].location->orientation == placement::Orientation::FW, "binary orientation");
  check(decoded.pins[0].offset_x == -0.5 && decoded.nets[0].pin_count == 3, "binary connectivity");

  auto bytes = read(first);
  bytes[0] = 'X';
  write(temporary.path() / "bad-magic.placebin", bytes);
  expect_error([&] { (void)serializer->read(temporary.path() / "bad-magic.placebin"); }, "invalid binary magic");

  bytes = read(first);
  bytes.resize(bytes.size() - 3);
  write(temporary.path() / "truncated.placebin", bytes);
  expect_error([&] { (void)serializer->read(temporary.path() / "truncated.placebin"); }, "truncated binary placement");

  bytes = read(first);
  bytes.push_back('x');
  write(temporary.path() / "trailing.placebin", bytes);
  expect_error([&] { (void)serializer->read(temporary.path() / "trailing.placebin"); }, "trailing binary data");

  bytes = read(first);
  // Magic (8), name length (4), and "tiny" (4) precede the cell count.
  for (std::size_t index = 16; index < 24; ++index)
    bytes[index] = '\xFF';
  write(temporary.path() / "bad-count.placebin", bytes);
  expect_error([&] { (void)serializer->read(temporary.path() / "bad-count.placebin"); }, "invalid cell count");

  placement::Board invalid_range;
  invalid_range.name = "invalid";
  invalid_range.nets.push_back({"net", 1, 1, {}});
  serializer->write(invalid_range, temporary.path() / "bad-range.placebin");
  expect_error([&] { (void)serializer->read(temporary.path() / "bad-range.placebin"); }, "net pin range is out of bounds");

  expect_error([&] { (void)placement::make_serializer("unknown"); }, "unsupported serialization format");
}

void utilization_test() {
  placement::Board board;
  placement::Row row;
  row.coordinate = 0;
  row.height = 10;
  row.site_spacing = 1;
  row.subrows.push_back({0, 20});
  board.rows.push_back(row);

  placement::Cell movable;
  movable.name = "movable";
  movable.width = 10;
  movable.height = 5;
  movable.location.emplace();
  movable.location->x = 5;
  board.cells.push_back(movable);

  placement::Cell macro;
  macro.name = "macro";
  macro.width = 2;
  macro.height = 10;
  macro.macro = true;
  macro.location.emplace();
  macro.location->status = placement::PlacementStatus::Movable;
  board.cells.push_back(macro);

  const auto grid = board.utilization(10);
  check(grid.columns == 2 && grid.rows == 1 && grid.bins.size() == 2, "utilization grid dimensions");
  check(close(grid.at(0, 0).movable_area, 45) && close(grid.at(1, 0).movable_area, 25), "movable cells and macros contribute utilization");
  check(close(grid.at(0, 0).placeable_area, 100) && close(grid.at(1, 0).placeable_area, 100), "movable macros do not reduce placeable area");
  check(close(*grid.at(0, 0).utilization(), 0.45) && close(*grid.at(1, 0).utilization(), 0.25), "utilization ratios");

  placement::Board fragmented;
  row.subrows = {{0, 4}, {6, 4}};
  fragmented.rows.push_back(row);
  macro.width = 4;
  macro.location->x = 3;
  macro.location->status = placement::PlacementStatus::Fixed;
  fragmented.cells.push_back(macro);
  const auto fragmented_grid = fragmented.utilization(10);
  check(close(fragmented_grid.at(0, 0).placeable_area, 60), "fixed blockage excludes only its intersection with legal rows");

  expect_error([&] { (void)board.utilization(0); }, "finite and positive");
  expect_error([&] { (void)grid.at(2, 0); }, "out of bounds");
}

void pin_density_test() {
  placement::Board board;
  placement::Row row;
  row.coordinate = 0;
  row.height = 20;
  row.site_spacing = 1;
  row.subrows.push_back({0, 40});
  board.rows.push_back(row);

  placement::Cell north;
  north.width = 4;
  north.height = 2;
  north.location.emplace();
  board.cells.push_back(north);

  placement::Cell east = north;
  east.location->x = 20;
  east.location->orientation = placement::Orientation::E;
  board.cells.push_back(east);

  board.pins.push_back({0, placement::PinDirection::Input, 1, 0});
  board.pins.push_back({1, placement::PinDirection::Output, 0, 6});
  placement::Cell unplaced;
  board.cells.push_back(unplaced);
  board.pins.push_back({2, placement::PinDirection::Unknown, 0, 0});

  const auto grid = board.pin_density(10);
  check(grid.columns == 4 && grid.rows == 2 && grid.bins.size() == 8, "pin density grid dimensions");
  check(grid.at(0, 0).pin_count == 1 && grid.at(2, 0).pin_count == 1, "oriented pins are assigned to bins");
  check(close(grid.at(0, 0).density(), 0.01), "pin density uses clipped bin area");
  expect_error([&] { (void)board.pin_density(0); }, "finite and positive");
  expect_error([&] { (void)grid.at(4, 0); }, "out of bounds");

  board.pins.push_back({99, placement::PinDirection::Unknown, 0, 0});
  expect_error([&] { (void)board.pin_density(10); }, "invalid cell reference");
}

void cell_density_test() {
  placement::Board board;
  placement::Row row;
  row.coordinate = 0;
  row.height = 10;
  row.site_spacing = 1;
  row.subrows.push_back({0, 25});
  board.rows.push_back(row);

  placement::Cell movable;
  movable.width = 10;
  movable.height = 10;
  movable.location.emplace();
  movable.location->x = 15;
  board.cells.push_back(movable);

  placement::Cell macro = movable;
  macro.macro = true;
  macro.location->x = 0;
  macro.location->status = placement::PlacementStatus::Fixed;
  board.cells.push_back(macro);

  placement::Cell non_interacting = movable;
  non_interacting.kind = placement::CellKind::TerminalNonInteracting;
  non_interacting.location->x = 5;
  non_interacting.location->status = placement::PlacementStatus::FixedNonInteracting;
  board.cells.push_back(non_interacting);

  placement::Cell unplaced = movable;
  unplaced.location.reset();
  board.cells.push_back(unplaced);

  const auto grid = board.cell_density(10);
  check(grid.columns == 3 && grid.rows == 1 && grid.bins.size() == 3, "cell density grid dimensions");
  check(!grid.at(0, 0).density() && close(grid.at(0, 0).movable_area, 0) && close(grid.at(0, 0).available_area, 0),
        "fixed macros consume capacity without contributing density");
  check(close(grid.at(1, 0).movable_area, 50) && close(*grid.at(1, 0).density(), 0.5), "cell density splits movable cells at bin boundaries");
  check(close(grid.at(2, 0).available_area, 50) && close(*grid.at(2, 0).density(), 1),
        "cell density clips edge bins and excludes non-interacting cells");

  board.cells[1].location->status = placement::PlacementStatus::Movable;
  const auto movable_macro_grid = board.cell_density(10);
  check(close(*movable_macro_grid.at(0, 0).density(), 1) && close(movable_macro_grid.at(0, 0).movable_area, 100) &&
            close(movable_macro_grid.at(0, 0).available_area, 100),
        "movable macros contribute cell density");
  expect_error([&] { (void)board.cell_density(0); }, "finite and positive");
  expect_error([&] { (void)grid.at(3, 0); }, "out of bounds");
}

void svg_test() {
  const auto &default_style = placement::rendering_style::default_palette;
  const auto &dark_mode_style = placement::rendering_style::dark_mode_palette;
  check(&placement::rendering_style::palette(false) == &default_style && &placement::rendering_style::palette(true) == &dark_mode_style,
        "rendering mode selects the common palette");

  TemporaryDirectory temporary;
  fixture(temporary.path());
  auto board = parse_fixture(temporary.path());
  const auto serializer = placement::make_serializer("binary");
  const auto binary = temporary.path() / "tiny.placebin";
  serializer->write(board, binary);
  board = serializer->read(binary);
  board.name = "tiny <&>";
  board.cells[1].location->status = placement::PlacementStatus::Movable;
  auto renderer = placement::make_renderer("SVG");
  const auto svg = temporary.path() / "tiny.svg";
  renderer->render(board, svg);
  const auto contents = read(svg);
  check(contents.find("tiny &lt;&amp;&gt; placement") != std::string::npos, "escaped SVG title");
  check(contents.find("translate(") != std::string::npos && contents.find("scale(1 -1)") != std::string::npos, "placement coordinate transform");
  check(contents.find("class=\"movable\"") != std::string::npos && contents.find("class=\"macro\"") != std::string::npos &&
            contents.find("class=\"fixed-ni\"") != std::string::npos,
        "SVG cell classes");
  check(contains_parts(contents, {".background{fill:", default_style.background, "}"}), "default placement SVG background is charcoal");
  check(contains_parts(contents, {".macro{fill:", default_style.macro_fill, ";stroke:", default_style.macro_stroke}),
        "light placement SVG macros have contrasting outlines");
  check(contents.find("M10.5 20h4v2h-4z") != std::string::npos, "rotated cell dimensions");

  auto dark_renderer = placement::make_renderer("svg", {.bin_size = std::nullopt, .dark_mode = true});
  const auto dark_svg = temporary.path() / "tiny-dark.svg";
  dark_renderer->render(board, dark_svg);
  const auto dark_contents = read(dark_svg);
  check(contains_parts(dark_contents, {".background{fill:", dark_mode_style.background, "}"}) &&
            contains_parts(dark_contents, {".movable{fill:", dark_mode_style.movable_fill}) &&
            contains_parts(dark_contents, {".macro{fill:", dark_mode_style.macro_fill, ";stroke:", dark_mode_style.macro_stroke}),
        "dark placement SVG palette");
  check(contents.find(dark_mode_style.surface) == std::string::npos, "light placement SVG remains the default");

  auto utilization_renderer = placement::make_renderer("utilization-svg", {.bin_size = 5.0});
  const auto utilization_svg = temporary.path() / "utilization.svg";
  utilization_renderer->render(board, utilization_svg);
  const auto utilization_contents = read(utilization_svg);
  check(utilization_contents.find("tiny &lt;&amp;&gt; utilization") != std::string::npos, "utilization SVG title");
  check(contains_parts(utilization_contents, {".background{fill:", default_style.background, "}"}), "utilization SVG background is charcoal");
  check(utilization_contents.find("class=\"bin\"") != std::string::npos && utilization_contents.find("macro-overlay") != std::string::npos,
        "utilization SVG bins and macros");
  check(contains_parts(utilization_contents, {".macro-overlay{fill:", default_style.surface}) &&
            contains_parts(utilization_contents, {".fixed-ni-overlay{fill:", default_style.surface}),
        "utilization SVG macros mask bin colors");

  auto dark_utilization_renderer = placement::make_renderer("utilization-svg", {.bin_size = 5.0, .dark_mode = true});
  const auto dark_utilization_svg = temporary.path() / "utilization-dark.svg";
  dark_utilization_renderer->render(board, dark_utilization_svg);
  const auto dark_utilization_contents = read(dark_utilization_svg);
  const auto dark_green = std::string("hsl(120 ") + std::to_string(dark_mode_style.heatmap_saturation_percent) + "% " +
                          std::to_string(dark_mode_style.heatmap_lightness_percent) + "%)";
  check(contains_parts(dark_utilization_contents, {".background{fill:", dark_mode_style.background, "}"}) &&
            dark_utilization_contents.find(dark_green) != std::string::npos,
        "dark utilization SVG palette");

  auto pin_density_renderer = placement::make_renderer("pin-density-svg", {.bin_size = 5.0});
  const auto pin_density_svg = temporary.path() / "pin-density.svg";
  pin_density_renderer->render(board, pin_density_svg);
  const auto pin_density_contents = read(pin_density_svg);
  check(pin_density_contents.find("tiny &lt;&amp;&gt; pin density") != std::string::npos, "pin density SVG title");
  check(contains_parts(pin_density_contents, {".background{fill:", default_style.background, "}"}), "pin density SVG background is charcoal");
  check(pin_density_contents.find("class=\"bin\"") != std::string::npos && pin_density_contents.find("pins; density") != std::string::npos,
        "pin density SVG bins and tooltips");
  check(contains_parts(pin_density_contents, {".movable-overlay{fill:", default_style.surface, ";fill-opacity:.42"}) &&
            contains_parts(pin_density_contents, {".macro-overlay{fill:", default_style.surface}) &&
            contains_parts(pin_density_contents, {".fixed-ni-overlay{fill:", default_style.surface}),
        "pin density SVG masks macros consistently with utilization");

  auto dark_pin_density_renderer = placement::make_renderer("pin-density-svg", {.bin_size = 5.0, .dark_mode = true});
  const auto dark_pin_density_svg = temporary.path() / "pin-density-dark.svg";
  dark_pin_density_renderer->render(board, dark_pin_density_svg);
  const auto dark_pin_density_contents = read(dark_pin_density_svg);
  check(contains_parts(dark_pin_density_contents, {".background{fill:", dark_mode_style.background, "}"}) &&
            contains_parts(dark_pin_density_contents, {".movable-overlay{fill:", dark_mode_style.surface, ";fill-opacity:.42"}) &&
            contains_parts(dark_pin_density_contents, {"stroke:", dark_mode_style.overlay_stroke}),
        "dark pin density SVG palette");

  auto cell_density_renderer = placement::make_renderer("cell-density-svg", {.bin_size = 5.0});
  const auto cell_density_svg = temporary.path() / "cell-density.svg";
  cell_density_renderer->render(board, cell_density_svg);
  const auto cell_density_contents = read(cell_density_svg);
  check(cell_density_contents.find("tiny &lt;&amp;&gt; cell density") != std::string::npos, "cell density SVG title");
  check(cell_density_contents.find("class=\"bin\"") != std::string::npos && cell_density_contents.find("movable-object area;") != std::string::npos &&
            cell_density_contents.find("available area; density") != std::string::npos,
        "cell density SVG bins and tooltips");
  check(contains_parts(cell_density_contents, {".macro-overlay{fill:", default_style.surface}) &&
            cell_density_contents.find("class=\"macro-overlay\"") != std::string::npos &&
            contains_parts(cell_density_contents, {".fixed-ni-overlay{fill:", default_style.surface}),
        "cell density SVG masks macros and non-interacting objects");

  auto dark_cell_density_renderer = placement::make_renderer("cell-density-svg", {.bin_size = 5.0, .dark_mode = true});
  const auto dark_cell_density_svg = temporary.path() / "cell-density-dark.svg";
  dark_cell_density_renderer->render(board, dark_cell_density_svg);
  const auto dark_cell_density_contents = read(dark_cell_density_svg);
  check(contains_parts(dark_cell_density_contents, {".background{fill:", dark_mode_style.background, "}"}) &&
            contains_parts(dark_cell_density_contents, {".macro-overlay{fill:", dark_mode_style.surface}) &&
            contains_parts(dark_cell_density_contents, {".fixed-ni-overlay{fill:", dark_mode_style.surface}) &&
            contains_parts(dark_cell_density_contents, {"stroke:", dark_mode_style.overlay_stroke}),
        "dark cell density SVG palette");

  placement::Board empty;
  expect_error([&] { renderer->render(empty, temporary.path() / "empty.svg"); }, "without geometry");
  check(!std::filesystem::exists(temporary.path() / "empty.svg"), "failed render must not leave output");
}

} // namespace

int main() {
  const std::vector<std::pair<std::string_view, std::function<void()>>> tests{
      {"Bookshelf parser", parser_test},
      {"parser diagnostics", malformed_parser_test},
      {"binary round trip and corruption", binary_test},
      {"placement override", placement_override_test},
      {"utilization grid", utilization_test},
      {"pin density grid", pin_density_test},
      {"cell density grid", cell_density_test},
      {"SVG renderer", svg_test},
  };
  std::size_t passed = 0;
  for (const auto &[name, test] : tests) {
    try {
      test();
      ++passed;
      std::cout << "[PASS] " << name << '\n';
    } catch (const std::exception &error) {
      std::cerr << "[FAIL] " << name << ": " << error.what() << '\n';
    }
  }
  std::cout << passed << '/' << tests.size() << " tests passed\n";
  return passed == tests.size() ? 0 : 1;
}
