#include "placement/rendering/renderer.hpp"

#include "../atomic_output.hpp"
#include "placement/error.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <fstream>
#include <iomanip>
#include <limits>
#include <sstream>
#include <string>

namespace placement {
namespace {

constexpr std::string_view background_color(bool dark_mode) { return dark_mode ? "#D3D3D3" : "#2C2C2C"; }

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

struct Bounds {
  double min_x{std::numeric_limits<double>::infinity()};
  double min_y{std::numeric_limits<double>::infinity()};
  double max_x{-std::numeric_limits<double>::infinity()};
  double max_y{-std::numeric_limits<double>::infinity()};

  void include(const PlacedRectangle &rect) {
    if (!std::isfinite(rect.x) || !std::isfinite(rect.y) || !std::isfinite(rect.width) || !std::isfinite(rect.height) || rect.width < 0 ||
        rect.height < 0)
      throw Error("cannot render non-finite or negative geometry");

    min_x = std::min(min_x, rect.x);
    min_y = std::min(min_y, rect.y);
    max_x = std::max(max_x, rect.right());
    max_y = std::max(max_y, rect.top());
  }

  [[nodiscard]] bool empty() const { return !std::isfinite(min_x); }
};

enum class CellClass { Movable, Macro, Fixed, FixedNonInteracting };

[[nodiscard]] CellClass cell_class(const Cell &cell) {
  if (cell.macro)
    return CellClass::Macro;
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

    const auto rect = placed_rectangle(cell);
    if (rect.width == 0 || rect.height == 0)
      continue;

    if (in_path == 0)
      output << "    <path class=\"" << css_class << "\" d=\"";
    output << 'M' << rect.x << ' ' << rect.y << 'h' << rect.width << 'v' << rect.height << 'h' << -rect.width << 'z';

    ++in_path;
    if (in_path == CELLS_PER_PATH) {
      output << "\"/>\n";
      in_path = 0;
    }
  }

  if (in_path != 0)
    output << "\"/>\n";
}

template <typename Write> void write_atomic(const std::filesystem::path &path, Write write) {
  detail::atomic_output(path, [&](const auto &temporary) {
    std::ofstream output(temporary);
    if (!output)
      throw Error("cannot create " + path.string());

    output << std::setprecision(12);
    write(output);
    output.flush();
    if (!output)
      throw Error("failed while writing " + path.string());
  });
}

class SvgWriter final : public Renderer {
public:
  explicit SvgWriter(RenderOptions options) : options_(options) {}

