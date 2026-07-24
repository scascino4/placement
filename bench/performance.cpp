#include "placement/model.hpp"
#include "placement/parsing/parser.hpp"
#include "placement/rendering/renderer.hpp"
#include "placement/serialization/serializer.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <exception>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <limits>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace {

using Clock = std::chrono::steady_clock;

template <typename Fn> double measure(Fn &&fn) {
  const auto begin = Clock::now();
  std::forward<Fn>(fn)();
  return std::chrono::duration<double>(Clock::now() - begin).count();
}

void result(std::string_view stage, double seconds, std::uintmax_t units = 0, double checksum = 0) {
  std::cout << stage << '\t' << std::fixed << std::setprecision(6) << seconds << '\t' << units << '\t' << std::setprecision(17) << checksum << '\n';
}

[[noreturn]] void usage() {
  std::cerr << "Usage:\n"
               "  placement_benchmark scan <input>...\n"
               "  placement_benchmark write <bytes> <output>\n"
               "  placement_benchmark bookshelf <aux> <output-placebin> [placement]\n"
               "  placement_benchmark lefdef <def> <output-placebin> <lef>...\n"
               "  placement_benchmark binary <input-placebin> <output-placebin>\n"
               "  placement_benchmark analysis <input-placebin> [bin-size]\n"
               "  placement_benchmark render <format> <input-placebin> <output> [bin-size]\n";
  std::exit(2);
}

[[nodiscard]] std::uint64_t unsigned_number(std::string_view value, std::string_view name) {
  std::size_t consumed = 0;
  const auto parsed = std::stoull(std::string(value), &consumed);
  if (consumed != value.size())
    throw std::runtime_error("invalid " + std::string(name));
  return parsed;
}

[[nodiscard]] double real_number(std::string_view value, std::string_view name) {
  std::size_t consumed = 0;
  const auto parsed = std::stod(std::string(value), &consumed);
  if (consumed != value.size())
    throw std::runtime_error("invalid " + std::string(name));
  return parsed;
}

void scan(int argc, char **argv) {
  if (argc < 3)
    usage();

  constexpr std::size_t BUF_SIZE = 256 * 1024;
  std::array<char, BUF_SIZE> buf{};
  std::uintmax_t bytes = 0;
  std::uint64_t checksum = 0;
  const auto seconds = measure([&] {
    for (int arg = 2; arg < argc; ++arg) {
      std::ifstream in(argv[arg], std::ios::binary);
      if (!in)
        throw std::runtime_error("cannot open " + std::string(argv[arg]));
      while (in.read(buf.data(), static_cast<std::streamsize>(buf.size())) || in.gcount() != 0) {
        const auto count = static_cast<std::size_t>(in.gcount());
        bytes += count;
        checksum += static_cast<unsigned char>(buf[0]);
        checksum += static_cast<unsigned char>(buf[count - 1]);
      }
      if (!in.eof())
        throw std::runtime_error("failed while reading " + std::string(argv[arg]));
    }
  });
  result("scan", seconds, bytes, static_cast<double>(checksum));
}

void write_bytes(int argc, char **argv) {
  if (argc != 4)
    usage();

  constexpr std::size_t BUF_SIZE = 256 * 1024;
  const auto bytes = unsigned_number(argv[2], "byte count");
  std::array<char, BUF_SIZE> buf{};
  const auto seconds = measure([&] {
    std::ofstream out(argv[3], std::ios::binary);
    if (!out)
      throw std::runtime_error("cannot create " + std::string(argv[3]));
    std::uint64_t left = bytes;
    while (left != 0) {
      const auto count = static_cast<std::streamsize>(std::min<std::uint64_t>(left, buf.size()));
      out.write(buf.data(), count);
      left -= static_cast<std::uint64_t>(count);
    }
    out.flush();
    if (!out)
      throw std::runtime_error("failed while writing " + std::string(argv[3]));
  });
  result("write", seconds, bytes);
}

void parse_bookshelf(int argc, char **argv) {
  if (argc < 4 || argc > 5)
    usage();

  placement::BookshelfParseOptions opts;
  if (argc == 5)
    opts.placement_override = argv[4];
  const auto parser = placement::make_parser(std::move(opts));
  placement::Board board;
  const auto parse_seconds = measure([&] { board = parser->parse(argv[2]); });
  result("bookshelf_parse", parse_seconds, board.cells.size() + board.rows.size() + board.nets.size() + board.pins.size());

  const auto serializer = placement::make_serializer("binary");
  const auto write_seconds = measure([&] { serializer->write(board, argv[3]); });
  result("binary_write", write_seconds, std::filesystem::file_size(argv[3]));
}

void parse_lefdef(int argc, char **argv) {
  if (argc < 5)
    usage();

  placement::LefDefParseOptions opts;
  for (int arg = 4; arg < argc; ++arg)
    opts.lef_files.emplace_back(argv[arg]);
  const auto parser = placement::make_parser(std::move(opts));
  placement::Board board;
  const auto parse_seconds = measure([&] { board = parser->parse(argv[2]); });
  result("lefdef_parse", parse_seconds, board.cells.size() + board.rows.size() + board.nets.size() + board.pins.size());

  const auto serializer = placement::make_serializer("binary");
  const auto write_seconds = measure([&] { serializer->write(board, argv[3]); });
  result("binary_write", write_seconds, std::filesystem::file_size(argv[3]));
}

