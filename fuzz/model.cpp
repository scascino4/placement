#include "support.hpp"

#include "placement/error.hpp"

namespace placement::fuzz {

void fuzz_one(Input input) {
  const auto board = board_from_input(input);
  const auto bin_size = 0.25 + static_cast<double>(input.empty() ? 1 : input.front() % 32);

  try {
    for (const auto &cell : board.cells)
      (void)placed_rectangle(cell);
    (void)board.utilization(bin_size);
    (void)board.pin_density(bin_size);
    (void)board.cell_density(bin_size);
  } catch (const Error &) {
  }
}

} // namespace placement::fuzz
