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

void usage(std::ostream &out) {
  out << "Usage: placement_parse [--input-format bookshelf|lefdef] [--lef-file path]... [--placement-file path] "
         "[--serialization-format binary] <input> <output>\n";
}

} // namespace

int main(int argc, char **argv) {
  try {
    std::string in_fmt = "bookshelf";
    std::string ser_fmt = "binary";
    std::optional<std::filesystem::path> pl_override;
    std::vector<std::filesystem::path> lefs;
    int arg = 1;

    while (arg < argc && std::string_view(argv[arg]).starts_with("--")) {
      const std::string_view opt(argv[arg++]);
      if (opt == "--help") {
        usage(std::cout);
        return 0;
      }
      if (arg >= argc)
        throw placement::Error(std::string(opt) + " requires a value");

      if (opt == "--input-format")
        in_fmt = argv[arg++];
      else if (opt == "--lef-file")
        lefs.emplace_back(argv[arg++]);
      else if (opt == "--placement-file")
        pl_override = argv[arg++];
      else if (opt == "--serialization-format")
        ser_fmt = argv[arg++];
      else
        throw placement::Error("unknown option '" + std::string(opt) + "'");
    }

    if (argc - arg != 2) {
      usage(std::cerr);
      return 2;
    }

    const std::filesystem::path in(argv[arg]);
    const std::filesystem::path out(argv[arg + 1]);

    // Normalize and select the input backend at the only decision point.
    std::string norm(in_fmt);
    for (auto &ch : norm)
      ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));

    InputFormat fmt;
    if (norm == "bookshelf")
      fmt = InputFormat::Bookshelf;
    else if (norm == "lefdef" || norm == "lef/def")
      fmt = InputFormat::LefDef;
    else
      throw placement::Error("unsupported input format '" + in_fmt + "'");

    std::unique_ptr<placement::Parser> parser;
    switch (fmt) {
    case InputFormat::Bookshelf:
      if (!lefs.empty())
        throw placement::Error("--lef-file is only valid with --input-format lefdef");
      parser = placement::make_parser(placement::BookshelfParseOptions{.placement_override = pl_override});
      break;

    case InputFormat::LefDef:
      if (pl_override)
        throw placement::Error("--placement-file is only valid with --input-format bookshelf");
      parser = placement::make_parser(placement::LefDefParseOptions{.lef_files = std::move(lefs)});
      break;
    }

    auto serializer = placement::make_serializer(ser_fmt);
    auto board = parser->parse(in);
    serializer->write(board, out);

    std::cout << board.name << ": " << board.cells.size() << " cells, " << board.nets.size() << " nets, " << board.pins.size() << " pins, "
              << board.rows.size() << " rows -> " << out << '\n';

    return 0;
  } catch (const std::exception &error) {
    std::cerr << "placement_parse: " << error.what() << '\n';
    return 1;
  }
}
