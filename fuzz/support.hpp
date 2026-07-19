#pragma once

#include "placement/model.hpp"

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <span>
#include <string>
#include <string_view>

namespace placement::fuzz {

using Input = std::span<const std::uint8_t>;

[[nodiscard]] const std::filesystem::path &work_dir();
void write_file(const std::filesystem::path &path, std::string_view contents);
[[nodiscard]] std::string read_file(const std::filesystem::path &path);
[[nodiscard]] std::string mutate(std::string_view baseline, Input input, std::size_t salt);
[[nodiscard]] Board board_from_input(Input input);

void fuzz_one(Input input);

} // namespace placement::fuzz
