#include "placement/error.hpp"
#include "placement/parsing/parser.hpp"
#include "placement/rendering/renderer.hpp"
#include "placement/serialization/serializer.hpp"

#include <chrono>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <functional>
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

void fixture(const std::filesystem::path &directory) {
  write(directory / "tiny.aux",
        "# arbitrary component order\n"
        "RowBasedPlacement : tiny.pl tiny.scl tiny.wts tiny.nets tiny.nodes\n");
  write(directory / "tiny.nodes", "UCLA nodes 1.0\n"
                                  "# cells\n"
                                  "NumNodes : 4\nNumTerminals : 2\n"
                                  "a 2 4\nb 6 3 terminal\nc 5 2 terminal_NI\nd 1.5 2.5\n");
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
    check(std::string_view(error.what()).find(fragment) != std::string_view::npos,
          "error did not contain expected diagnostic");
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
  check(board.cells[2].kind == placement::CellKind::TerminalNonInteracting, "terminal_NI kind");
  check(!board.cells[3].location, "undefined placement");
  check(board.cells[0].location->orientation == placement::Orientation::E, "orientation");
  check(board.cells[1].location->status == placement::PlacementStatus::Fixed, "fixed status");
  check(board.cells[2].location->status == placement::PlacementStatus::FixedNonInteracting,
        "fixed_NI status");
  check(*board.cells[2].location->width == 7 && *board.cells[2].location->height == 8, "DIMS");
  check(board.cells[0].weights == std::vector<double>({1, 2}), "node weights");
  check(board.nets[0].weights == std::vector<double>({2.5}), "net weights");
  check(board.pins[2].direction == placement::PinDirection::Bidirectional, "bidirectional pin");
  check(board.rows[0].subrows.size() == 2 && board.rows[0].site_width == 2,
        "multiple subrows and default site width");
  check(board.rows[0].symmetry == 7, "row symmetry");
}

void malformed_parser_test() {
  TemporaryDirectory temporary;
  fixture(temporary.path());
  write(temporary.path() / "tiny.nets", "UCLA nets 1.0\nNumNets : 1\nNumPins : 1\n"
                                        "NetDegree : 1 broken\nmissing I : 0 0\n");
  expect_error([&] { (void)parse_fixture(temporary.path()); }, "pin references unknown cell");
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
  check(decoded.name == board.name && decoded.cells.size() == board.cells.size(),
        "binary board identity");
  check(decoded.cells[2].location->orientation == placement::Orientation::FW, "binary orientation");
  check(decoded.pins[0].offset_x == -0.5 && decoded.nets[0].pin_count == 3, "binary connectivity");

  auto bytes = read(first);
  bytes[0] = 'X';
  write(temporary.path() / "bad-magic.placebin", bytes);
  expect_error([&] { (void)serializer->read(temporary.path() / "bad-magic.placebin"); },
               "invalid binary magic");

  bytes = read(first);
  bytes[8] = 2;
  bytes[9] = 0;
  write(temporary.path() / "bad-version.placebin", bytes);
  expect_error([&] { (void)serializer->read(temporary.path() / "bad-version.placebin"); },
               "unsupported binary major version");

  bytes = read(first);
  bytes.resize(bytes.size() - 3);
  write(temporary.path() / "truncated.placebin", bytes);
  expect_error([&] { (void)serializer->read(temporary.path() / "truncated.placebin"); },
               "truncated binary placement");

  bytes = read(first);
  bytes.push_back('x');
  write(temporary.path() / "trailing.placebin", bytes);
  expect_error([&] { (void)serializer->read(temporary.path() / "trailing.placebin"); },
               "trailing binary data");

  bytes = read(first);
  // Header (16), name length (4), and "tiny" (4) precede the cell count.
  for (std::size_t index = 24; index < 32; ++index)
    bytes[index] = '\xFF';
  write(temporary.path() / "bad-count.placebin", bytes);
  expect_error([&] { (void)serializer->read(temporary.path() / "bad-count.placebin"); },
               "invalid cell count");

  placement::Board invalid_range;
  invalid_range.name = "invalid";
  invalid_range.nets.push_back({"net", 1, 1, {}});
  serializer->write(invalid_range, temporary.path() / "bad-range.placebin");
  expect_error([&] { (void)serializer->read(temporary.path() / "bad-range.placebin"); },
               "net pin range is out of bounds");

  expect_error([&] { (void)placement::make_serializer("unknown"); },
               "unsupported serialization format");
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

  placement::Cell fixed;
  fixed.name = "macro";
  fixed.width = 2;
  fixed.height = 10;
  fixed.kind = placement::CellKind::Terminal;
  fixed.location.emplace();
  fixed.location->status = placement::PlacementStatus::Fixed;
  board.cells.push_back(fixed);

  const auto grid = board.utilization(10);
  check(grid.columns == 2 && grid.rows == 1 && grid.bins.size() == 2,
        "utilization grid dimensions");
  check(close(grid.at(0, 0).movable_area, 25) && close(grid.at(1, 0).movable_area, 25),
        "movable area is split at bin boundaries");
  check(close(grid.at(0, 0).placeable_area, 80) && close(grid.at(1, 0).placeable_area, 100),
        "fixed geometry is excluded from placeable area");
  check(close(*grid.at(0, 0).utilization(), 0.3125) && close(*grid.at(1, 0).utilization(), 0.25),
        "utilization ratios");

  placement::Board fragmented;
  row.subrows = {{0, 4}, {6, 4}};
  fragmented.rows.push_back(row);
  fixed.width = 4;
  fixed.location->x = 3;
  fragmented.cells.push_back(fixed);
  const auto fragmented_grid = fragmented.utilization(10);
  check(close(fragmented_grid.at(0, 0).placeable_area, 60),
        "fixed blockage excludes only its intersection with legal rows");

  expect_error([&] { (void)board.utilization(0); }, "finite and positive");
  expect_error([&] { (void)grid.at(2, 0); }, "out of bounds");
}

