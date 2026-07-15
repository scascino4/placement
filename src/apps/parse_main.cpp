#include "placement/error.hpp"
#include "placement/parsing/parser.hpp"
#include "placement/serialization/serializer.hpp"

#include <exception>
#include <filesystem>
#include <iostream>
#include <optional>
#include <string>
#include <string_view>

namespace {

void usage(std::ostream &output) {
  output << "Usage: placement_parse [--input-format bookshelf] [--placement-file path] "
            "[--serialization-format binary] <input> <output>\n";
}

} // namespace

int main(int argc, char **argv) {
  try {
    std::string input_format = "bookshelf";
    std::string serialization_format = "binary";
    std::optional<std::filesystem::path> placement_override;
    int arg = 1;

    while (arg < argc && std::string_view(argv[arg]).starts_with("--")) {
      const std::string_view option(argv[arg++]);
      if (option == "--help") {
        usage(std::cout);
        return 0;
      }
      if (arg >= argc)
        throw placement::Error(std::string(option) + " requires a value");

      if (option == "--input-format")
        input_format = argv[arg++];
      else if (option == "--placement-file")
        placement_override = argv[arg++];
      else if (option == "--serialization-format")
        serialization_format = argv[arg++];
      else
        throw placement::Error("unknown option '" + std::string(option) + "'");
    }

    if (argc - arg != 2) {
      usage(std::cerr);
      return 2;
    }

    const std::filesystem::path input(argv[arg]);
    const std::filesystem::path output(argv[arg + 1]);
    auto parser = placement::make_parser(input_format, {.placement_override = placement_override});
    auto serializer = placement::make_serializer(serialization_format);
    auto board = parser->parse(input);
    serializer->write(board, output);
    std::cout << board.name << ": " << board.cells.size() << " cells, " << board.nets.size() << " nets, " << board.pins.size() << " pins, "
              << board.rows.size() << " rows -> " << output << '\n';
    return 0;
  } catch (const std::exception &error) {
    std::cerr << "placement_parse: " << error.what() << '\n';
    return 1;
  }
}
