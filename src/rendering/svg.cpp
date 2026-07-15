#include "placement/rendering/renderer.hpp"
#include "placement/rendering/style.hpp"

#include "../atomic_output.hpp"
#include "placement/error.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <charconv>
#include <cmath>
#include <concepts>
#include <fstream>
#include <limits>
#include <string>
#include <system_error>

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

class SvgOutput {
public:
  explicit SvgOutput(const std::filesystem::path &path) {
    file_.pubsetbuf(buffer_.data(), static_cast<std::streamsize>(buffer_.size()));
    if (!file_.open(path, std::ios::out))
      throw Error("cannot create " + path.string());
  }

  SvgOutput &operator<<(std::string_view value) {
    write(value.data(), value.size());
    return *this;
  }

  SvgOutput &operator<<(const std::string &value) { return *this << std::string_view(value); }
  SvgOutput &operator<<(const char *value) { return *this << std::string_view(value); }

  SvgOutput &operator<<(char value) {
    write(&value, 1);
    return *this;
  }

  template <std::integral T> SvgOutput &operator<<(T value) {
    std::array<char, 32> encoded{};
    const auto result = std::to_chars(encoded.data(), encoded.data() + encoded.size(), value);
    if (result.ec != std::errc{})
      throw Error("failed while formatting SVG integer");
    write(encoded.data(), static_cast<std::size_t>(result.ptr - encoded.data()));
    return *this;
  }

  SvgOutput &operator<<(double value) { return number(value, 12); }

  SvgOutput &number(double value, int precision) {
    std::array<char, 32> encoded{};
    const auto result = std::to_chars(encoded.data(), encoded.data() + encoded.size(), value, std::chars_format::general, precision);
    if (result.ec != std::errc{})
      throw Error("failed while formatting SVG number");
    write(encoded.data(), static_cast<std::size_t>(result.ptr - encoded.data()));
    return *this;
  }

  void finish() {
    if (file_.pubsync() != 0)
      throw Error("failed while writing SVG");
  }

private:
  void write(const char *data, std::size_t size) {
    if (file_.sputn(data, static_cast<std::streamsize>(size)) != static_cast<std::streamsize>(size))
      throw Error("failed while writing SVG");
  }

  // A larger fixed buffer avoids both a heap allocation and thousands of
  // small writes for SVGs containing millions of rectangles.
  std::array<char, 256 * 1024> buffer_{};
  std::filebuf file_;
};

void include_cell_bounds(const Board &board, Bounds &bounds) {
  for (const auto &cell : board.cells) {
    if (cell.location)
      bounds.include(placed_rectangle(cell));
  }
}

