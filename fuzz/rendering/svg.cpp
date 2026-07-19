#include "../support.hpp"

#include "placement/error.hpp"
#include "placement/rendering/renderer.hpp"

#include <array>

namespace placement::fuzz {

void fuzz_one(Input input) {
  const auto board = board_from_input(input);
  const auto dir = work_dir() / "svg";
  std::filesystem::create_directories(dir);
  constexpr std::array<std::string_view, 4> formats{"svg", "utilization-svg", "pin-density-svg", "cell-density-svg"};

  for (const auto format : formats) {
    try {
      const auto renderer =
          make_renderer(format, {.bin_size = 1.0 + static_cast<double>(input.size() % 8), .dark_mode = !input.empty() && (input.front() & 1) != 0});
      renderer->render(board, dir / (std::string(format) + ".svg"));
    } catch (const Error &) {
    }
  }
}

} // namespace placement::fuzz
