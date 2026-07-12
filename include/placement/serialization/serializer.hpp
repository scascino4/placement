#pragma once

#include "placement/model.hpp"

#include <filesystem>

namespace placement {

class BinaryBoard {
public:
  static void write(const Board &board, const std::filesystem::path &output);
  [[nodiscard]] static Board read(const std::filesystem::path &input);
};

} // namespace placement