  void render(const Board &board, const std::filesystem::path &output_path) const override {
    Bounds bounds;
    for (const auto &row : board.rows) {
      for (const auto &subrow : row.subrows)
        bounds.include({subrow.origin, row.coordinate, static_cast<double>(subrow.site_count) * row.site_spacing, row.height});
    }
    for (const auto &cell : board.cells)
      if (cell.location)
        bounds.include(placed_rectangle(cell));
    if (bounds.empty())
      throw Error("cannot render a board without geometry");

    const auto width = std::max(1.0, bounds.max_x - bounds.min_x);
    const auto height = std::max(1.0, bounds.max_y - bounds.min_y);
    const auto span = std::max(width, height);
    const auto padding = span * 0.01;
    const auto stroke = span / 6000.0;

    write_atomic(output_path, [&](std::ostream &output) {
      output << "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
             << "<svg xmlns=\"http://www.w3.org/2000/svg\" viewBox=\"" << -padding << ' ' << -padding << ' ' << width + 2 * padding << ' '
             << height + 2 * padding << "\" preserveAspectRatio=\"xMidYMid meet\">\n"
             << "  <title>" << escape(board.name) << " placement</title>\n"
             << "  <desc>" << board.cells.size() << " cells, " << board.rows.size() << " rows, " << board.nets.size() << " nets</desc>\n";

      output << "  <style>\n";
      if (options_.dark_mode)
        output << "    .background{fill:" << background_color(options_.dark_mode) << "}.row{fill:#1e293b;stroke:#64748b;stroke-width:" << stroke
               << "}.movable{fill:#60a5fa;stroke:none}.macro{fill:#ffffff;stroke:#cbd5e1;stroke-width:" << stroke
               << "}.fixed{fill:#ffffff;stroke:#ffffff;stroke-width:" << stroke << "}.fixed-ni{fill:#fbbf24;stroke:#fde68a;stroke-width:" << stroke
               << "}\n";
      else
        output << "    .background{fill:" << background_color(options_.dark_mode) << "}.row{fill:#e2e8f0;stroke:#94a3b8;stroke-width:" << stroke
               << "}.movable{fill:#3b82f6;stroke:none}.macro{fill:#ffffff;stroke:#1f2937;stroke-width:" << stroke
               << "}.fixed{fill:#ffffff;stroke:#ffffff;stroke-width:" << stroke << "}.fixed-ni{fill:#f59e0b;stroke:#78350f;stroke-width:" << stroke
               << "}\n";
      output << "  </style>\n";

      output << "  <rect class=\"background\" x=\"" << -padding << "\" y=\"" << -padding << "\" width=\"" << width + 2 * padding << "\" height=\""
             << height + 2 * padding << "\"/>\n";

      // Placement coordinates use an upward-positive Y axis, while SVG uses
      // a downward-positive one. Translate to the computed bounds, then flip
      // the geometry group without also flipping title or descriptive text.
      output << "  <g transform=\"translate(" << -bounds.min_x << ' ' << bounds.max_y << ") scale(1 -1)\" shape-rendering=\"crispEdges\">\n";

      for (const auto &row : board.rows) {
        for (const auto &subrow : row.subrows) {
          output << "    <rect class=\"row\" x=\"" << subrow.origin << "\" y=\"" << row.coordinate << "\" width=\""
                 << static_cast<double>(subrow.site_count) * row.site_spacing << "\" height=\"" << row.height << "\"/>\n";
        }
      }

      write_paths(output, board, CellClass::Movable, "movable");
      write_paths(output, board, CellClass::Macro, "macro");
      write_paths(output, board, CellClass::Fixed, "fixed");
      write_paths(output, board, CellClass::FixedNonInteracting, "fixed-ni");

      output << "  </g>\n</svg>\n";
    });
  }

private:
  RenderOptions options_;
};

[[nodiscard]] std::string utilization_color(double utilization, bool dark_mode) {
  const auto clamped = std::clamp(utilization, 0.0, 1.0);
  const auto hue = 120.0 * (1.0 - clamped);
  std::ostringstream color;
  color << "hsl(" << std::setprecision(5) << hue << (dark_mode ? " 78% 56%)" : " 72% 48%)");
  return color.str();
}

struct DensityLayout {
  Bounds core;
  double width{};
  double height{};
  double bin_size{};
  double padding{};
  double stroke{};
};

[[nodiscard]] DensityLayout density_layout(const Board &board, const RenderOptions &options, std::string_view kind) {
  Bounds core;
  for (const auto &row : board.rows) {
    for (const auto &subrow : row.subrows)
      core.include({subrow.origin, row.coordinate, static_cast<double>(subrow.site_count) * row.site_spacing, row.height});
  }
  if (core.empty())
    throw Error("cannot render " + std::string(kind) + " without a placement region");

  const auto width = core.max_x - core.min_x;
  const auto height = core.max_y - core.min_y;
  const auto span = std::max(width, height);
  return {core, width, height, options.bin_size.value_or(span / 100.0), span * 0.01, span / 8000.0};
}

template <typename Grid, typename Function> void write_grid_bins(std::ostream &output, const Grid &grid, Function write_bin) {
  for (std::uint64_t row = 0; row < grid.rows; ++row) {
    const auto y = grid.min_y + static_cast<double>(row) * grid.bin_size;
    const auto height = std::min(grid.bin_size, grid.max_y - y);
    for (std::uint64_t column = 0; column < grid.columns; ++column) {
      const auto x = grid.min_x + static_cast<double>(column) * grid.bin_size;
      const auto width = std::min(grid.bin_size, grid.max_x - x);
      write_bin(output, grid.at(column, row), x, y, width, height, column, row);
    }
  }
}

template <typename Description, typename Bins>
void write_density_svg(const std::filesystem::path &path, const Board &board, const RenderOptions &options, const DensityLayout &layout,
                       std::string_view kind, Description description, Bins bins, bool movable_overlay) {
  write_atomic(path, [&](std::ostream &output) {
    const auto background = background_color(options.dark_mode);
    const auto surface = options.dark_mode ? "#0f172a" : "#f8fafc";
    const auto grid_stroke = options.dark_mode ? "#0f172a" : "#ffffff";
    const auto fixed_stroke = options.dark_mode ? "#cbd5e1" : "#1f2937";
    const auto fixed_ni_stroke = options.dark_mode ? "#94a3b8" : "#334155";

    output << "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
           << "<svg xmlns=\"http://www.w3.org/2000/svg\" viewBox=\"" << -layout.padding << ' ' << -layout.padding << ' '
           << layout.width + 2 * layout.padding << ' ' << layout.height + 2 * layout.padding << "\" preserveAspectRatio=\"xMidYMid meet\">\n"
           << "  <title>" << escape(board.name) << ' ' << kind << "</title>\n"
           << "  <desc>";
    description(output);
    output << "</desc>\n"
           << "  <style>\n"
           << "    .background{fill:" << background << "}.bin{stroke:" << grid_stroke << ";stroke-opacity:.38;stroke-width:" << layout.stroke << '}';
    if (movable_overlay)
      output << ".movable-overlay{fill:" << surface << ";fill-opacity:.42;stroke:none}";
    output << ".macro-overlay{fill:" << surface << ";stroke:" << fixed_stroke << ";stroke-width:" << layout.stroke << "}"
           << ".fixed-overlay{fill:" << surface << ";stroke:" << fixed_stroke << ";stroke-width:" << layout.stroke
           << "}.fixed-ni-overlay{fill:" << surface << ";stroke:" << fixed_ni_stroke << ";stroke-width:" << layout.stroke << "}\n"
           << "  </style>\n"
           << "  <rect class=\"background\" x=\"" << -layout.padding << "\" y=\"" << -layout.padding << "\" width=\""
           << layout.width + 2 * layout.padding << "\" height=\"" << layout.height + 2 * layout.padding << "\"/>\n"
           << "  <g transform=\"translate(" << -layout.core.min_x << ' ' << layout.core.max_y << ") scale(1 -1)\" shape-rendering=\"crispEdges\">\n";

    bins(output);
    if (movable_overlay)
      write_paths(output, board, CellClass::Movable, "movable-overlay");
    write_paths(output, board, CellClass::Macro, "macro-overlay");
    write_paths(output, board, CellClass::Fixed, "fixed-overlay");
    write_paths(output, board, CellClass::FixedNonInteracting, "fixed-ni-overlay");
    output << "  </g>\n</svg>\n";
  });
}

class UtilizationSvgWriter final : public Renderer {
public:
  explicit UtilizationSvgWriter(RenderOptions options) : options_(options) {}

