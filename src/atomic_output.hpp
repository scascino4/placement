#pragma once

#include "placement/error.hpp"

#include <chrono>
#include <filesystem>
#include <string>
#include <system_error>
#include <utility>

namespace placement::detail {

template <typename Write> void atomic_output(const std::filesystem::path &out, Write &&write) {
  if (!out.parent_path().empty())
    std::filesystem::create_directories(out.parent_path());

  // Keep the temporary file beside the destination so the final rename is
  // atomic on filesystems where crossing directories or mounts would not be.
  const auto tick = std::chrono::steady_clock::now().time_since_epoch().count();
  const std::filesystem::path tmp = out.string() + ".tmp." + std::to_string(tick);
  try {
    std::forward<Write>(write)(tmp);

    std::error_code err;
    std::filesystem::rename(tmp, out, err);
    if (err) {
      // Some platforms cannot replace an existing file with rename.
      std::filesystem::remove(out, err);
      err.clear();
      std::filesystem::rename(tmp, out, err);
    }

    if (err)
      throw Error("cannot replace " + out.string() + ": " + err.message());
  } catch (...) {
    std::error_code ignored;
    std::filesystem::remove(tmp, ignored);
    throw;
  }
}

} // namespace placement::detail
