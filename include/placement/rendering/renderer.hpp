#pragma once

#include "placement/model.hpp"

#include <filesystem>
#include <memory>
#include <optional>
#include <string_view>

namespace placement {

class Renderer {
public:
  virtual ~Renderer() = default;
  // Renderers consume Board only; they must not depend on the input backend
  // that originally produced it.
  virtual void render(const Board &board, const std::filesystem::path &out) const = 0;
};

struct RenderOptions {
  std::optional<double> bin_size;
  bool dark_mode{};
};

[[nodiscard]] std::unique_ptr<Renderer> make_renderer(std::string_view format, RenderOptions opts = {});

} // namespace placement