  void render(const Board &board, const std::filesystem::path &output_path) const override {
    const auto layout = density_layout(board, options_, "utilization");
    const auto grid = board.utilization(layout.bin_size);
    const auto unavailable = options_.dark_mode ? "#374151" : "#d1d5db";
    write_density_svg(
        output_path, board, options_, layout, "utilization",
        [&](std::ostream &output) {
          output << grid.columns << " by " << grid.rows << " bins of size " << grid.bin_size
                 << "; green is low utilization, red is 100 percent or greater, gray is not placeable";
        },
        [&](std::ostream &output) {
          write_grid_bins(
              output, grid,
              [&](std::ostream &stream, const UtilizationBin &bin, double x, double y, double width, double height, std::uint64_t, std::uint64_t) {
                const auto utilization = bin.utilization();
                stream << "    <rect class=\"bin\" x=\"" << x << "\" y=\"" << y << "\" width=\"" << width << "\" height=\"" << height << "\" fill=\""
                       << (utilization ? utilization_color(*utilization, options_.dark_mode) : unavailable) << "\"/>\n";
              });
        },
        true);
  }

private:
  RenderOptions options_;
};

class PinDensitySvgWriter final : public Renderer {
public:
  explicit PinDensitySvgWriter(RenderOptions options) : options_(options) {}

