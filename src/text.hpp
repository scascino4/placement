#pragma once

#include <algorithm>
#include <cctype>
#include <string>
#include <string_view>

namespace placement::detail {

[[nodiscard]] inline std::string lower(std::string_view value) {
  std::string result(value);
  std::ranges::transform(result, result.begin(), [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
  return result;
}

} // namespace placement::detail
