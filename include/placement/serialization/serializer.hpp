#pragma once

#include "placement/model.hpp"

#include <filesystem>
#include <memory>
#include <string_view>

namespace placement {

class Serializer {
public:
  virtual ~Serializer() = default;

  // Serialization is the handoff between the parsing and rendering
  // applications; neither side needs to know the other's concrete backend.
  virtual void write(const Board &board, const std::filesystem::path &out) const = 0;
  [[nodiscard]] virtual Board read(const std::filesystem::path &in) const = 0;
};

[[nodiscard]] std::unique_ptr<Serializer> make_serializer(std::string_view format);

} // namespace placement
