#include "../suites.hpp"

#include "placement/parsing/parser.hpp"

namespace placement::test {
namespace {

void lefdef_parser_test() {
  TempDir tmp;
  lefdef_fixture(tmp.path());
  const auto board = parse_lefdef_fixture(tmp.path());
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

void malformed_lefdef_test() {
  TempDir tmp;
  lefdef_fixture(tmp.path());
  auto text = read(tmp.path() / "design.def");
  auto pos = text.find("u1 NAND");
  check(pos != std::string::npos, "fixture component exists");
  text.replace(pos, std::string_view("u1 NAND").size(), "u1 MISSING");
  write(tmp.path() / "design.def", text);
  expect_error([&] { (void)parse_lefdef_fixture(tmp.path()); }, "component references unknown macro 'MISSING'");

  lefdef_fixture(tmp.path());
  text = read(tmp.path() / "design.def");
  pos = text.find("( u1 A )");
  check(pos != std::string::npos, "fixture endpoint exists");
  text.replace(pos, std::string_view("( u1 A )").size(), "( u1 MISSING )");
  write(tmp.path() / "design.def", text);
  expect_error([&] { (void)parse_lefdef_fixture(tmp.path()); }, "component pin references unknown macro pin 'MISSING'");

  lefdef_fixture(tmp.path());
  text = read(tmp.path() / "design.def");
  pos = text.find("COMPONENTS 2");
  check(pos != std::string::npos, "fixture component count exists");
  text.replace(pos, std::string_view("COMPONENTS 2").size(), "COMPONENTS 3");
  write(tmp.path() / "design.def", text);
  expect_error([&] { (void)parse_lefdef_fixture(tmp.path()); }, "COMPONENTS count does not match records");

  lefdef_fixture(tmp.path());
  LefDefParseOptions opts;
  opts.lef_files = {tmp.path() / "tech.lef", tmp.path() / "tech.lef", tmp.path() / "cells.lef"};
  auto dup_parser = make_parser(std::move(opts));
  expect_error([&] { (void)dup_parser->parse(tmp.path() / "design.def"); }, "duplicate site name 'core'");

  auto parser = make_parser(LefDefParseOptions{});
  expect_error([&] { (void)parser->parse(tmp.path() / "design.def"); }, "requires at least one --lef-file");
}

} // namespace

Tests lefdef_tests() { return {{"LEF/DEF parser", lefdef_parser_test}, {"LEF/DEF parser diagnostics", malformed_lefdef_test}}; }

} // namespace placement::test
