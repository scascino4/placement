#include "../suites.hpp"

#include "placement/serialization/serializer.hpp"

#include <cstdint>
#include <limits>

namespace placement::test {
namespace {

void set_uint64_le(std::string &bytes, std::size_t offset, std::uint64_t value) {
  for (std::size_t i = 0; i < sizeof(value); ++i)
    bytes[offset + i] = static_cast<char>(static_cast<std::uint8_t>(value >> (i * 8)));
}

void binary_test() {
  constexpr std::size_t counts_offset = 16;
  constexpr std::size_t payload_offset = counts_offset + 4 * sizeof(std::uint64_t);
  constexpr std::size_t min_row_bytes = 4 * sizeof(std::uint64_t) + 2 * sizeof(std::uint8_t) + sizeof(std::uint64_t);

  TempDir tmp;
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
  set_uint64_le(bytes, counts_offset, std::numeric_limits<std::uint64_t>::max());
  write(tmp.path() / "bad-count.placebin", bytes);
  expect_error([&] { (void)serializer->read(tmp.path() / "bad-count.placebin"); }, "invalid cell count");

  bytes = read(first);
  set_uint64_le(bytes, counts_offset, 100'000'000);
  write(tmp.path() / "impossible-count.placebin", bytes);
  expect_error([&] { (void)serializer->read(tmp.path() / "impossible-count.placebin"); }, "impossible cell count");

  bytes = read(first);
  const auto cell_and_row_count = static_cast<std::uint64_t>((bytes.size() - payload_offset) / min_row_bytes);
  check(cell_and_row_count > 1, "binary fixture is large enough for aggregate count test");
  set_uint64_le(bytes, counts_offset, cell_and_row_count);
  set_uint64_le(bytes, counts_offset + sizeof(std::uint64_t), cell_and_row_count);
  set_uint64_le(bytes, counts_offset + 2 * sizeof(std::uint64_t), 0);
  set_uint64_le(bytes, counts_offset + 3 * sizeof(std::uint64_t), 0);
  write(tmp.path() / "impossible-aggregate-count.placebin", bytes);
  expect_error([&] { (void)serializer->read(tmp.path() / "impossible-aggregate-count.placebin"); }, "impossible row count");

  Board invalid_range;
  invalid_range.name = "invalid";
  invalid_range.nets.push_back({"net", 1, 1, {}});
  serializer->write(invalid_range, tmp.path() / "bad-range.placebin");
  expect_error([&] { (void)serializer->read(tmp.path() / "bad-range.placebin"); }, "net pin range is out of bounds");

  expect_error([&] { (void)make_serializer("unknown"); }, "unsupported serialization format");
}

} // namespace

Tests binary_tests() { return {{"binary round trip and corruption", binary_test}}; }

} // namespace placement::test
