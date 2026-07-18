#include "../suites.hpp"

#include "placement/parsing/parser.hpp"

namespace placement::test {
namespace {

void bookshelf_parser_test() {
  TemporaryDirectory temporary;
  bookshelf_fixture(temporary.path());
  const auto board = parse_bookshelf_fixture(temporary.path());
  check(board.name == "tiny", "design name");
  check(board.cells.size() == 4 && board.rows.size() == 1, "cell and row counts");
  check(board.nets.size() == 2 && board.pins.size() == 4, "net and pin counts");
  check(board.cells[1].kind == CellKind::Terminal && board.cells[1].macro, "terminal kind and macro identity");
  check(board.cells[2].kind == CellKind::TerminalNonInteracting && !board.cells[2].macro, "terminal_NI kind");
  check(!board.cells[3].location, "undefined placement");
  check(board.cells[3].width == 1.5 && board.cells[3].height == 2.5, "decimal and scientific geometry");
  check(board.cells[0].location->orientation == Orientation::E, "orientation");
  check(board.cells[1].location->status == PlacementStatus::Fixed, "fixed status");
  check(board.cells[2].location->status == PlacementStatus::FixedNonInteracting, "fixed_NI status");
  check(*board.cells[2].location->width == 7 && *board.cells[2].location->height == 8, "DIMS");
  check(board.cells[0].weights == std::vector<double>({1, 2}), "node weights");
  check(board.nets[0].weights == std::vector<double>({2.5}), "net weights");
  check(board.pins[2].direction == PinDirection::Bidirectional, "bidirectional pin");
  check(board.rows[0].subrows.size() == 2 && board.rows[0].site_width == 2, "multiple subrows and default site width");
  check(board.rows[0].symmetry == 7, "row symmetry");
}

void lefdef_parser_test() {
  TemporaryDirectory temporary;
  lefdef_fixture(temporary.path());
  const auto board = parse_lefdef_fixture(temporary.path());
  check(board.name == "tiny_def", "LEF/DEF design name");
  check(board.cells.size() == 3 && board.rows.size() == 1, "LEF/DEF cell and row counts");
  check(board.nets.size() == 2 && board.pins.size() == 4, "LEF/DEF net and endpoint counts");
  check(board.cells[0].width == 2 && board.cells[0].height == 2 && !board.cells[0].macro, "LEF standard-cell geometry");
  check(board.cells[0].location->x == 1 && board.cells[0].location->orientation == Orientation::FS, "DEF component placement and units");
  check(board.cells[1].macro && board.cells[1].location->status == PlacementStatus::Fixed, "LEF block macro identity");
  check(board.cells[2].kind == CellKind::TerminalNonInteracting && board.cells[2].location->status == PlacementStatus::FixedNonInteracting &&
            board.cells[2].location->x == 0 && board.cells[2].location->y == 1,
        "DEF top-level pin mapping");
  check(board.rows[0].site_width == 1 && board.rows[0].site_spacing == 1 && board.rows[0].height == 2 && board.rows[0].symmetry == 3,
        "LEF site maps to DEF row");
  check(board.rows[0].subrows.size() == 2 && board.rows[0].subrows[0].origin == 0 && board.rows[0].subrows[0].site_count == 3 &&
            board.rows[0].subrows[1].origin == 5 && board.rows[0].subrows[1].site_count == 5,
        "placement blockages split row capacity");
  check(board.pins[0].cell == 2 && board.pins[0].direction == PinDirection::Input, "top-level pin endpoint");
  check(board.pins[1].cell == 0 && board.pins[1].direction == PinDirection::Input && close(board.pins[1].offset_x, -0.75), "LEF input pin offset");
  check(board.pins[2].direction == PinDirection::Output && close(board.pins[2].offset_x, 0.75), "multi-rectangle LEF output pin offset");
}

void malformed_bookshelf_test() {
  TemporaryDirectory temporary;
  bookshelf_fixture(temporary.path());
  write(temporary.path() / "tiny.nets", "UCLA nets 1.0\nNumNets : 1\nNumPins : 1\n"
                                        "NetDegree : 1 broken\nmissing I : 0 0\n");
  expect_error([&] { (void)parse_bookshelf_fixture(temporary.path()); }, "pin references unknown cell");

  bookshelf_fixture(temporary.path());
  write(temporary.path() / "tiny.nodes", "UCLA nodes 1.0\nNumNodes : 2\nNumTerminals : 0\n"
                                         "duplicate 1 1\nduplicate 2 2\n");
  expect_error([&] { (void)parse_bookshelf_fixture(temporary.path()); }, "duplicate cell name 'duplicate'");

  bookshelf_fixture(temporary.path());
  write(temporary.path() / "tiny.nets", "UCLA nets 1.0\nNumNets : 2\nNumPins : 0\n"
                                        "NetDegree : 0 duplicate\nNetDegree : 0 duplicate\n");
  expect_error([&] { (void)parse_bookshelf_fixture(temporary.path()); }, "duplicate net name 'duplicate'");
}

void malformed_lefdef_test() {
  TemporaryDirectory temporary;
  lefdef_fixture(temporary.path());
  auto contents = read(temporary.path() / "design.def");
  auto position = contents.find("u1 NAND");
  check(position != std::string::npos, "fixture component exists");
  contents.replace(position, std::string_view("u1 NAND").size(), "u1 MISSING");
  write(temporary.path() / "design.def", contents);
  expect_error([&] { (void)parse_lefdef_fixture(temporary.path()); }, "component references unknown macro 'MISSING'");

  lefdef_fixture(temporary.path());
  contents = read(temporary.path() / "design.def");
  position = contents.find("( u1 A )");
  check(position != std::string::npos, "fixture endpoint exists");
  contents.replace(position, std::string_view("( u1 A )").size(), "( u1 MISSING )");
  write(temporary.path() / "design.def", contents);
  expect_error([&] { (void)parse_lefdef_fixture(temporary.path()); }, "component pin references unknown macro pin 'MISSING'");

  lefdef_fixture(temporary.path());
  contents = read(temporary.path() / "design.def");
  position = contents.find("COMPONENTS 2");
  check(position != std::string::npos, "fixture component count exists");
  contents.replace(position, std::string_view("COMPONENTS 2").size(), "COMPONENTS 3");
  write(temporary.path() / "design.def", contents);
  expect_error([&] { (void)parse_lefdef_fixture(temporary.path()); }, "COMPONENTS count does not match records");

  lefdef_fixture(temporary.path());
  LefDefParseOptions options;
  options.lef_files = {temporary.path() / "tech.lef", temporary.path() / "tech.lef", temporary.path() / "cells.lef"};
  auto duplicate_library_parser = make_parser(std::move(options));
  expect_error([&] { (void)duplicate_library_parser->parse(temporary.path() / "design.def"); }, "duplicate site name 'core'");

  auto parser = make_parser(LefDefParseOptions{});
  expect_error([&] { (void)parser->parse(temporary.path() / "design.def"); }, "requires at least one --lef-file");
}

void placement_override_test() {
  TemporaryDirectory temporary;
  bookshelf_fixture(temporary.path());
  const auto override = temporary.path() / "dreamplace.pl";
  write(override, "UCLA pl 1.0\na 101 202 : S\nb 303 404 : N\n");
  write(temporary.path() / "tiny.pl", "this file must not be parsed\n");
  BookshelfParseOptions options;
  options.placement_override = override;
  auto parser = make_parser(std::move(options));
  const auto board = parser->parse(temporary.path() / "tiny.aux");
  check(board.cells[0].location->x == 101 && board.cells[0].location->y == 202, "placement override coordinates");
  check(board.cells[0].location->orientation == Orientation::S, "placement override orientation");
  check(board.cells[1].location->status == PlacementStatus::Movable && board.cells[1].macro, "placement override preserves movable macro identity");
  check(!board.cells[2].location && !board.cells[3].location, "placement override may leave cells unplaced");

  write(override, "UCLA pl 1.0\nunknown 1 2 : N\n");
  expect_error([&] { (void)parser->parse(temporary.path() / "tiny.aux"); }, override.string() + ":2: placement references unknown cell");
}

} // namespace

Tests parsing_tests() {
  return {{"Bookshelf parser", bookshelf_parser_test},
          {"LEF/DEF parser", lefdef_parser_test},
          {"Bookshelf parser diagnostics", malformed_bookshelf_test},
          {"LEF/DEF parser diagnostics", malformed_lefdef_test},
          {"placement override", placement_override_test}};
}

} // namespace placement::test
