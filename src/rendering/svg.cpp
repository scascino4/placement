#include "placement/rendering/renderer.hpp"

#include "placement/error.hpp"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cmath>
#include <fstream>
#include <iomanip>
#include <limits>
#include <sstream>
#include <string>

namespace placement {
namespace {

[[nodiscard]] std::string lower(std::string_view value) {
  std::string result(value);
  std::ranges::transform(result, result.begin(), [](unsigned char character) { return static_cast<char>(std::tolower(character)); });
  return result;
}

[[nodiscard]] std::string escape(std::string_view value) {
  std::string result;
  result.reserve(value.size());
  for (const auto character : value) {
    switch (character) {
    case '&':
      result += "&amp;";
      break;
    case '<':
      result += "&lt;";
      break;
    case '>':
      result += "&gt;";
      break;
    case '\"':
      result += "&quot;";
      break;
    case '\'':
      result += "&apos;";
      break;
    default:
      result += character;
    }
  }
  return result;
}

struct Rectangle {
  double x{};
  double y{};
  double width{};
  double height{};
};

[[nodiscard]] Rectangle cell_rectangle(const Cell &cell) {
  const auto &location = *cell.location;
  double width = location.width.value_or(cell.width);
  double height = location.height.value_or(cell.height);

  // A quarter-turn changes the axis-aligned footprint. Reflections preserve
  // dimensions, including the reflected quarter-turn orientations FE/FW.
  switch (location.orientation) {
  case Orientation::E:
  case Orientation::W:
  case Orientation::FE:
  case Orientation::FW:
    std::swap(width, height);
    break;
  default:
    break;
  }

  return {location.x, location.y, width, height};
}

struct Bounds {
  double minimum_x{std::numeric_limits<double>::infinity()};
  double minimum_y{std::numeric_limits<double>::infinity()};
  double maximum_x{-std::numeric_limits<double>::infinity()};
  double maximum_y{-std::numeric_limits<double>::infinity()};

  void include(const Rectangle &rectangle) {
    if (!std::isfinite(rectangle.x) || !std::isfinite(rectangle.y) || !std::isfinite(rectangle.width) || !std::isfinite(rectangle.height) ||
        rectangle.width < 0 || rectangle.height < 0)
      throw Error("cannot render non-finite or negative geometry");
    minimum_x = std::min(minimum_x, rectangle.x);
    minimum_y = std::min(minimum_y, rectangle.y);
    maximum_x = std::max(maximum_x, rectangle.x + rectangle.width);
    maximum_y = std::max(maximum_y, rectangle.y + rectangle.height);
  }

