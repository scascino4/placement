#include "../suites.hpp"

#include "placement/rendering/renderer.hpp"
#include "placement/rendering/style.hpp"
#include "placement/serialization/serializer.hpp"

namespace placement::test {
namespace {

void svg_test() {
  const auto &style = rendering_style::default_palette;
  const auto &dark = rendering_style::dark_mode_palette;
  check(&rendering_style::palette(false) == &style && &rendering_style::palette(true) == &dark, "rendering mode selects the common palette");

  TemporaryDirectory tmp;
  bookshelf_fixture(tmp.path());
  auto board = parse_bookshelf_fixture(tmp.path());
  const auto serializer = make_serializer("binary");
  const auto binary = tmp.path() / "tiny.placebin";
  serializer->write(board, binary);
  board = serializer->read(binary);
  board.name = "tiny <&>";
  board.cells[1].location->status = PlacementStatus::Movable;
  auto renderer = make_renderer("SVG");
  const auto svg = tmp.path() / "tiny.svg";
  renderer->render(board, svg);
  const auto contents = read(svg);
  check(contents.find("tiny &lt;&amp;&gt; placement") != std::string::npos, "escaped SVG title");
  check(contents.find("translate(") != std::string::npos && contents.find("scale(1 -1)") != std::string::npos, "placement coordinate transform");
  check(contents.find("class=\"movable\"") != std::string::npos && contents.find("class=\"macro\"") != std::string::npos &&
            contents.find("class=\"fixed-ni\"") != std::string::npos,
        "SVG cell classes");
  check(contains_parts(contents, {".background{fill:", style.background, "}"}), "default placement SVG background is charcoal");
  check(contains_parts(contents, {".macro{fill:", style.macro_fill, ";stroke:", style.macro_stroke}),
        "light placement SVG macros have contrasting outlines");
  check(contents.find("M10.5 20h4v2h-4z") != std::string::npos, "rotated cell dimensions");

  auto dark_renderer = make_renderer("svg", {.bin_size = std::nullopt, .dark_mode = true});
  const auto dark_svg = tmp.path() / "tiny-dark.svg";
  dark_renderer->render(board, dark_svg);
  const auto dark_contents = read(dark_svg);
  check(contains_parts(dark_contents, {".background{fill:", dark.background, "}"}) &&
            contains_parts(dark_contents, {".movable{fill:", dark.movable_fill}) &&
            contains_parts(dark_contents, {".macro{fill:", dark.macro_fill, ";stroke:", dark.macro_stroke}),
        "dark placement SVG palette");
  check(contents.find(dark.surface) == std::string::npos, "light placement SVG remains the default");

  auto util_renderer = make_renderer("utilization-svg", {.bin_size = 5.0});
  const auto util_svg = tmp.path() / "utilization.svg";
  util_renderer->render(board, util_svg);
  const auto utilization = read(util_svg);
  check(utilization.find("tiny &lt;&amp;&gt; utilization") != std::string::npos, "utilization SVG title");
  check(contains_parts(utilization, {".background{fill:", style.background, "}"}), "utilization SVG background is charcoal");
  check(utilization.find("class=\"bin\"") != std::string::npos && utilization.find("macro-overlay") != std::string::npos,
        "utilization SVG bins and macros");
  check(contains_parts(utilization, {".macro-overlay{fill:", style.surface}) &&
            contains_parts(utilization, {".fixed-ni-overlay{fill:", style.surface}),
        "utilization SVG macros mask bin colors");
  check(attribute_value(utilization, "viewBox") == attribute_value(contents, "viewBox"), "utilization SVG uses the full placement viewport");

  auto dark_util_renderer = make_renderer("utilization-svg", {.bin_size = 5.0, .dark_mode = true});
  const auto dark_util_svg = tmp.path() / "utilization-dark.svg";
  dark_util_renderer->render(board, dark_util_svg);
  const auto dark_util = read(dark_util_svg);
  const auto dark_green =
      std::string("hsl(120 ") + std::to_string(dark.heatmap_saturation_percent) + "% " + std::to_string(dark.heatmap_lightness_percent) + "%)";
  check(contains_parts(dark_util, {".background{fill:", dark.background, "}"}) && dark_util.find(dark_green) != std::string::npos,
        "dark utilization SVG palette");

  auto pin_renderer = make_renderer("pin-density-svg", {.bin_size = 5.0});
  const auto pin_svg = tmp.path() / "pin-density.svg";
  pin_renderer->render(board, pin_svg);
  const auto pin_dens = read(pin_svg);
  check(pin_dens.find("tiny &lt;&amp;&gt; pin density") != std::string::npos, "pin density SVG title");
  check(contains_parts(pin_dens, {".background{fill:", style.background, "}"}), "pin density SVG background is charcoal");
  check(pin_dens.find("class=\"bin\"") != std::string::npos && pin_dens.find("pins; density") != std::string::npos,
        "pin density SVG bins and tooltips");
  check(contains_parts(pin_dens, {".movable-overlay{fill:", style.surface, ";fill-opacity:.42"}) &&
            contains_parts(pin_dens, {".macro-overlay{fill:", style.surface}) && contains_parts(pin_dens, {".fixed-ni-overlay{fill:", style.surface}),
        "pin density SVG masks macros consistently with utilization");
  check(attribute_value(pin_dens, "viewBox") == attribute_value(contents, "viewBox"), "pin density SVG uses the full placement viewport");

  auto dark_pin_renderer = make_renderer("pin-density-svg", {.bin_size = 5.0, .dark_mode = true});
  const auto dark_pin_svg = tmp.path() / "pin-density-dark.svg";
  dark_pin_renderer->render(board, dark_pin_svg);
  const auto dark_pin_dens = read(dark_pin_svg);
  check(contains_parts(dark_pin_dens, {".background{fill:", dark.background, "}"}) &&
            contains_parts(dark_pin_dens, {".movable-overlay{fill:", dark.surface, ";fill-opacity:.42"}) &&
            contains_parts(dark_pin_dens, {"stroke:", dark.overlay_stroke}),
        "dark pin density SVG palette");

  auto cell_renderer = make_renderer("cell-density-svg", {.bin_size = 5.0});
  const auto cell_svg = tmp.path() / "cell-density.svg";
  cell_renderer->render(board, cell_svg);
  const auto cell_dens = read(cell_svg);
  check(cell_dens.find("tiny &lt;&amp;&gt; cell density") != std::string::npos, "cell density SVG title");
  check(cell_dens.find("class=\"bin\"") != std::string::npos && cell_dens.find("movable-object area;") != std::string::npos &&
            cell_dens.find("available area; density") != std::string::npos,
        "cell density SVG bins and tooltips");
  check(contains_parts(cell_dens, {".macro-overlay{fill:", style.surface}) && cell_dens.find("class=\"macro-overlay\"") != std::string::npos &&
            contains_parts(cell_dens, {".fixed-ni-overlay{fill:", style.surface}),
        "cell density SVG masks macros and non-interacting objects");
  check(attribute_value(cell_dens, "viewBox") == attribute_value(contents, "viewBox"), "cell density SVG uses the full placement viewport");

  auto dark_cell_renderer = make_renderer("cell-density-svg", {.bin_size = 5.0, .dark_mode = true});
  const auto dark_cell_svg = tmp.path() / "cell-density-dark.svg";
  dark_cell_renderer->render(board, dark_cell_svg);
  const auto dark_cell_dens = read(dark_cell_svg);
  check(contains_parts(dark_cell_dens, {".background{fill:", dark.background, "}"}) &&
            contains_parts(dark_cell_dens, {".macro-overlay{fill:", dark.surface}) &&
            contains_parts(dark_cell_dens, {".fixed-ni-overlay{fill:", dark.surface}) &&
            contains_parts(dark_cell_dens, {"stroke:", dark.overlay_stroke}),
        "dark cell density SVG palette");

  Board empty;
  expect_error([&] { renderer->render(empty, tmp.path() / "empty.svg"); }, "without geometry");
  check(!std::filesystem::exists(tmp.path() / "empty.svg"), "failed render must not leave output");
  expect_error([&] { (void)make_renderer("unknown"); }, "unsupported output format");
}

} // namespace

Tests svg_tests() { return {{"SVG renderers", svg_test}}; }

} // namespace placement::test
