#include "suites.hpp"

#include <exception>
#include <iostream>
#include <iterator>

int main() {
  placement::test::Tests tests;
  for (auto suite :
       {placement::test::parsing_tests(), placement::test::model_tests(), placement::test::serialization_tests(), placement::test::rendering_tests()})
    tests.insert(tests.end(), std::make_move_iterator(suite.begin()), std::make_move_iterator(suite.end()));

  std::size_t passed = 0;
  for (const auto &[name, test] : tests) {
    try {
      test();
      ++passed;
      std::cout << "[PASS] " << name << '\n';
    } catch (const std::exception &error) {
      std::cerr << "[FAIL] " << name << ": " << error.what() << '\n';
    }
  }
  std::cout << passed << '/' << tests.size() << " tests passed\n";
  return passed == tests.size() ? 0 : 1;
}