void svg_test() {
  TemporaryDirectory temporary;
  fixture(temporary.path());
  auto board = parse_fixture(temporary.path());
  const auto serializer = placement::make_serializer("binary");
  const auto binary = temporary.path() / "tiny.placebin";
  serializer->write(board, binary);
  board = serializer->read(binary);
  board.name = "tiny <&>";
  auto renderer = placement::make_renderer("SVG");
  const auto svg = temporary.path() / "tiny.svg";
  renderer->render(board, svg);
  const auto contents = read(svg);
  check(contents.find("tiny &lt;&amp;&gt; placement") != std::string::npos, "escaped SVG title");
  check(contents.find("translate(") != std::string::npos &&
            contents.find("scale(1 -1)") != std::string::npos,
        "placement coordinate transform");
  check(contents.find("class=\"movable\"") != std::string::npos &&
            contents.find("class=\"fixed\"") != std::string::npos &&
            contents.find("class=\"fixed-ni\"") != std::string::npos,
        "SVG cell classes");
  check(contents.find("M10.5 20h4v2h-4z") != std::string::npos, "rotated cell dimensions");

  auto utilization_renderer = placement::make_renderer("utilization-svg", {.bin_size = 5.0});
  const auto utilization_svg = temporary.path() / "utilization.svg";
  utilization_renderer->render(board, utilization_svg);
  const auto utilization_contents = read(utilization_svg);
  check(utilization_contents.find("tiny &lt;&amp;&gt; utilization") != std::string::npos,
        "utilization SVG title");
  check(utilization_contents.find("class=\"bin\"") != std::string::npos &&
            utilization_contents.find("fixed-overlay") != std::string::npos,
        "utilization SVG bins and fixed objects");
  check(utilization_contents.find(".fixed-overlay{fill:#f8fafc") != std::string::npos &&
            utilization_contents.find(".fixed-ni-overlay{fill:#f8fafc") != std::string::npos,
        "utilization SVG macros mask bin colors");

  placement::Board empty;
  expect_error([&] { renderer->render(empty, temporary.path() / "empty.svg"); },
               "without geometry");
  check(!std::filesystem::exists(temporary.path() / "empty.svg"),
        "failed render must not leave output");
}

} // namespace

int main() {
  const std::vector<std::pair<std::string_view, std::function<void()>>> tests{
      {"Bookshelf parser", parser_test},
      {"parser diagnostics", malformed_parser_test},
      {"binary round trip and corruption", binary_test},
      {"utilization grid", utilization_test},
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
