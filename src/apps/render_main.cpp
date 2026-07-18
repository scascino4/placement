#include "placement/error.hpp"
#include "placement/rendering/renderer.hpp"
#include "placement/serialization/serializer.hpp"

#include <exception>
#include <filesystem>
#include <iostream>
#include <optional>
#include <string>
#include <string_view>

namespace {

void usage(std::ostream &out) {
  out << "Usage: placement_render [--serialization-format binary] "
         "[--output-format svg|utilization-svg|pin-density-svg|cell-density-svg] [--bin-size size] [--dark-mode] <input> <output>\n";
}

} // namespace

int main(int argc, char **argv) {
  try {
    std::string ser_fmt = "binary";
    std::string out_fmt = "svg";
    std::optional<double> bin_size;
    bool dark_mode = false;
    int arg = 1;

    while (arg < argc && std::string_view(argv[arg]).starts_with("--")) {
      const std::string_view opt(argv[arg++]);
      if (opt == "--help") {
        usage(std::cout);
        return 0;
      }
      if (opt == "--dark-mode") {
        dark_mode = true;
        continue;
      }
      if (arg >= argc)
        throw placement::Error(std::string(opt) + " requires a value");

      if (opt == "--serialization-format") {
        ser_fmt = argv[arg++];
      } else if (opt == "--output-format") {
        out_fmt = argv[arg++];
      } else if (opt == "--bin-size") {
        const std::string value(argv[arg++]);
        std::size_t consumed = 0;

        try {
          bin_size = std::stod(value, &consumed);
        } catch (const std::exception &) {
          throw placement::Error("invalid bin size '" + value + "'");
        }

        if (consumed != value.size())
          throw placement::Error("invalid bin size '" + value + "'");
      } else {
        throw placement::Error("unknown option '" + std::string(opt) + "'");
      }
    }

    if (argc - arg != 2) {
      usage(std::cerr);
      return 2;
    }

    const std::filesystem::path in(argv[arg]);
    const std::filesystem::path out(argv[arg + 1]);

    auto serializer = placement::make_serializer(ser_fmt);
    auto renderer = placement::make_renderer(out_fmt, {.bin_size = bin_size, .dark_mode = dark_mode});
    const auto board = serializer->read(in);
    renderer->render(board, out);

    std::cout << board.name << ": rendered " << board.cells.size() << " cells -> " << out << '\n';

    return 0;
  } catch (const std::exception &error) {
    std::cerr << "placement_render: " << error.what() << '\n';
    return 1;
  }
}
