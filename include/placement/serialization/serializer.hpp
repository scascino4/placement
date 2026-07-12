#pragma once

#include "placement/model.hpp"

#include <filesystem>
#include <memory>
#include <string_view>

namespace placement {

class Serializer {
public:
  virtual ~Serializer() = default;

  virtual void write(const Board &board, const std::filesystem::path &output) const = 0;
  [[nodiscard]] virtual Board read(const std::filesystem::path &input) const = 0;
};

[[nodiscard]] std::unique_ptr<Serializer> make_serializer(std::string_view format);

} // namespace placement
