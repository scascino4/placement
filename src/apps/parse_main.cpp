#include "placement/error.hpp"
#include "placement/parsing/parser.hpp"
#include "placement/serialization/serializer.hpp"

#include <cctype>
#include <exception>
#include <filesystem>
#include <iostream>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace {

enum class InputFormat { Bookshelf, LefDef };

void usage(std::ostream &output) {
  output << "Usage: placement_parse [--input-format bookshelf|lefdef] [--lef-file path]... [--placement-file path] "
            "[--serialization-format binary] <input> <output>\n";
}

[[nodiscard]] InputFormat parse_input_format(std::string_view value) {
  std::string normalized(value);
  for (auto &character : normalized)
    character = static_cast<char>(std::tolower(static_cast<unsigned char>(character)));

  if (normalized == "bookshelf")
    return InputFormat::Bookshelf;
  if (normalized == "lefdef" || normalized == "lef/def")
    return InputFormat::LefDef;
  throw placement::Error("unsupported input format '" + std::string(value) + "'");
}

} // namespace

int main(int argc, char **argv) {
  try {
    std::string input_format = "bookshelf";
    std::string serialization_format = "binary";
    std::optional<std::filesystem::path> placement_override;
    std::vector<std::filesystem::path> lef_files;
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
      else if (option == "--lef-file")
        lef_files.emplace_back(argv[arg++]);
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

    std::unique_ptr<placement::Parser> parser;
    switch (parse_input_format(input_format)) {
    case InputFormat::Bookshelf:
      if (!lef_files.empty())
        throw placement::Error("--lef-file is only valid with --input-format lefdef");
      parser = placement::make_parser(placement::BookshelfParseOptions{.placement_override = placement_override});
      break;

    case InputFormat::LefDef:
      if (placement_override)
        throw placement::Error("--placement-file is only valid with --input-format bookshelf");
      parser = placement::make_parser(placement::LefDefParseOptions{.lef_files = std::move(lef_files)});
      break;
    }

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
