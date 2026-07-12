#include "placement/binary.hpp"
#include "placement/error.hpp"
#include "placement/parser.hpp"

#include <exception>
#include <filesystem>
#include <iostream>
#include <string>
#include <string_view>

namespace {

void usage(std::ostream &output) {
  output << "Usage: placement_parse [--input-format bookshelf] <input.aux> "
            "<output.placebin>\n";
}

} // namespace

int main(int argc, char **argv) {
  try {
    std::string format = "bookshelf";
    int argument = 1;
    if (argument < argc && std::string_view(argv[argument]) == "--help") {
      usage(std::cout);
      return 0;
    }
    if (argument < argc && std::string_view(argv[argument]) == "--input-format") {
      if (++argument >= argc) throw placement::Error("--input-format requires a value");
      format = argv[argument++];
    }
    if (argc - argument != 2) {
      usage(std::cerr);
      return 2;
    }
    const std::filesystem::path input(argv[argument]);
    const std::filesystem::path output(argv[argument + 1]);
    auto parser = placement::make_parser(format);
    auto board = parser->parse(input);
    placement::BinaryBoard::write(board, output);
    std::cout << board.name << ": " << board.cells.size() << " cells, "
              << board.nets.size() << " nets, " << board.pins.size() << " pins, "
              << board.rows.size() << " rows -> " << output << '\n';
    return 0;
  } catch (const std::exception &error) {
    std::cerr << "placement_parse: " << error.what() << '\n';
    return 1;
  }
}