  [[nodiscard]] bool empty() const { return !std::isfinite(minimum_x); }
};

enum class CellClass { Movable, Fixed, FixedNonInteracting };

[[nodiscard]] CellClass cell_class(const Cell &cell) {
  if (cell.kind == CellKind::TerminalNonInteracting || cell.location->status == PlacementStatus::FixedNonInteracting)
    return CellClass::FixedNonInteracting;
  if (cell.kind == CellKind::Terminal || cell.location->status == PlacementStatus::Fixed)
    return CellClass::Fixed;
  return CellClass::Movable;
}

void write_paths(std::ostream &output, const Board &board, CellClass wanted, std::string_view css_class) {
  // Combining rectangles into paths keeps SVG size and DOM overhead low. A
  // bounded batch size avoids producing path attributes that are unwieldy for
  // viewers to parse on multi-million-cell designs.
  constexpr std::size_t CELLS_PER_PATH = 10'000;
  std::size_t in_path = 0;

  for (const auto &cell : board.cells) {
    if (!cell.location || cell_class(cell) != wanted)
      continue;

    const auto rectangle = cell_rectangle(cell);
    if (rectangle.width == 0 || rectangle.height == 0)
      continue;

    if (in_path == 0)
      output << "    <path class=\"" << css_class << "\" d=\"";
    output << 'M' << rectangle.x << ' ' << rectangle.y << 'h' << rectangle.width << 'v' << rectangle.height << 'h' << -rectangle.width << 'z';

    ++in_path;
    if (in_path == CELLS_PER_PATH) {
      output << "\"/>\n";
      in_path = 0;
    }
  }

  if (in_path != 0)
    output << "\"/>\n";
}

[[nodiscard]] std::filesystem::path temporary_path(const std::filesystem::path &output) {
  const auto tick = std::chrono::steady_clock::now().time_since_epoch().count();
  return output.string() + ".tmp." + std::to_string(tick);
}

template <typename Write> void write_atomic(const std::filesystem::path &path, Write write) {
  if (!path.parent_path().empty())
    std::filesystem::create_directories(path.parent_path());

  // Write beside the destination so failures never expose a partial SVG.
  const auto temporary = temporary_path(path);
  try {
    std::ofstream output(temporary);
    if (!output)
      throw Error("cannot create " + path.string());

    output << std::setprecision(12);
    write(output);
    output.flush();
    if (!output)
      throw Error("failed while writing " + path.string());
    output.close();

    // Some platforms cannot replace an existing file with rename.
    std::error_code error;
    std::filesystem::rename(temporary, path, error);
    if (error) {
      std::filesystem::remove(path, error);
      error.clear();
      std::filesystem::rename(temporary, path, error);
    }
    if (error)
      throw Error("cannot replace " + path.string() + ": " + error.message());
  } catch (...) {
    std::error_code ignored;
    std::filesystem::remove(temporary, ignored);
    throw;
  }
}

class SvgWriter final : public Renderer {
public:
  void render(const Board &board, const std::filesystem::path &output_path) const override {
    Bounds bounds;
    for (const auto &row : board.rows) {
      for (const auto &subrow : row.subrows)
        bounds.include({subrow.origin, row.coordinate, static_cast<double>(subrow.site_count) * row.site_spacing, row.height});
    }
    for (const auto &cell : board.cells)
      if (cell.location)
        bounds.include(cell_rectangle(cell));
    if (bounds.empty())
      throw Error("cannot render a board without geometry");

    const auto width = std::max(1.0, bounds.maximum_x - bounds.minimum_x);
    const auto height = std::max(1.0, bounds.maximum_y - bounds.minimum_y);
    const auto span = std::max(width, height);
    const auto padding = span * 0.01;
    const auto stroke = span / 6000.0;

    write_atomic(output_path, [&](std::ostream &output) {
      output << "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
             << "<svg xmlns=\"http://www.w3.org/2000/svg\" viewBox=\"" << -padding << ' ' << -padding << ' ' << width + 2 * padding << ' '
             << height + 2 * padding << "\" preserveAspectRatio=\"xMidYMid meet\">\n"
             << "  <title>" << escape(board.name) << " placement</title>\n"
             << "  <desc>" << board.cells.size() << " cells, " << board.rows.size() << " rows, " << board.nets.size() << " nets</desc>\n";

      output << "  <style>\n"
             << "    .background{fill:#f8fafc}.row{fill:#e2e8f0;stroke:#94a3b8;stroke-width:" << stroke
             << "}.movable{fill:#3b82f6;stroke:none}.fixed{fill:#ef4444;stroke:#7f1d1d;stroke-width:" << stroke
             << "}.fixed-ni{fill:#f59e0b;stroke:#78350f;stroke-width:" << stroke << "}\n"
             << "  </style>\n";

      output << "  <rect class=\"background\" x=\"" << -padding << "\" y=\"" << -padding << "\" width=\"" << width + 2 * padding << "\" height=\""
             << height + 2 * padding << "\"/>\n";

      // Placement coordinates use an upward-positive Y axis, while SVG uses
      // a downward-positive one. Translate to the computed bounds, then flip
      // the geometry group without also flipping title or descriptive text.
      output << "  <g transform=\"translate(" << -bounds.minimum_x << ' ' << bounds.maximum_y << ") scale(1 -1)\" shape-rendering=\"crispEdges\">\n";

      for (const auto &row : board.rows) {
        for (const auto &subrow : row.subrows) {
          output << "    <rect class=\"row\" x=\"" << subrow.origin << "\" y=\"" << row.coordinate << "\" width=\""
                 << static_cast<double>(subrow.site_count) * row.site_spacing << "\" height=\"" << row.height << "\"/>\n";
        }
      }

      write_paths(output, board, CellClass::Movable, "movable");
      write_paths(output, board, CellClass::Fixed, "fixed");
      write_paths(output, board, CellClass::FixedNonInteracting, "fixed-ni");

      output << "  </g>\n</svg>\n";
    });
  }
};

[[nodiscard]] std::string utilization_color(double utilization) {
  const auto clamped = std::clamp(utilization, 0.0, 1.0);
  const auto hue = 120.0 * (1.0 - clamped);
  std::ostringstream color;
  color << "hsl(" << std::setprecision(5) << hue << " 72% 48%)";
  return color.str();
}

class UtilizationSvgWriter final : public Renderer {
public:
  explicit UtilizationSvgWriter(RenderOptions options) : options_(options) {}

