#include "support.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <fstream>
#include <iterator>
#include <optional>
#include <stdexcept>

namespace placement::fuzz {
namespace {

class WorkDir {
public:
  WorkDir() {
    const auto id = std::chrono::steady_clock::now().time_since_epoch().count();
    path_ = std::filesystem::temp_directory_path() / ("placement-fuzz-" + std::to_string(id));
    std::filesystem::create_directories(path_);
  }

  ~WorkDir() {
    std::error_code ignored;
    std::filesystem::remove_all(path_, ignored);
  }

  [[nodiscard]] const std::filesystem::path &path() const { return path_; }

private:
  std::filesystem::path path_;
};

[[nodiscard]] double number(Input input, std::size_t idx, double scale) {
  if (input.empty())
    return 0.0;
  return (static_cast<double>(input[idx % input.size()]) - 127.0) / scale;
}

[[nodiscard]] std::string name(Input input, std::size_t offset) {
  std::string res = "cell";
  if (!input.empty())
    res += std::to_string(static_cast<unsigned int>(input[offset % input.size()] % 4));
  return res;
}

} // namespace

const std::filesystem::path &work_dir() {
  static const WorkDir dir;
  return dir.path();
}

void write_file(const std::filesystem::path &path, std::string_view contents) {
  std::ofstream out(path, std::ios::binary | std::ios::trunc);
  out.write(contents.data(), static_cast<std::streamsize>(contents.size()));
  if (!out)
    throw std::runtime_error("fuzz target could not write " + path.string());
}

std::string read_file(const std::filesystem::path &path) {
  std::ifstream in(path, std::ios::binary);
  if (!in)
    throw std::runtime_error("fuzz target could not read " + path.string());
  return {std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
}

std::string mutate(std::string_view baseline, Input input, std::size_t salt) {
  std::string res(baseline);
  if (input.empty())
    return res;

  const auto mutation_offset = [&](std::size_t idx, std::size_t size) { return (static_cast<std::size_t>(input[idx % input.size()]) + salt) % size; };

  switch ((static_cast<std::size_t>(input[0]) + salt) % 4) {
  case 0:
    for (std::size_t i = 1; i < input.size(); ++i)
      res[mutation_offset(i - 1, res.size())] ^= static_cast<char>(input[i]);
    break;
  case 1: {
    const auto start = mutation_offset(1, res.size() + 1);
    const auto count = std::min(mutation_offset(2, res.size() + 1), res.size() - start);
    res.erase(start, count);
    break;
  }
  case 2: {
    const auto start = mutation_offset(1, res.size() + 1);
    const auto count = std::min(input.size() - 1, std::size_t{256});
    res.insert(start, reinterpret_cast<const char *>(input.data() + 1), count);
    break;
  }
  case 3:
    res.resize(mutation_offset(1, res.size() + 1));
    break;
  }
  return res;
}

Board board_from_input(Input input) {
  Board board;
  board.name = "fuzz<&>";
  const auto width = 2.0 + std::abs(number(input, 0, 32.0));
  const auto height = 2.0 + std::abs(number(input, 1, 32.0));

  board.cells = {{name(input, 2),
                  width,
                  height,
                  CellKind::Movable,
                  false,
                  Location{number(input, 3, 8.0), number(input, 4, 8.0), Orientation::N, PlacementStatus::Movable, std::nullopt, std::nullopt},
                  {}},
                 {name(input, 5),
                  3.0,
                  2.0,
                  CellKind::Terminal,
                  true,
                  Location{4.0 + number(input, 6, 16.0), number(input, 7, 16.0), Orientation::FW, PlacementStatus::Fixed, std::nullopt, std::nullopt},
                  {}}};
  board.rows.push_back({-2.0, 4.0, 1.0, 1.0, Orientation::N, 3, {{-4.0, 16}}});
  board.nets.push_back({"net", 0, 2, {}});
  board.pins = {{0, PinDirection::Input, number(input, 8, 32.0), number(input, 9, 32.0)},
                {1, PinDirection::Output, number(input, 10, 32.0), number(input, 11, 32.0)}};
  return board;
}

} // namespace placement::fuzz
