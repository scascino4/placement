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
  virtual void render(const Board &board, const std::filesystem::path &output) const = 0;
};

struct RenderOptions {
  std::optional<double> bin_size;
};

[[nodiscard]] std::unique_ptr<Renderer> make_renderer(std::string_view format,
                                                      RenderOptions options = {});

} // namespace placement
