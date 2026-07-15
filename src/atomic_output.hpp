#pragma once

#include "placement/error.hpp"

#include <chrono>
#include <filesystem>
#include <string>
#include <system_error>
#include <utility>

namespace placement::detail {

[[nodiscard]] inline std::filesystem::path temporary_path(const std::filesystem::path &output) {
  const auto tick = std::chrono::steady_clock::now().time_since_epoch().count();
  return output.string() + ".tmp." + std::to_string(tick);
}

template <typename Write>
void atomic_output(const std::filesystem::path &output, Write &&write) {
  if (!output.parent_path().empty())
    std::filesystem::create_directories(output.parent_path());

  const auto temporary = temporary_path(output);
  try {
    std::forward<Write>(write)(temporary);

    std::error_code error;
    std::filesystem::rename(temporary, output, error);
    if (error) {
      // Some platforms cannot replace an existing file with rename.
      std::filesystem::remove(output, error);
      error.clear();
      std::filesystem::rename(temporary, output, error);
    }
    if (error)
      throw Error("cannot replace " + output.string() + ": " + error.message());
  } catch (...) {
    std::error_code ignored;
    std::filesystem::remove(temporary, ignored);
    throw;
  }
}

} // namespace placement::detail
