#pragma once

#include "placement/error.hpp"
#include "placement/model.hpp"

#include <cmath>
#include <filesystem>
#include <functional>
#include <initializer_list>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace placement::test {

using Test = std::pair<std::string_view, std::function<void()>>;
using Tests = std::vector<Test>;

class TemporaryDirectory {
public:
  TemporaryDirectory();
  ~TemporaryDirectory();
  TemporaryDirectory(const TemporaryDirectory &) = delete;
  TemporaryDirectory &operator=(const TemporaryDirectory &) = delete;

  [[nodiscard]] const std::filesystem::path &path() const { return path_; }

private:
  std::filesystem::path path_;
};

void write(const std::filesystem::path &path, std::string_view contents);
[[nodiscard]] std::string read(const std::filesystem::path &path);
[[nodiscard]] bool contains_parts(std::string_view contents, std::initializer_list<std::string_view> parts);
[[nodiscard]] std::string_view attribute_value(std::string_view contents, std::string_view name);
void bookshelf_fixture(const std::filesystem::path &dir);
void lefdef_fixture(const std::filesystem::path &dir);
[[nodiscard]] Board parse_bookshelf_fixture(const std::filesystem::path &dir);
[[nodiscard]] Board parse_lefdef_fixture(const std::filesystem::path &dir);

inline void check(bool condition, std::string_view message) {
  if (!condition)
    throw std::runtime_error(std::string(message));
}

[[nodiscard]] inline bool close(double a, double b) { return std::abs(a - b) < 1e-12; }

template <typename Function> void expect_error(Function &&function, std::string_view fragment) {
  try {
    function();
  } catch (const Error &error) {
    check(std::string_view(error.what()).find(fragment) != std::string_view::npos, "error did not contain expected diagnostic");
    return;
  }
  throw std::runtime_error("expected placement::Error");
}

} // namespace placement::test
