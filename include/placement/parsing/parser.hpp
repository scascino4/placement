#pragma once

#include "placement/model.hpp"

#include <filesystem>
#include <memory>
#include <string_view>

namespace placement {

class Parser {
public:
  virtual ~Parser() = default;
  [[nodiscard]] virtual Board parse(const std::filesystem::path &input) const = 0;
};

[[nodiscard]] std::unique_ptr<Parser> make_parser(std::string_view format);

} // namespace placement
