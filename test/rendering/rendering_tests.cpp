#include "../suites.hpp"

#include "placement/rendering/renderer.hpp"
#include "placement/rendering/style.hpp"
#include "placement/serialization/serializer.hpp"

namespace placement::test {
namespace {

void svg_test() {
  const auto &default_style = rendering_style::default_palette;
  const auto &dark_style = rendering_style::dark_mode_palette;
  check(&rendering_style::palette(false) == &default_style && &rendering_style::palette(true) == &dark_style,
        "rendering mode selects the common palette");

  TemporaryDirectory temporary;
  bookshelf_fixture(temporary.path());
  auto board = parse_bookshelf_fixture(temporary.path());
  const auto serializer = make_serializer("binary");
  const auto binary = temporary.path() / "tiny.placebin";
  serializer->write(board, binary);
  board = serializer->read(binary);
  board.name = "tiny <&>";
  board.cells[1].location->status = PlacementStatus::Movable;
  auto renderer = make_renderer("SVG");
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

  auto dark_renderer = make_renderer("svg", {.bin_size = std::nullopt, .dark_mode = true});
  const auto dark_svg = temporary.path() / "tiny-dark.svg";
  dark_renderer->render(board, dark_svg);
  const auto dark_contents = read(dark_svg);
  check(contains_parts(dark_contents, {".background{fill:", dark_style.background, "}"}) &&
            contains_parts(dark_contents, {".movable{fill:", dark_style.movable_fill}) &&
            contains_parts(dark_contents, {".macro{fill:", dark_style.macro_fill, ";stroke:", dark_style.macro_stroke}),
        "dark placement SVG palette");
  check(contents.find(dark_style.surface) == std::string::npos, "light placement SVG remains the default");

  auto utilization_renderer = make_renderer("utilization-svg", {.bin_size = 5.0});
  const auto utilization_svg = temporary.path() / "utilization.svg";
  utilization_renderer->render(board, utilization_svg);
  const auto utilization = read(utilization_svg);
  check(utilization.find("tiny &lt;&amp;&gt; utilization") != std::string::npos, "utilization SVG title");
  check(contains_parts(utilization, {".background{fill:", default_style.background, "}"}), "utilization SVG background is charcoal");
  check(utilization.find("class=\"bin\"") != std::string::npos && utilization.find("macro-overlay") != std::string::npos,
        "utilization SVG bins and macros");
  check(contains_parts(utilization, {".macro-overlay{fill:", default_style.surface}) &&
            contains_parts(utilization, {".fixed-ni-overlay{fill:", default_style.surface}),
        "utilization SVG macros mask bin colors");
  check(attribute_value(utilization, "viewBox") == attribute_value(contents, "viewBox"), "utilization SVG uses the full placement viewport");

  auto dark_utilization_renderer = make_renderer("utilization-svg", {.bin_size = 5.0, .dark_mode = true});
  const auto dark_utilization_svg = temporary.path() / "utilization-dark.svg";
  dark_utilization_renderer->render(board, dark_utilization_svg);
  const auto dark_utilization = read(dark_utilization_svg);
  const auto dark_green = std::string("hsl(120 ") + std::to_string(dark_style.heatmap_saturation_percent) + "% " +
                          std::to_string(dark_style.heatmap_lightness_percent) + "%)";
  check(contains_parts(dark_utilization, {".background{fill:", dark_style.background, "}"}) && dark_utilization.find(dark_green) != std::string::npos,
        "dark utilization SVG palette");

  auto pin_renderer = make_renderer("pin-density-svg", {.bin_size = 5.0});
  const auto pin_svg = temporary.path() / "pin-density.svg";
  pin_renderer->render(board, pin_svg);
  const auto pin_density = read(pin_svg);
  check(pin_density.find("tiny &lt;&amp;&gt; pin density") != std::string::npos, "pin density SVG title");
  check(contains_parts(pin_density, {".background{fill:", default_style.background, "}"}), "pin density SVG background is charcoal");
  check(pin_density.find("class=\"bin\"") != std::string::npos && pin_density.find("pins; density") != std::string::npos,
        "pin density SVG bins and tooltips");
  check(contains_parts(pin_density, {".movable-overlay{fill:", default_style.surface, ";fill-opacity:.42"}) &&
            contains_parts(pin_density, {".macro-overlay{fill:", default_style.surface}) &&
            contains_parts(pin_density, {".fixed-ni-overlay{fill:", default_style.surface}),
        "pin density SVG masks macros consistently with utilization");
  check(attribute_value(pin_density, "viewBox") == attribute_value(contents, "viewBox"), "pin density SVG uses the full placement viewport");

  auto dark_pin_renderer = make_renderer("pin-density-svg", {.bin_size = 5.0, .dark_mode = true});
  const auto dark_pin_svg = temporary.path() / "pin-density-dark.svg";
  dark_pin_renderer->render(board, dark_pin_svg);
  const auto dark_pin_density = read(dark_pin_svg);
  check(contains_parts(dark_pin_density, {".background{fill:", dark_style.background, "}"}) &&
            contains_parts(dark_pin_density, {".movable-overlay{fill:", dark_style.surface, ";fill-opacity:.42"}) &&
            contains_parts(dark_pin_density, {"stroke:", dark_style.overlay_stroke}),
        "dark pin density SVG palette");

  auto cell_renderer = make_renderer("cell-density-svg", {.bin_size = 5.0});
  const auto cell_svg = temporary.path() / "cell-density.svg";
  cell_renderer->render(board, cell_svg);
  const auto cell_density = read(cell_svg);
  check(cell_density.find("tiny &lt;&amp;&gt; cell density") != std::string::npos, "cell density SVG title");
  check(cell_density.find("class=\"bin\"") != std::string::npos && cell_density.find("movable-object area;") != std::string::npos &&
            cell_density.find("available area; density") != std::string::npos,
        "cell density SVG bins and tooltips");
  check(contains_parts(cell_density, {".macro-overlay{fill:", default_style.surface}) &&
            cell_density.find("class=\"macro-overlay\"") != std::string::npos &&
            contains_parts(cell_density, {".fixed-ni-overlay{fill:", default_style.surface}),
        "cell density SVG masks macros and non-interacting objects");
  check(attribute_value(cell_density, "viewBox") == attribute_value(contents, "viewBox"), "cell density SVG uses the full placement viewport");

  auto dark_cell_renderer = make_renderer("cell-density-svg", {.bin_size = 5.0, .dark_mode = true});
  const auto dark_cell_svg = temporary.path() / "cell-density-dark.svg";
  dark_cell_renderer->render(board, dark_cell_svg);
  const auto dark_cell_density = read(dark_cell_svg);
  check(contains_parts(dark_cell_density, {".background{fill:", dark_style.background, "}"}) &&
            contains_parts(dark_cell_density, {".macro-overlay{fill:", dark_style.surface}) &&
            contains_parts(dark_cell_density, {".fixed-ni-overlay{fill:", dark_style.surface}) &&
            contains_parts(dark_cell_density, {"stroke:", dark_style.overlay_stroke}),
        "dark cell density SVG palette");

  Board empty;
  expect_error([&] { renderer->render(empty, temporary.path() / "empty.svg"); }, "without geometry");
  check(!std::filesystem::exists(temporary.path() / "empty.svg"), "failed render must not leave output");
  expect_error([&] { (void)make_renderer("unknown"); }, "unsupported output format");
}

} // namespace

Tests rendering_tests() { return {{"SVG renderers", svg_test}}; }

} // namespace placement::test
