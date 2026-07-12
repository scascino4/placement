#pragma once

#include "placement/model.hpp"

#include <filesystem>
#include <memory>
#include <optional>
#include <string_view>

namespace placement {

class Parser {
public:
  virtual ~Parser() = default;
  [[nodiscard]] virtual Board parse(const std::filesystem::path &input) const = 0;
};

struct ParseOptions {
  std::optional<std::filesystem::path> placement_override;
};

[[nodiscard]] std::unique_ptr<Parser> make_parser(std::string_view format, ParseOptions options = {});

} // namespace placement