  void render(const Board &board, const std::filesystem::path &output_path) const override {
    Bounds core;
    for (const auto &row : board.rows) {
      for (const auto &subrow : row.subrows)
        core.include({subrow.origin, row.coordinate, static_cast<double>(subrow.site_count) * row.site_spacing, row.height});
    }
    if (core.empty())
      throw Error("cannot render utilization without a placement region");

    const auto width = core.maximum_x - core.minimum_x;
    const auto height = core.maximum_y - core.minimum_y;
    const auto bin_size = options_.bin_size.value_or(std::max(width, height) / 100.0);
    const auto grid = board.utilization(bin_size);
    const auto span = std::max(width, height);
    const auto padding = span * 0.01;
    const auto stroke = span / 8000.0;

    write_atomic(output_path, [&](std::ostream &output) {
      output << "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
             << "<svg xmlns=\"http://www.w3.org/2000/svg\" viewBox=\"" << -padding << ' ' << -padding << ' ' << width + 2 * padding << ' '
             << height + 2 * padding << "\" preserveAspectRatio=\"xMidYMid meet\">\n"
             << "  <title>" << escape(board.name) << " utilization</title>\n"
             << "  <desc>" << grid.columns << " by " << grid.rows << " bins of size " << grid.bin_size
             << "; green is low utilization, red is 100 percent or greater, gray is not "
                "placeable</desc>\n"
             << "  <style>\n"
             << "    .background{fill:#f8fafc}.bin{stroke:#ffffff;stroke-opacity:.38;stroke-width:" << stroke
             << "}.movable-overlay{fill:#f8fafc;fill-opacity:.42;stroke:none}"
                ".fixed-overlay{fill:#f8fafc;stroke:#1f2937;stroke-width:"
             << stroke << "}.fixed-ni-overlay{fill:#f8fafc;stroke:#334155;stroke-width:" << stroke << "}\n"
             << "  </style>\n"
             << "  <rect class=\"background\" x=\"" << -padding << "\" y=\"" << -padding << "\" width=\"" << width + 2 * padding << "\" height=\""
             << height + 2 * padding << "\"/>\n"
             << "  <g transform=\"translate(" << -core.minimum_x << ' ' << core.maximum_y << ") scale(1 -1)\" shape-rendering=\"crispEdges\">\n";

      for (std::uint64_t row = 0; row < grid.rows; ++row) {
        const auto y = grid.minimum_y + static_cast<double>(row) * grid.bin_size;
        const auto bin_height = std::min(grid.bin_size, grid.maximum_y - y);
        for (std::uint64_t column = 0; column < grid.columns; ++column) {
          const auto x = grid.minimum_x + static_cast<double>(column) * grid.bin_size;
          const auto bin_width = std::min(grid.bin_size, grid.maximum_x - x);
          const auto utilization = grid.at(column, row).utilization();
          output << "    <rect class=\"bin\" x=\"" << x << "\" y=\"" << y << "\" width=\"" << bin_width << "\" height=\"" << bin_height
                 << "\" fill=\"" << (utilization ? utilization_color(*utilization) : std::string("#d1d5db")) << "\"/>\n";
        }
      }

      write_paths(output, board, CellClass::Movable, "movable-overlay");
      write_paths(output, board, CellClass::Fixed, "fixed-overlay");
      write_paths(output, board, CellClass::FixedNonInteracting, "fixed-ni-overlay");
      output << "  </g>\n</svg>\n";
    });
  }

private:
  RenderOptions options_;
};

class PinDensitySvgWriter final : public Renderer {
public:
  explicit PinDensitySvgWriter(RenderOptions options) : options_(options) {}

