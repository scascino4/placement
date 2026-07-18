#pragma once

#include "placement/model.hpp"

#include <filesystem>
#include <memory>
#include <optional>
#include <vector>

namespace placement {

class Parser {
public:
  virtual ~Parser() = default;
  [[nodiscard]] virtual Board parse(const std::filesystem::path &in) const = 0;
};

struct BookshelfParseOptions {
  std::optional<std::filesystem::path> placement_override;
};

struct LefDefParseOptions {
  std::vector<std::filesystem::path> lef_files;
};

// The option type selects the backend and prevents backend-specific inputs from
// being mixed in one loosely typed configuration object.
[[nodiscard]] std::unique_ptr<Parser> make_parser(BookshelfParseOptions opts = {});
[[nodiscard]] std::unique_ptr<Parser> make_parser(LefDefParseOptions opts);

} // namespace placement
