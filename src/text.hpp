#pragma once

#include <algorithm>
#include <cctype>
#include <string>
#include <string_view>

namespace placement::detail {

[[nodiscard]] inline std::string lower(std::string_view value) {
  std::string res(value);
  std::ranges::transform(res, res.begin(), [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
  return res;
}

} // namespace placement::detail