void write_paths(SvgOutput &output, const Board &board, CellClass classification, std::string_view css_class) {
  // Combining rectangles into paths keeps SVG size and DOM overhead low. A
  // bounded batch size avoids producing path attributes that are unwieldy for
  // viewers to parse on multi-million-cell designs. Filtering directly from
  // the model avoids duplicating every placed rectangle in a temporary array.
  constexpr std::size_t CELLS_PER_PATH = 10'000;
  std::size_t in_path = 0;

  for (const auto &cell : board.cells) {
    if (!cell.location || cell_class(cell) != classification)
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
    SvgOutput output(temporary);
    write(output);
    output.finish();
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
    include_cell_bounds(board, bounds);
    if (bounds.empty())
      throw Error("cannot render a board without geometry");

    const auto width = std::max(1.0, bounds.max_x - bounds.min_x);
    const auto height = std::max(1.0, bounds.max_y - bounds.min_y);
    const auto span = std::max(width, height);
    const auto padding = span * 0.01;
    const auto stroke = span / 6000.0;
    const auto &style = rendering_style::palette(options_.dark_mode);

    write_atomic(output_path, [&](SvgOutput &output) {
      output << "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
             << "<svg xmlns=\"http://www.w3.org/2000/svg\" viewBox=\"" << -padding << ' ' << -padding << ' ' << width + 2 * padding << ' '
             << height + 2 * padding << "\" preserveAspectRatio=\"xMidYMid meet\">\n"
             << "  <title>" << escape(board.name) << " placement</title>\n"
             << "  <desc>" << board.cells.size() << " cells, " << board.rows.size() << " rows, " << board.nets.size() << " nets</desc>\n";

      output << "  <style>\n"
             << "    .background{fill:" << style.background << "}.row{fill:" << style.row_fill << ";stroke:" << style.row_stroke
             << ";stroke-width:" << stroke << "}.movable{fill:" << style.movable_fill << ";stroke:none}.macro{fill:" << style.macro_fill
             << ";stroke:" << style.macro_stroke << ";stroke-width:" << stroke << "}.fixed{fill:" << style.fixed_fill
             << ";stroke:" << style.fixed_stroke << ";stroke-width:" << stroke << "}.fixed-ni{fill:" << style.fixed_non_interacting_fill
             << ";stroke:" << style.fixed_non_interacting_stroke << ";stroke-width:" << stroke << "}\n"
             << "  </style>\n";

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

void write_utilization_color(SvgOutput &output, double utilization, const rendering_style::Palette &style) {
  const auto clamped = std::clamp(utilization, 0.0, 1.0);
  const auto hue = 120.0 * (1.0 - clamped);
  output << "hsl(";
  output.number(hue, 5) << ' ' << style.heatmap_saturation_percent << "% " << style.heatmap_lightness_percent << "%)";
}

template <typename Grid> struct DensityPresentation;

template <> struct DensityPresentation<UtilizationGrid> {
  static constexpr std::string_view kind = "utilization";
};

template <> struct DensityPresentation<PinDensityGrid> {
  static constexpr std::string_view kind = "pin density";
};

template <> struct DensityPresentation<CellDensityGrid> {
  static constexpr std::string_view kind = "cell density";
};

template <typename Grid> struct DensityLayout {
  Bounds core;
  double width{};
  double height{};
  double bin_size{};
  double padding{};
  double stroke{};
};

template <typename Grid> [[nodiscard]] DensityLayout<Grid> density_layout(const Board &board, const RenderOptions &options) {
  Bounds core;
  for (const auto &row : board.rows) {
    for (const auto &subrow : row.subrows)
      core.include({subrow.origin, row.coordinate, static_cast<double>(subrow.site_count) * row.site_spacing, row.height});
  }
  if (core.empty())
    throw Error("cannot render " + std::string(DensityPresentation<Grid>::kind) + " without a placement region");

  const auto width = core.max_x - core.min_x;
  const auto height = core.max_y - core.min_y;
  const auto span = std::max(width, height);
  return {core, width, height, options.bin_size.value_or(span / 100.0), span * 0.01, span / 8000.0};
}

template <typename Grid, typename Function> void write_grid_bins(SvgOutput &output, const Grid &grid, Function write_bin) {
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

template <typename Grid, typename Description, typename Bins>
void write_density_svg(const std::filesystem::path &path, const Board &board, const RenderOptions &options, const DensityLayout<Grid> &layout,
                       Description description, Bins bins, bool movable_overlay) {
  write_atomic(path, [&](SvgOutput &output) {
    const auto &style = rendering_style::palette(options.dark_mode);

    output << "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
           << "<svg xmlns=\"http://www.w3.org/2000/svg\" viewBox=\"" << -layout.padding << ' ' << -layout.padding << ' '
           << layout.width + 2 * layout.padding << ' ' << layout.height + 2 * layout.padding << "\" preserveAspectRatio=\"xMidYMid meet\">\n"
           << "  <title>" << escape(board.name) << ' ' << DensityPresentation<Grid>::kind << "</title>\n"
           << "  <desc>";
    description(output);
    output << "</desc>\n"
           << "  <style>\n"
           << "    .background{fill:" << style.background << "}.bin{stroke:" << style.grid_stroke
           << ";stroke-opacity:.38;stroke-width:" << layout.stroke << '}';
    if (movable_overlay)
      output << ".movable-overlay{fill:" << style.surface << ";fill-opacity:.42;stroke:none}";
    output << ".macro-overlay{fill:" << style.surface << ";stroke:" << style.overlay_stroke << ";stroke-width:" << layout.stroke << "}"
           << ".fixed-overlay{fill:" << style.surface << ";stroke:" << style.overlay_stroke << ";stroke-width:" << layout.stroke
           << "}.fixed-ni-overlay{fill:" << style.surface << ";stroke:" << style.non_interacting_overlay_stroke << ";stroke-width:" << layout.stroke
           << "}\n"
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
    const auto layout = density_layout<UtilizationGrid>(board, options_);
    const auto grid = board.utilization(layout.bin_size);
    const auto &style = rendering_style::palette(options_.dark_mode);
    write_density_svg(
        output_path, board, options_, layout,
        [&](SvgOutput &output) {
          output << grid.columns << " by " << grid.rows << " bins of size " << grid.bin_size
                 << "; green is low utilization, red is 100 percent or greater, gray is not placeable";
        },
        [&](SvgOutput &output) {
          write_grid_bins(
              output, grid,
              [&](SvgOutput &stream, const UtilizationBin &bin, double x, double y, double width, double height, std::uint64_t, std::uint64_t) {
                const auto utilization = bin.utilization();
                stream << "    <rect class=\"bin\" x=\"" << x << "\" y=\"" << y << "\" width=\"" << width << "\" height=\"" << height << "\" fill=\"";
                if (utilization)
                  write_utilization_color(stream, *utilization, style);
                else
                  stream << style.unavailable;
                stream << "\"/>\n";
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
    const auto layout = density_layout<PinDensityGrid>(board, options_);
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

    const auto &style = rendering_style::palette(options_.dark_mode);
    write_density_svg(
        output_path, board, options_, layout,
        [&](SvgOutput &output) {
          output << grid.columns << " by " << grid.rows << " bins of size " << grid.bin_size << "; color saturates at the 95th percentile, "
                 << color_ceiling << " pins per square placement unit";
        },
        [&](SvgOutput &output) {
          write_grid_bins(output, grid,
                          [&](SvgOutput &stream, const PinDensityBin &bin, double x, double y, double width, double height, std::uint64_t column,
                              std::uint64_t row) {
                            const auto placeable = utilization_grid.at(column, row).utilization().has_value();
                            stream << "    <rect class=\"bin\" x=\"" << x << "\" y=\"" << y << "\" width=\"" << width << "\" height=\"" << height
                                   << "\" fill=\"";
                            if (placeable)
                              write_utilization_color(stream, bin.density() / color_ceiling, style);
                            else
                              stream << style.unavailable;
                            stream << "\"><title>" << bin.pin_count << " pins; density " << bin.density() << "</title></rect>\n";
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
    const auto layout = density_layout<CellDensityGrid>(board, options_);
    const auto grid = board.cell_density(layout.bin_size);
    const auto &style = rendering_style::palette(options_.dark_mode);
    write_density_svg(
        output_path, board, options_, layout,
        [&](SvgOutput &output) {
          output << grid.columns << " by " << grid.rows << " bins of size " << grid.bin_size
                 << "; density is movable standard-cell overlap divided by capacity after macros and fixed physical objects are removed";
        },
        [&](SvgOutput &output) {
          write_grid_bins(
              output, grid,
              [&](SvgOutput &stream, const CellDensityBin &bin, double x, double y, double width, double height, std::uint64_t, std::uint64_t) {
                const auto density = bin.density();
                stream << "    <rect class=\"bin\" x=\"" << x << "\" y=\"" << y << "\" width=\"" << width << "\" height=\"" << height << "\" fill=\"";
                if (density)
                  write_utilization_color(stream, *density, style);
                else
                  stream << style.unavailable;
                stream << "\"><title>" << bin.movable_area << " movable standard-cell area; " << bin.available_area << " available area; density ";
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
