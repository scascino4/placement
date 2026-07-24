#include "../suites.hpp"

#include "placement/parsing/parser.hpp"

namespace placement::test {
namespace {

void bookshelf_parser_test() {
  TempDir tmp;
  bookshelf_fixture(tmp.path());
  const auto board = parse_bookshelf_fixture(tmp.path());
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

void malformed_bookshelf_test() {
  TempDir tmp;
  bookshelf_fixture(tmp.path());
  write(tmp.path() / "tiny.nets", "UCLA nets 1.0\nNumNets : 1\nNumPins : 1\n"
                                  "NetDegree : 1 broken\nmissing I : 0 0\n");
  expect_error([&] { (void)parse_bookshelf_fixture(tmp.path()); }, "pin references unknown cell");

  bookshelf_fixture(tmp.path());
  write(tmp.path() / "tiny.nodes", "UCLA nodes 1.0\nNumNodes : 2\nNumTerminals : 0\n"
                                   "duplicate 1 1\nduplicate 2 2\n");
  expect_error([&] { (void)parse_bookshelf_fixture(tmp.path()); }, "duplicate cell name 'duplicate'");

  bookshelf_fixture(tmp.path());
  write(tmp.path() / "tiny.nets", "UCLA nets 1.0\nNumNets : 2\nNumPins : 0\n"
                                  "NetDegree : 0 duplicate\nNetDegree : 0 duplicate\n");
  expect_error([&] { (void)parse_bookshelf_fixture(tmp.path()); }, "duplicate net name 'duplicate'");
}

void bookshelf_control_byte_blank_line_test() {
  TempDir tmp;
  bookshelf_fixture(tmp.path());
  std::string contents = "UCLA nodes 1.0\nNumNodes : 4\n";
  contents.append(2, '\0');
  contents += "\nNumTerminals : 2\na 2 4\nb 6 3 terminal\nc 5 2 terminal_NI\nd 1.5 2.5e0\n";
  write(tmp.path() / "tiny.nodes", std::string_view(contents.data(), contents.size()));

  const auto board = parse_bookshelf_fixture(tmp.path());
  check(board.cells.size() == 4, "control-byte blank line is ignored");
}

void placement_override_test() {
  TempDir tmp;
  bookshelf_fixture(tmp.path());
  const auto override = tmp.path() / "dreamplace.pl";
  write(override, "UCLA pl 1.0\na 101 202 : S\nb 303 404 : N\n");
  write(tmp.path() / "tiny.pl", "this file must not be parsed\n");
  BookshelfParseOptions opts;
  opts.placement_override = override;
  auto parser = make_parser(std::move(opts));
  const auto board = parser->parse(tmp.path() / "tiny.aux");
  check(board.cells[0].location->x == 101 && board.cells[0].location->y == 202, "placement override coordinates");
  check(board.cells[0].location->orientation == Orientation::S, "placement override orientation");
  check(board.cells[1].location->status == PlacementStatus::Movable && board.cells[1].macro, "placement override preserves movable macro identity");
  check(!board.cells[2].location && !board.cells[3].location, "placement override may leave cells unplaced");

  write(override, "UCLA pl 1.0\nunknown 1 2 : N\n");
  expect_error([&] { (void)parser->parse(tmp.path() / "tiny.aux"); }, override.string() + ":2: placement references unknown cell");
}

void bookshelf_manifest_validation_test() {
  TempDir tmp;
  bookshelf_fixture(tmp.path());
  write(tmp.path() / "tiny.aux", "UnknownFormat : tiny.nodes tiny.nets tiny.pl tiny.scl\n");
  expect_error([&] { (void)parse_bookshelf_fixture(tmp.path()); }, "unsupported AUX format");

  write(tmp.path() / "tiny.aux", "RowBasedPlacement : a.nodes b.nodes tiny.nets tiny.pl tiny.scl\n");
  expect_error([&] { (void)parse_bookshelf_fixture(tmp.path()); }, "duplicate component suffix '.nodes'");

  write(tmp.path() / "tiny.aux", "RowBasedPlacement : tiny.nodes tiny.nets tiny.pl unsupported.foo\n");
  expect_error([&] { (void)parse_bookshelf_fixture(tmp.path()); }, "unsupported component suffix '.foo'");

  write(tmp.path() / "tiny.aux", "RowBasedPlacement : tiny.nodes tiny.nets tiny.pl\n");
  expect_error([&] { (void)parse_bookshelf_fixture(tmp.path()); }, "manifest requires .nodes, .nets, .pl, and .scl");
}

void bookshelf_record_validation_test() {
  TempDir tmp;
  bookshelf_fixture(tmp.path());
  write(tmp.path() / "tiny.nodes", "UCLA nodes 1.0\nNumNodes : 1\nNumTerminals : 0\na -1 2\n");
  expect_error([&] { (void)parse_bookshelf_fixture(tmp.path()); }, "cell dimensions cannot be negative");

  bookshelf_fixture(tmp.path());
  write(tmp.path() / "tiny.nodes", "UCLA nodes 1.0\nNumNodes : 1\nNumTerminals : 1\na 1 2 unsupported\n");
  expect_error([&] { (void)parse_bookshelf_fixture(tmp.path()); }, "unknown node kind 'unsupported'");

  bookshelf_fixture(tmp.path());
  write(tmp.path() / "tiny.nets", "UCLA nets 1.0\nNumNets : 1\nNumPins : 1\nNetDegree : 1 n\na I 1 2\n");
  expect_error([&] { (void)parse_bookshelf_fixture(tmp.path()); }, "pin offsets require ': <x> <y>'");

  bookshelf_fixture(tmp.path());
  write(tmp.path() / "tiny.wts", "UCLA wts 1.0\na 1\nb 1 2\n");
  expect_error([&] { (void)parse_bookshelf_fixture(tmp.path()); }, "inconsistent node weight dimension");

  bookshelf_fixture(tmp.path());
  write(tmp.path() / "tiny.wts", "UCLA wts 1.0\nmissing 1\n");
  expect_error([&] { (void)parse_bookshelf_fixture(tmp.path()); }, "weight references unknown cell or net 'missing'");

  bookshelf_fixture(tmp.path());
  write(tmp.path() / "tiny.wts", "UCLA wts 1.0\na 1\na 2\n");
  expect_error([&] { (void)parse_bookshelf_fixture(tmp.path()); }, "duplicate weight record for 'a'");
}

void bookshelf_row_and_placement_validation_test() {
  TempDir tmp;
  bookshelf_fixture(tmp.path());
  write(tmp.path() / "tiny.scl", "UCLA scl 1.0\nNumRows : 1\nCoreRow Horizontal\n"
                                 "Coordinate : 0\nHeight : 1\nSitespacing : 1\nSitesymmetry : diagonal\n"
                                 "SubrowOrigin : 0 NumSites : 1\nEnd\n");
  expect_error([&] { (void)parse_bookshelf_fixture(tmp.path()); }, "unknown site symmetry 'diagonal'");

  bookshelf_fixture(tmp.path());
  write(tmp.path() / "tiny.scl", "UCLA scl 1.0\nNumRows : 1\nCoreRow Horizontal\nCoordinate : 0\nSitespacing : 0\n"
                                 "SubrowOrigin : 0 NumSites : 1\nEnd\n");
  expect_error([&] { (void)parse_bookshelf_fixture(tmp.path()); }, "invalid row dimensions");

  bookshelf_fixture(tmp.path());
  write(tmp.path() / "tiny.pl", "UCLA pl 1.0\na 0 0 : N\na 1 1 : N\n");
  expect_error([&] { (void)parse_bookshelf_fixture(tmp.path()); }, "duplicate placement for 'a'");

  bookshelf_fixture(tmp.path());
  write(tmp.path() / "tiny.pl", "UCLA pl 1.0\na 0 0 : N DIMS=(-1,2)\n");
  expect_error([&] { (void)parse_bookshelf_fixture(tmp.path()); }, "placement dimensions cannot be negative");
}

} // namespace

Tests bookshelf_tests() {
  return {{"Bookshelf parser", bookshelf_parser_test},
          {"Bookshelf parser diagnostics", malformed_bookshelf_test},
          {"Bookshelf parser control-byte blank line", bookshelf_control_byte_blank_line_test},
          {"placement override", placement_override_test},
          {"Bookshelf manifest validation", bookshelf_manifest_validation_test},
          {"Bookshelf record validation", bookshelf_record_validation_test},
          {"Bookshelf row and placement validation", bookshelf_row_and_placement_validation_test}};
}

} // namespace placement::test
