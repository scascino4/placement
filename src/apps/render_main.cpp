#include "placement/error.hpp"
#include "placement/rendering/renderer.hpp"
#include "placement/serialization/serializer.hpp"

#include <exception>
#include <filesystem>
#include <iostream>
#include <string>
#include <string_view>

namespace {

void usage(std::ostream &output) {
  output << "Usage: placement_render [--serialization-format binary] "
            "[--output-format svg] <input> <output>\n";
}

} // namespace

int main(int argc, char **argv) {
  try {
    std::string serialization_format = "binary";
    std::string output_format = "svg";
    int argument = 1;

    while (argument < argc && std::string_view(argv[argument]).starts_with("--")) {
      const std::string_view option(argv[argument++]);
      if (option == "--help") {
        usage(std::cout);
        return 0;
      }
      if (argument >= argc) {
        throw placement::Error(std::string(option) + " requires a value");
      }
      if (option == "--serialization-format") {
        serialization_format = argv[argument++];
      } else if (option == "--output-format") {
        output_format = argv[argument++];
      } else {
        throw placement::Error("unknown option '" + std::string(option) + "'");
      }
    }

    if (argc - argument != 2) {
      usage(std::cerr);
      return 2;
    }
    const std::filesystem::path input(argv[argument]);
    const std::filesystem::path output(argv[argument + 1]);
    auto serializer = placement::make_serializer(serialization_format);
    auto renderer = placement::make_renderer(output_format);
    const auto board = serializer->read(input);
    renderer->render(board, output);
    std::cout << board.name << ": rendered " << board.cells.size() << " cells -> " << output
              << '\n';
    return 0;
  } catch (const std::exception &error) {
    std::cerr << "placement_render: " << error.what() << '\n';
    return 1;
  }
}
