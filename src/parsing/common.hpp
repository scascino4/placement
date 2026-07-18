#pragma once

#include "../text.hpp"
#include "placement/error.hpp"
#include "placement/model.hpp"

#include <array>
#include <bit>
#include <charconv>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <limits>
#include <optional>
#include <string>
#include <string_view>
#include <type_traits>
#include <vector>

namespace placement::parsing_detail {

using detail::lower;

[[nodiscard]] inline bool ascii_iequal(std::string_view lhs, std::string_view rhs) {
  if (lhs.size() != rhs.size())
    return false;

  for (std::size_t i = 0; i < lhs.size(); ++i) {
    const auto left = static_cast<unsigned char>(lhs[i]);
    const auto right = static_cast<unsigned char>(rhs[i]);
    const auto folded_left = left >= 'A' && left <= 'Z' ? static_cast<unsigned char>(left + ('a' - 'A')) : left;
    const auto folded_right = right >= 'A' && right <= 'Z' ? static_cast<unsigned char>(right + ('a' - 'A')) : right;
    if (folded_left != folded_right)
      return false;
  }
  return true;
}

[[nodiscard]] inline std::optional<double> simple_decimal(std::string_view token) {
  // Most benchmark geometry uses short decimal notation. Parse that common
  // case with bounded integer arithmetic and leave scientific notation or
  // unusually precise values to std::from_chars in number().
  constexpr std::array<double, 16> powers_of_ten{1.0,
                                                 10.0,
                                                 100.0,
                                                 1'000.0,
                                                 10'000.0,
                                                 100'000.0,
                                                 1'000'000.0,
                                                 10'000'000.0,
                                                 100'000'000.0,
                                                 1'000'000'000.0,
                                                 10'000'000'000.0,
                                                 100'000'000'000.0,
                                                 1'000'000'000'000.0,
                                                 10'000'000'000'000.0,
                                                 100'000'000'000'000.0,
                                                 1'000'000'000'000'000.0};

  bool negative = false;
  if (!token.empty() && token.front() == '-') {
    negative = true;
    token.remove_prefix(1);
  }
  if (token.empty())
    return std::nullopt;

  std::uint64_t significand = 0;
  std::size_t digits = 0;
  std::size_t frac_digits = 0;
  bool decimal_point = false;
  for (const char ch : token) {
    if (ch == '.' && !decimal_point) {
      decimal_point = true;
      continue;
    }
    if (ch < '0' || ch > '9' || digits == 15)
      return std::nullopt;

    significand = significand * 10 + static_cast<std::uint64_t>(ch - '0');
    ++digits;
    if (decimal_point)
      ++frac_digits;
  }
  if (digits == 0)
    return std::nullopt;

  const double value = static_cast<double>(significand) / powers_of_ten[frac_digits];
  return negative ? -value : value;
}

template <typename T, typename Source> [[nodiscard]] T number(std::string_view token, const Source &source, std::string_view description) {
  T value{};
  if constexpr (std::is_same_v<T, double>) {
    if (const auto parsed = simple_decimal(token))
      return *parsed;
  }

  const auto *begin = token.data();
  const auto *end = begin + token.size();
  const auto [ptr, error] = std::from_chars(begin, end, value);
  if (error != std::errc{} || ptr != end)
    source.fail("invalid " + std::string(description) + " '" + std::string(token) + "'");

  if constexpr (std::is_floating_point_v<T>) {
    if (!std::isfinite(value))
      source.fail("non-finite " + std::string(description) + " is not allowed");
  }
  return value;
}

template <typename Source> [[nodiscard]] Orientation orientation(std::string_view value, const Source &source) {
  if (ascii_iequal(value, "n") || value == "1")
    return Orientation::N;
  if (ascii_iequal(value, "e"))
    return Orientation::E;
  if (ascii_iequal(value, "s"))
    return Orientation::S;
  if (ascii_iequal(value, "w"))
    return Orientation::W;
  if (ascii_iequal(value, "fn"))
    return Orientation::FN;
  if (ascii_iequal(value, "fe"))
    return Orientation::FE;
  if (ascii_iequal(value, "fs"))
    return Orientation::FS;
  if (ascii_iequal(value, "fw"))
    return Orientation::FW;
  source.fail("unknown orientation '" + std::string(value) + "'");
}

template <typename Source> [[nodiscard]] PinDirection pin_direction(std::string_view value, const Source &source) {
  if (ascii_iequal(value, "i") || ascii_iequal(value, "input"))
    return PinDirection::Input;
  if (ascii_iequal(value, "o") || ascii_iequal(value, "output"))
    return PinDirection::Output;
  if (ascii_iequal(value, "b") || ascii_iequal(value, "inout"))
    return PinDirection::Bidirectional;
  if (ascii_iequal(value, "u") || ascii_iequal(value, "unknown") || ascii_iequal(value, "feedthru"))
    return PinDirection::Unknown;
  source.fail("unknown pin direction '" + std::string(value) + "'");
}

template <typename Record> struct NameTraits;

template <> struct NameTraits<Cell> {
  static constexpr std::string_view kind = "cell";
};

template <> struct NameTraits<Net> {
  static constexpr std::string_view kind = "net";
};

template <typename Record> class NameIndex {
public:
  NameIndex(const std::vector<Record> &records, const std::filesystem::path &path) : records_(records) {
    if (records.size() >= EMPTY)
      throw Error(path.string() + ": " + std::string(NameTraits<Record>::kind) + " count exceeds placement model limit");

    // Open addressing stores one compact integer per slot and avoids one heap
    // allocation per name on multi-million-record designs.
    const auto required = records.size() + records.size() / 2 + 1;
    slots_.assign(std::bit_ceil(required), EMPTY);
    for (std::uint32_t i = 0; i < records.size(); ++i) {
      auto slot = initial_slot(records[i].name);
      while (slots_[slot] != EMPTY) {
        if (records_[slots_[slot]].name == records[i].name)
          throw Error(path.string() + ": duplicate " + std::string(NameTraits<Record>::kind) + " name '" + records[i].name + "'");
        slot = (slot + 1) & (slots_.size() - 1);
      }
      slots_[slot] = i;
    }
  }

  [[nodiscard]] std::optional<std::uint32_t> find(std::string_view name) const {
    auto slot = initial_slot(name);
    while (slots_[slot] != EMPTY) {
      const auto idx = slots_[slot];
      if (records_[idx].name == name)
        return idx;
      slot = (slot + 1) & (slots_.size() - 1);
    }
    return std::nullopt;
  }

private:
  static constexpr std::uint32_t EMPTY = std::numeric_limits<std::uint32_t>::max();

  [[nodiscard]] std::size_t initial_slot(std::string_view name) const { return std::hash<std::string_view>{}(name) & (slots_.size() - 1); }

  const std::vector<Record> &records_;
  std::vector<std::uint32_t> slots_;
};

template <typename Record> void validate_unique_names(const std::vector<Record> &records, const std::filesystem::path &path) {
  (void)NameIndex<Record>(records, path);
}

} // namespace placement::parsing_detail