void binary(int argc, char **argv) {
  if (argc != 4)
    usage();

  const auto serializer = placement::make_serializer("binary");
  placement::Board board;
  const auto read_seconds = measure([&] { board = serializer->read(argv[2]); });
  result("binary_read", read_seconds, std::filesystem::file_size(argv[2]));

  const auto write_seconds = measure([&] { serializer->write(board, argv[3]); });
  result("binary_write", write_seconds, std::filesystem::file_size(argv[3]));
}

[[nodiscard]] double default_bin_size(const placement::Board &board) {
  double min_x = std::numeric_limits<double>::infinity();
  double min_y = std::numeric_limits<double>::infinity();
  double max_x = -std::numeric_limits<double>::infinity();
  double max_y = -std::numeric_limits<double>::infinity();
  for (const auto &row : board.rows)
    for (const auto &subrow : row.subrows) {
      const auto rect = placement::subrow_rectangle(row, subrow);
      min_x = std::min(min_x, rect.x);
      min_y = std::min(min_y, rect.y);
      max_x = std::max(max_x, rect.right());
      max_y = std::max(max_y, rect.top());
    }
  return std::max(max_x - min_x, max_y - min_y) / 100.0;
}

[[nodiscard]] double cell_walk(const placement::Board &board) {
  double checksum = 0;
  for (const auto &row : board.rows) {
    checksum += row.coordinate + row.height + row.site_width + row.site_spacing;
    for (const auto &subrow : row.subrows)
      checksum += subrow.origin + static_cast<double>(subrow.site_count);
  }
  for (const auto &cell : board.cells) {
    checksum += cell.width + cell.height;
    if (cell.location)
      checksum += cell.location->x + cell.location->y;
  }
  return checksum;
}

[[nodiscard]] double pin_walk(const placement::Board &board) {
  double checksum = 0;
  for (const auto &pin : board.pins) {
    const auto &cell = board.cells[pin.cell];
    checksum += pin.offset_x + pin.offset_y + cell.width + cell.height;
    if (cell.location)
      checksum += cell.location->x + cell.location->y;
  }
  return checksum;
}

void analysis(int argc, char **argv) {
  if (argc < 3 || argc > 4)
    usage();

  const auto serializer = placement::make_serializer("binary");
  placement::Board board;
  const auto read_seconds = measure([&] { board = serializer->read(argv[2]); });
  result("binary_read", read_seconds, std::filesystem::file_size(argv[2]));

  const auto bin_size = argc == 4 ? real_number(argv[3], "bin size") : default_bin_size(board);
  double checksum = 0;
  const auto cell_walk_seconds = measure([&] { checksum = cell_walk(board); });
  result("cell_walk", cell_walk_seconds, board.cells.size() + board.rows.size(), checksum);

  const auto pin_walk_seconds = measure([&] { checksum = pin_walk(board); });
  result("pin_walk", pin_walk_seconds, board.pins.size(), checksum);

  placement::UtilizationGrid utilization;
  const auto utilization_seconds = measure([&] { utilization = board.utilization(bin_size); });
  checksum = 0;
  for (const auto &bin : utilization.bins)
    checksum += bin.movable_area + bin.placeable_area;
  result("utilization", utilization_seconds, utilization.bins.size(), checksum);

  placement::UtilizationGrid placeable;
  const auto placeable_seconds = measure([&] { placeable = board.utilization(bin_size, false); });
  checksum = 0;
  for (const auto &bin : placeable.bins)
    checksum += bin.placeable_area;
  result("placeable", placeable_seconds, placeable.bins.size(), checksum);

  placement::PinDensityGrid pins;
  const auto pin_seconds = measure([&] { pins = board.pin_density(bin_size); });
  checksum = 0;
  for (const auto &bin : pins.bins)
    checksum += static_cast<double>(bin.pin_count) + bin.area;
  result("pin_density", pin_seconds, pins.bins.size(), checksum);

  placement::CellDensityGrid cells;
  const auto cell_seconds = measure([&] { cells = board.cell_density(bin_size); });
  checksum = 0;
  for (const auto &bin : cells.bins)
    checksum += bin.movable_area + bin.available_area;
  result("cell_density", cell_seconds, cells.bins.size(), checksum);
}

void render(int argc, char **argv) {
  if (argc < 5 || argc > 6)
    usage();

  const auto serializer = placement::make_serializer("binary");
  placement::Board board;
  const auto read_seconds = measure([&] { board = serializer->read(argv[3]); });
  result("binary_read", read_seconds, std::filesystem::file_size(argv[3]));

  placement::RenderOptions opts;
  if (argc == 6)
    opts.bin_size = real_number(argv[5], "bin size");
  const auto renderer = placement::make_renderer(argv[2], opts);
  const auto render_seconds = measure([&] { renderer->render(board, argv[4]); });
  result(std::string("render_") + argv[2], render_seconds, std::filesystem::file_size(argv[4]));
}

} // namespace

int main(int argc, char **argv) {
  try {
    if (argc < 2)
      usage();

    const std::string_view command(argv[1]);
    if (command == "scan")
      scan(argc, argv);
    else if (command == "write")
      write_bytes(argc, argv);
    else if (command == "bookshelf")
      parse_bookshelf(argc, argv);
    else if (command == "lefdef")
      parse_lefdef(argc, argv);
    else if (command == "binary")
      binary(argc, argv);
    else if (command == "analysis")
      analysis(argc, argv);
    else if (command == "render")
      render(argc, argv);
    else
      usage();
    return 0;
  } catch (const std::exception &error) {
    std::cerr << "placement_benchmark: " << error.what() << '\n';
    return 1;
  }
}
