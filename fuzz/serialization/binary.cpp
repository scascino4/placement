#include "../support.hpp"

#include "placement/error.hpp"
#include "placement/serialization/serializer.hpp"

namespace placement::fuzz {
namespace {

[[nodiscard]] const std::string &baseline_bytes() {
  static const std::string bytes = [] {
    const auto dir = work_dir() / "binary";
    std::filesystem::create_directories(dir);
    const auto path = dir / "baseline.placebin";
    make_serializer("binary")->write(board_from_input({}), path);
    return read_file(path);
  }();
  return bytes;
}

} // namespace

void fuzz_one(Input input) {
  const auto dir = work_dir() / "binary";
  std::filesystem::create_directories(dir);
  const auto mutated = dir / "mutated.placebin";
  static const auto serializer = make_serializer("binary");
  write_file(mutated, mutate(baseline_bytes(), input, 17));

  try {
    (void)serializer->read(mutated);
  } catch (const Error &) {
  }
}

} // namespace placement::fuzz
