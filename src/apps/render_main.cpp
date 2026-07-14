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

void usage(std::ostream &output) {
  output << "Usage: placement_render [--serialization-format binary] "
            "[--output-format svg|utilization-svg|pin-density-svg|cell-density-svg] [--bin-size size] [--dark-mode] <input> <output>\n";
}

} // namespace

int main(int argc, char **argv) {
  try {
    std::string serialization_format = "binary";
    std::string output_format = "svg";
    std::optional<double> bin_size;
    bool dark_mode = false;
    int arg = 1;

    while (arg < argc && std::string_view(argv[arg]).starts_with("--")) {
      const std::string_view option(argv[arg++]);
      if (option == "--help") {
        usage(std::cout);
        return 0;
      }
      if (option == "--dark-mode") {
        dark_mode = true;
        continue;
      }
      if (arg >= argc)
        throw placement::Error(std::string(option) + " requires a value");

      if (option == "--serialization-format") {
        serialization_format = argv[arg++];
      } else if (option == "--output-format") {
        output_format = argv[arg++];
      } else if (option == "--bin-size") {
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
        throw placement::Error("unknown option '" + std::string(option) + "'");
      }
    }

    if (argc - arg != 2) {
      usage(std::cerr);
      return 2;
    }

    const std::filesystem::path input(argv[arg]);
    const std::filesystem::path output(argv[arg + 1]);
    auto serializer = placement::make_serializer(serialization_format);
    auto renderer = placement::make_renderer(output_format, {.bin_size = bin_size, .dark_mode = dark_mode});
    const auto board = serializer->read(input);
    renderer->render(board, output);
    std::cout << board.name << ": rendered " << board.cells.size() << " cells -> " << output << '\n';
    return 0;
  } catch (const std::exception &error) {
    std::cerr << "placement_render: " << error.what() << '\n';
    return 1;
  }
}