  void render(const Board &board, const std::filesystem::path &output_path) const override {
    Bounds core;
    for (const auto &row : board.rows) {
      for (const auto &subrow : row.subrows)
        core.include({subrow.origin, row.coordinate, static_cast<double>(subrow.site_count) * row.site_spacing, row.height});
    }
    if (core.empty())
      throw Error("cannot render pin density without a placement region");

    const auto width = core.maximum_x - core.minimum_x;
    const auto height = core.maximum_y - core.minimum_y;
    const auto bin_size = options_.bin_size.value_or(std::max(width, height) / 100.0);
    const auto grid = board.pin_density(bin_size);
    const auto utilization_grid = board.utilization(bin_size);
    std::vector<double> nonzero_densities;
    nonzero_densities.reserve(grid.bins.size());
    for (const auto &bin : grid.bins)
      if (bin.pin_count != 0)
        nonzero_densities.push_back(bin.density());
    std::ranges::sort(nonzero_densities);
    double color_ceiling = 1.0;
    if (!nonzero_densities.empty()) {
      const auto percentile_index = static_cast<std::size_t>(std::ceil(0.95 * static_cast<double>(nonzero_densities.size()))) - 1;
      color_ceiling = nonzero_densities[percentile_index];
    }

    const auto span = std::max(width, height);
    const auto padding = span * 0.01;
    const auto stroke = span / 8000.0;
    write_atomic(output_path, [&](std::ostream &output) {
      output << "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
             << "<svg xmlns=\"http://www.w3.org/2000/svg\" viewBox=\"" << -padding << ' ' << -padding << ' ' << width + 2 * padding << ' '
             << height + 2 * padding << "\" preserveAspectRatio=\"xMidYMid meet\">\n"
             << "  <title>" << escape(board.name) << " pin density</title>\n"
             << "  <desc>" << grid.columns << " by " << grid.rows << " bins of size " << grid.bin_size << "; color saturates at the 95th percentile, "
             << color_ceiling << " pins per square placement unit</desc>\n"
             << "  <style>\n"
             << "    .background{fill:#f8fafc}.bin{stroke:#ffffff;stroke-opacity:.38;stroke-width:" << stroke
             << "}.movable-overlay{fill:#f8fafc;fill-opacity:.42;stroke:none}"
                ".fixed-overlay{fill:#f8fafc;stroke:#1f2937;stroke-width:"
             << stroke << "}.fixed-ni-overlay{fill:#f8fafc;stroke:#334155;stroke-width:" << stroke << "}\n"
             << "  </style>\n"
             << "  <rect class=\"background\" x=\"" << -padding << "\" y=\"" << -padding << "\" width=\"" << width + 2 * padding << "\" height=\""
             << height + 2 * padding << "\"/>\n"
             << "  <g transform=\"translate(" << -core.minimum_x << ' ' << core.maximum_y << ") scale(1 -1)\" shape-rendering=\"crispEdges\">\n";

      for (std::uint64_t row = 0; row < grid.rows; ++row) {
        const auto y = grid.minimum_y + static_cast<double>(row) * grid.bin_size;
        const auto bin_height = std::min(grid.bin_size, grid.maximum_y - y);
        for (std::uint64_t column = 0; column < grid.columns; ++column) {
          const auto x = grid.minimum_x + static_cast<double>(column) * grid.bin_size;
          const auto bin_width = std::min(grid.bin_size, grid.maximum_x - x);
          const auto &bin = grid.at(column, row);
          const auto placeable = utilization_grid.at(column, row).utilization().has_value();
          output << "    <rect class=\"bin\" x=\"" << x << "\" y=\"" << y << "\" width=\"" << bin_width << "\" height=\"" << bin_height
                 << "\" fill=\"" << (placeable ? utilization_color(bin.density() / color_ceiling) : std::string("#d1d5db")) << "\"><title>"
                 << bin.pin_count << " pins; density " << bin.density() << "</title></rect>\n";
        }
      }
      write_paths(output, board, CellClass::Movable, "movable-overlay");
      write_paths(output, board, CellClass::Fixed, "fixed-overlay");
      write_paths(output, board, CellClass::FixedNonInteracting, "fixed-ni-overlay");
      output << "  </g>\n</svg>\n";
    });
  }

private:
  RenderOptions options_;
};

} // namespace

std::unique_ptr<Renderer> make_renderer(std::string_view format, RenderOptions options) {
  if (lower(format) == "svg")
    return std::make_unique<SvgWriter>();
  if (lower(format) == "utilization-svg")
    return std::make_unique<UtilizationSvgWriter>(options);
  if (lower(format) == "pin-density-svg")
    return std::make_unique<PinDensitySvgWriter>(options);
  throw Error("unsupported output format '" + std::string(format) + "'");
}

} // namespace placement
