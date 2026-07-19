#include "suites.hpp"

#include <exception>
#include <iostream>
#include <iterator>

namespace {

constexpr auto green = "\033[32m";
constexpr auto red = "\033[31m";
constexpr auto reset = "\033[0m";

}  // namespace

int main() {
  placement::test::Tests tests;
  for (auto suite : {placement::test::bookshelf_tests(), placement::test::lefdef_tests(), placement::test::model_tests(),
                     placement::test::binary_tests(), placement::test::svg_tests()})
    tests.insert(tests.end(), std::make_move_iterator(suite.begin()), std::make_move_iterator(suite.end()));

  std::size_t passed = 0;
  for (const auto &[name, test] : tests) {
    try {
      test();
      ++passed;
      std::cout << green << "[PASS]" << reset << ' ' << name << '\n';
    } catch (const std::exception &error) {
      std::cerr << red << "[FAIL]" << reset << ' ' << name << ": " << error.what() << '\n';
    }
  }
  std::cout << passed << '/' << tests.size() << " tests passed\n";
  return passed == tests.size() ? 0 : 1;
}
