#include "../suites.hpp"

#include "placement/serialization/serializer.hpp"

namespace placement::test {
namespace {

void binary_test() {
  TemporaryDirectory tmp;
  bookshelf_fixture(tmp.path());

  const auto board = parse_bookshelf_fixture(tmp.path());
  const auto serializer = make_serializer("BINARY");
  const auto first = tmp.path() / "first.placebin";
  const auto second = tmp.path() / "second.placebin";
  serializer->write(board, first);
  serializer->write(board, second);
  check(read(first) == read(second), "binary output must be deterministic");
  const auto decoded = serializer->read(first);
  check(decoded.name == board.name && decoded.cells.size() == board.cells.size(), "binary board identity");
  check(decoded.cells[1].macro && !decoded.cells[2].macro, "binary macro identity");
  check(decoded.cells[2].location->orientation == Orientation::FW, "binary orientation");
  check(decoded.pins[0].offset_x == -0.5 && decoded.nets[0].pin_count == 3, "binary connectivity");

  auto bytes = read(first);
  bytes[0] = 'X';
  write(tmp.path() / "bad-magic.placebin", bytes);
  expect_error([&] { (void)serializer->read(tmp.path() / "bad-magic.placebin"); }, "invalid binary magic");

  bytes = read(first);
  bytes.resize(bytes.size() - 3);
  write(tmp.path() / "truncated.placebin", bytes);
  expect_error([&] { (void)serializer->read(tmp.path() / "truncated.placebin"); }, "truncated binary placement");

  bytes = read(first);
  bytes.push_back('x');
  write(tmp.path() / "trailing.placebin", bytes);
  expect_error([&] { (void)serializer->read(tmp.path() / "trailing.placebin"); }, "trailing binary data");

  bytes = read(first);
  // Magic (8), name length (4), and "tiny" (4) precede the cell count.
  for (std::size_t idx = 16; idx < 24; ++idx)
    bytes[idx] = '\xFF';
  write(tmp.path() / "bad-count.placebin", bytes);
  expect_error([&] { (void)serializer->read(tmp.path() / "bad-count.placebin"); }, "invalid cell count");

  Board invalid_range;
  invalid_range.name = "invalid";
  invalid_range.nets.push_back({"net", 1, 1, {}});
  serializer->write(invalid_range, tmp.path() / "bad-range.placebin");
  expect_error([&] { (void)serializer->read(tmp.path() / "bad-range.placebin"); }, "net pin range is out of bounds");

  expect_error([&] { (void)make_serializer("unknown"); }, "unsupported serialization format");
}

} // namespace

Tests serialization_tests() { return {{"binary round trip and corruption", binary_test}}; }

} // namespace placement::test