  void render(const Board &board, const std::filesystem::path &output_path) const override {
    const auto layout = density_layout(board, options_, "pin density");
    const auto grid = board.pin_density(layout.bin_size);
    const auto utilization_grid = board.utilization(layout.bin_size);

    std::vector<double> nonzero_densities;
    nonzero_densities.reserve(grid.bins.size());
    for (const auto &bin : grid.bins)
      if (bin.pin_count != 0)
        nonzero_densities.push_back(bin.density());
    std::sort(nonzero_densities.begin(), nonzero_densities.end());

    double color_ceiling = 1.0;
    if (!nonzero_densities.empty()) {
      const auto percentile_index = static_cast<std::size_t>(std::ceil(0.95 * static_cast<double>(nonzero_densities.size()))) - 1;
      color_ceiling = nonzero_densities[percentile_index];
    }

    const auto unavailable = options_.dark_mode ? "#374151" : "#d1d5db";
    write_density_svg(
        output_path, board, options_, layout, "pin density",
        [&](std::ostream &output) {
          output << grid.columns << " by " << grid.rows << " bins of size " << grid.bin_size << "; color saturates at the 95th percentile, "
                 << color_ceiling << " pins per square placement unit";
        },
        [&](std::ostream &output) {
          write_grid_bins(output, grid,
                          [&](std::ostream &stream, const PinDensityBin &bin, double x, double y, double width, double height, std::uint64_t column,
                              std::uint64_t row) {
                            const auto placeable = utilization_grid.at(column, row).utilization().has_value();
                            stream << "    <rect class=\"bin\" x=\"" << x << "\" y=\"" << y << "\" width=\"" << width << "\" height=\"" << height
                                   << "\" fill=\"" << (placeable ? utilization_color(bin.density() / color_ceiling, options_.dark_mode) : unavailable)
                                   << "\"><title>" << bin.pin_count << " pins; density " << bin.density() << "</title></rect>\n";
                          });
        },
        true);
  }

private:
  RenderOptions options_;
};

class CellDensitySvgWriter final : public Renderer {
public:
  explicit CellDensitySvgWriter(RenderOptions options) : options_(options) {}

  void render(const Board &board, const std::filesystem::path &output_path) const override {
    const auto layout = density_layout(board, options_, "cell density");
    const auto grid = board.cell_density(layout.bin_size);
    const auto unavailable = options_.dark_mode ? "#374151" : "#d1d5db";
    write_density_svg(
        output_path, board, options_, layout, "cell density",
        [&](std::ostream &output) {
          output << grid.columns << " by " << grid.rows << " bins of size " << grid.bin_size
                 << "; density is movable standard-cell overlap divided by capacity after macros and fixed physical objects are removed";
        },
        [&](std::ostream &output) {
          write_grid_bins(
              output, grid,
              [&](std::ostream &stream, const CellDensityBin &bin, double x, double y, double width, double height, std::uint64_t, std::uint64_t) {
                const auto density = bin.density();
                stream << "    <rect class=\"bin\" x=\"" << x << "\" y=\"" << y << "\" width=\"" << width << "\" height=\"" << height << "\" fill=\""
                       << (density ? utilization_color(*density, options_.dark_mode) : unavailable) << "\"><title>" << bin.movable_area
                       << " movable standard-cell area; " << bin.available_area << " available area; density ";
                if (density)
                  stream << *density;
                else
                  stream << "unavailable";
                stream << "</title></rect>\n";
              });
        },
        false);
  }

private:
  RenderOptions options_;
};

} // namespace

std::unique_ptr<Renderer> make_renderer(std::string_view format, RenderOptions options) {
  const auto normalized = lower(format);
  if (normalized == "svg")
    return std::make_unique<SvgWriter>(options);
  if (normalized == "utilization-svg")
    return std::make_unique<UtilizationSvgWriter>(options);
  if (normalized == "pin-density-svg")
    return std::make_unique<PinDensitySvgWriter>(options);
  if (normalized == "cell-density-svg")
    return std::make_unique<CellDensitySvgWriter>(options);
  throw Error("unsupported output format '" + std::string(format) + "'");
}

} // namespace placement
