#include "placement/binary.hpp"
#include "placement/error.hpp"
#include "placement/renderer.hpp"

#include <exception>
#include <filesystem>
#include <iostream>
#include <string>
#include <string_view>

namespace {

void usage(std::ostream &output) {
  output << "Usage: placement_render [--output-format svg] <input.placebin> "
            "<output.svg>\n";
}

} // namespace

int main(int argc, char **argv) {
  try {
    std::string format = "svg";
    int argument = 1;
    if (argument < argc && std::string_view(argv[argument]) == "--help") {
      usage(std::cout);
      return 0;
    }
    if (argument < argc && std::string_view(argv[argument]) == "--output-format") {
      if (++argument >= argc) throw placement::Error("--output-format requires a value");
      format = argv[argument++];
    }
    if (argc - argument != 2) {
      usage(std::cerr);
      return 2;
    }
    const std::filesystem::path input(argv[argument]);
    const std::filesystem::path output(argv[argument + 1]);
    const auto board = placement::BinaryBoard::read(input);
    auto renderer = placement::make_renderer(format);
    renderer->render(board, output);
    std::cout << board.name << ": rendered " << board.cells.size() << " cells -> "
              << output << '\n';
    return 0;
  } catch (const std::exception &error) {
    std::cerr << "placement_render: " << error.what() << '\n';
    return 1;
  }
}
