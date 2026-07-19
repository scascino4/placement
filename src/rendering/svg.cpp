#include "placement/rendering/renderer.hpp"
#include "placement/rendering/style.hpp"

#include "../atomic_output.hpp"
#include "../bounds.hpp"
#include "../text.hpp"
#include "placement/error.hpp"

#include <algorithm>
#include <array>
#include <charconv>
#include <cmath>
#include <concepts>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <limits>
#include <string>
#include <system_error>
#include <vector>

namespace placement {
namespace {

[[nodiscard]] std::string escape(std::string_view value) {
  std::string res;
  res.reserve(value.size());
  for (const auto ch : value) {
    switch (ch) {
    case '&':
      res += "&amp;";
      break;
    case '<':
      res += "&lt;";
      break;
    case '>':
      res += "&gt;";
      break;
    case '\"':
      res += "&quot;";
      break;
    case '\'':
      res += "&apos;";
      break;
    default:
      res += ch;
    }
  }
  return res;
}

using detail::Bounds;

void include_geometry(Bounds &bounds, const PlacedRectangle &rect) {
  if (!std::isfinite(rect.x) || !std::isfinite(rect.y) || !std::isfinite(rect.width) || !std::isfinite(rect.height) || rect.width < 0 ||
      rect.height < 0)
    throw Error("cannot render non-finite or negative geometry");
  bounds.include(rect);
}

enum class CellClass { Movable, Macro, Fixed, FixedNonInteracting };

[[nodiscard]] CellClass cell_class(const Cell &cell) {
  // Macro presentation takes precedence over placement status so a movable
  // physical macro remains visually distinguishable from standard cells.
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
    const auto res = std::to_chars(encoded.data(), encoded.data() + encoded.size(), value);
    if (res.ec != std::errc{})
      throw Error("failed while formatting SVG integer");
    write(encoded.data(), static_cast<std::size_t>(res.ptr - encoded.data()));
    return *this;
  }

  SvgOutput &operator<<(double value) { return number(value, 12); }

  SvgOutput &number(double value, int precision) {
    constexpr double MAX_EXACT_INTEGER = 9'007'199'254'740'992.0;
    if (value == 0.0 && !std::signbit(value))
      return *this << '0';
    if (value != 0.0 && value >= -MAX_EXACT_INTEGER && value <= MAX_EXACT_INTEGER && std::trunc(value) == value)
      return *this << static_cast<std::int64_t>(value);

    std::array<char, 32> encoded{};
    const auto res = std::to_chars(encoded.data(), encoded.data() + encoded.size(), value, std::chars_format::general, precision);
    if (res.ec != std::errc{})
      throw Error("failed while formatting SVG number");
    write(encoded.data(), static_cast<std::size_t>(res.ptr - encoded.data()));
    return *this;
  }

  void finish() {
    flush_buf();
    if (file_.pubsync() != 0)
      throw Error("failed while writing SVG");
  }

private:
  void write(const char *data, std::size_t size) {
    while (size != 0) {
      if (used_ == buf_.size())
        flush_buf();

      const auto count = std::min(size, buf_.size() - used_);
      std::memcpy(buf_.data() + used_, data, count);
      used_ += count;
      data += count;
      size -= count;
    }
  }

  void flush_buf() {
    if (used_ == 0)
      return;
    if (file_.sputn(buf_.data(), static_cast<std::streamsize>(used_)) != static_cast<std::streamsize>(used_))
      throw Error("failed while writing SVG");
    used_ = 0;
  }

  // A larger fixed buffer avoids both a heap allocation and thousands of
  // small writes for SVGs containing millions of rectangles.
  std::array<char, 256 * 1024> buf_{};
  std::size_t used_{};
  std::filebuf file_;
};

constexpr std::uint8_t UNPLACED_CELL = std::numeric_limits<std::uint8_t>::max();

[[nodiscard]] std::vector<std::uint8_t> classify_cells(const Board &board, Bounds *bounds = nullptr) {
  std::vector<std::uint8_t> classes(board.cells.size(), UNPLACED_CELL);
  for (std::size_t idx = 0; idx < board.cells.size(); ++idx) {
    const auto &cell = board.cells[idx];
    if (!cell.location)
      continue;

    if (bounds)
      include_geometry(*bounds, placed_rectangle(cell));
    classes[idx] = static_cast<std::uint8_t>(cell_class(cell));
  }
  return classes;
}

template <CellClass Classification> struct CellPresentation;

template <> struct CellPresentation<CellClass::Movable> {
  static constexpr std::string_view css_class = "movable";
};

template <> struct CellPresentation<CellClass::Macro> {
  static constexpr std::string_view css_class = "macro";
};

template <> struct CellPresentation<CellClass::Fixed> {
  static constexpr std::string_view css_class = "fixed";
};

template <> struct CellPresentation<CellClass::FixedNonInteracting> {
  static constexpr std::string_view css_class = "fixed-ni";
};

template <CellClass Classification, bool Overlay>
void write_cell_paths(SvgOutput &out, const Board &board, const std::vector<std::uint8_t> &classes) {
  // Combining rectangles into paths keeps SVG size and DOM overhead low. A
  // bounded batch size avoids producing path attributes that are unwieldy for
  // viewers to parse on multi-million-cell designs. The compact classification
  // array keeps the class-ordered SVG layers without touching every large Cell
  // record during all four filtering passes.
  constexpr std::size_t CELLS_PER_PATH = 10'000;
  std::size_t in_path = 0;

  for (std::size_t idx = 0; idx < board.cells.size(); ++idx) {
    if (classes[idx] != static_cast<std::uint8_t>(Classification))
      continue;

    const auto &cell = board.cells[idx];
    const auto rect = placed_rectangle(cell);
    if (rect.width == 0 || rect.height == 0)
      continue;

    if (in_path == 0) {
      out << "    <path class=\"" << CellPresentation<Classification>::css_class;
      if constexpr (Overlay)
        out << "-overlay";
      out << "\" d=\"";
    }

    out << 'M' << rect.x << ' ' << rect.y << 'h' << rect.width << 'v' << rect.height << 'h' << -rect.width << 'z';

    ++in_path;
    if (in_path == CELLS_PER_PATH) {
      out << "\"/>\n";
      in_path = 0;
    }
  }

  if (in_path != 0)
    out << "\"/>\n";
}

template <CellClass Classification> void write_paths(SvgOutput &out, const Board &board, const std::vector<std::uint8_t> &classes) {
  write_cell_paths<Classification, false>(out, board, classes);
}

template <CellClass Classification> void write_overlay_paths(SvgOutput &out, const Board &board, const std::vector<std::uint8_t> &classes) {
  write_cell_paths<Classification, true>(out, board, classes);
}

template <typename Write> void write_atomic(const std::filesystem::path &path, Write write) {
  detail::atomic_output(path, [&](const auto &tmp) {
    SvgOutput out(tmp);
    write(out);
    out.finish();
  });
}

class SvgWriter final : public Renderer {
public:
  explicit SvgWriter(RenderOptions opts) : opts_(opts) {}

  void render(const Board &board, const std::filesystem::path &out_path) const override {
    Bounds box;
    for (const auto &row : board.rows)
      for (const auto &subrow : row.subrows)
        include_geometry(box, {subrow.origin, row.coordinate, static_cast<double>(subrow.site_count) * row.site_spacing, row.height});
    const auto classes = classify_cells(board, &box);

    if (box.empty())
      throw Error("cannot render a board without geometry");

    const auto width = std::max(1.0, box.max_x - box.min_x);
    const auto height = std::max(1.0, box.max_y - box.min_y);
    const auto span = std::max(width, height);
    const auto padding = span * 0.01;
    const auto stroke = span / 6000.0;
    const auto &style = rendering_style::palette(opts_.dark_mode);

    write_atomic(out_path, [&](SvgOutput &out) {
      out << "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
          << "<svg xmlns=\"http://www.w3.org/2000/svg\" viewBox=\"" << -padding << ' ' << -padding << ' ' << width + 2 * padding << ' '
          << height + 2 * padding << "\" preserveAspectRatio=\"xMidYMid meet\">\n"
          << "  <title>" << escape(board.name) << " placement</title>\n"
          << "  <desc>" << board.cells.size() << " cells, " << board.rows.size() << " rows, " << board.nets.size() << " nets</desc>\n";

      out << "  <style>\n"
          << "    .background{fill:" << style.background << "}.row{fill:" << style.row_fill << ";stroke:" << style.row_stroke
          << ";stroke-width:" << stroke << "}.movable{fill:" << style.movable_fill << ";stroke:none}.macro{fill:" << style.macro_fill
          << ";stroke:" << style.macro_stroke << ";stroke-width:" << stroke << "}.fixed{fill:" << style.fixed_fill << ";stroke:" << style.fixed_stroke
          << ";stroke-width:" << stroke << "}.fixed-ni{fill:" << style.fixed_non_interacting_fill << ";stroke:" << style.fixed_non_interacting_stroke
          << ";stroke-width:" << stroke << "}\n"
          << "  </style>\n";

      out << "  <rect class=\"background\" x=\"" << -padding << "\" y=\"" << -padding << "\" width=\"" << width + 2 * padding << "\" height=\""
          << height + 2 * padding << "\"/>\n";

      // Placement coordinates use an upward-positive Y axis, while SVG uses
      // a downward-positive one. Translate to the computed bounds, then flip
      // the geometry group without also flipping title or descriptive text.
      out << "  <g transform=\"translate(" << -box.min_x << ' ' << box.max_y << ") scale(1 -1)\" shape-rendering=\"crispEdges\">\n";

      for (const auto &row : board.rows)
        for (const auto &subrow : row.subrows)
          out << "    <rect class=\"row\" x=\"" << subrow.origin << "\" y=\"" << row.coordinate << "\" width=\""
              << static_cast<double>(subrow.site_count) * row.site_spacing << "\" height=\"" << row.height << "\"/>\n";

      write_paths<CellClass::Movable>(out, board, classes);
      write_paths<CellClass::Macro>(out, board, classes);
      write_paths<CellClass::Fixed>(out, board, classes);
      write_paths<CellClass::FixedNonInteracting>(out, board, classes);

      out << "  </g>\n</svg>\n";
    });
  }

private:
  RenderOptions opts_;
};

void write_utilization_color(SvgOutput &out, double util, const rendering_style::Palette &style) {
  const auto clamped = std::clamp(util, 0.0, 1.0);
  const auto hue = 120.0 * (1.0 - clamped);
  out << "hsl(";
  out.number(hue, 5) << ' ' << style.heatmap_saturation_percent << "% " << style.heatmap_lightness_percent << "%)";
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
  Bounds viewport;
  double width{};
  double height{};
  double bin_size{};
  double padding{};
  double stroke{};
};

template <typename Grid> [[nodiscard]] DensityLayout<Grid> density_layout(const Board &board, const RenderOptions &opts) {
  // Grids cover only the row-defined core, but the viewport also includes
  // placed objects outside it so overlays are not clipped.
  Bounds core;
  for (const auto &row : board.rows)
    for (const auto &subrow : row.subrows)
      include_geometry(core, {subrow.origin, row.coordinate, static_cast<double>(subrow.site_count) * row.site_spacing, row.height});

  if (core.empty())
    throw Error("cannot render " + std::string(DensityPresentation<Grid>::kind) + " without a placement region");

  Bounds viewport = core;
  for (const auto &cell : board.cells)
    if (cell.location)
      include_geometry(viewport, placed_rectangle(cell));

  const auto width = std::max(1.0, viewport.max_x - viewport.min_x);
  const auto height = std::max(1.0, viewport.max_y - viewport.min_y);
  const auto view_span = std::max(width, height);
  const auto core_span = std::max(core.max_x - core.min_x, core.max_y - core.min_y);

  return {core, viewport, width, height, opts.bin_size.value_or(core_span / 100.0), view_span * 0.01, view_span / 8000.0};
}

template <typename Grid, typename Fn> void write_grid_bins(SvgOutput &out, const Grid &grid, Fn write_bin) {
  for (std::uint64_t row = 0; row < grid.rows; ++row) {
    const auto y = grid.min_y + static_cast<double>(row) * grid.bin_size;
    const auto height = std::min(grid.bin_size, grid.max_y - y);
    for (std::uint64_t col = 0; col < grid.columns; ++col) {
      const auto x = grid.min_x + static_cast<double>(col) * grid.bin_size;
      const auto width = std::min(grid.bin_size, grid.max_x - x);
      write_bin(out, grid.at(col, row), x, y, width, height, col, row);
    }
  }
}

template <typename Grid, typename Description, typename Bins>
void write_density_svg(const std::filesystem::path &path, const Board &board, const RenderOptions &opts, const DensityLayout<Grid> &layout,
                       Description description, Bins bins, bool movable_overlay) {
  const auto classes = classify_cells(board);
  write_atomic(path, [&](SvgOutput &out) {
    const auto &style = rendering_style::palette(opts.dark_mode);

    out << "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
        << "<svg xmlns=\"http://www.w3.org/2000/svg\" viewBox=\"" << -layout.padding << ' ' << -layout.padding << ' '
        << layout.width + 2 * layout.padding << ' ' << layout.height + 2 * layout.padding << "\" preserveAspectRatio=\"xMidYMid meet\">\n"
        << "  <title>" << escape(board.name) << ' ' << DensityPresentation<Grid>::kind << "</title>\n"
        << "  <desc>";
    description(out);
    out << "</desc>\n"
        << "  <style>\n"
        << "    .background{fill:" << style.background << "}.bin{stroke:" << style.grid_stroke << ";stroke-opacity:.38;stroke-width:" << layout.stroke
        << '}';
    if (movable_overlay)
      out << ".movable-overlay{fill:" << style.surface << ";fill-opacity:.42;stroke:none}";
    out << ".macro-overlay{fill:" << style.surface << ";stroke:" << style.overlay_stroke << ";stroke-width:" << layout.stroke << "}"
        << ".fixed-overlay{fill:" << style.surface << ";stroke:" << style.overlay_stroke << ";stroke-width:" << layout.stroke
        << "}.fixed-ni-overlay{fill:" << style.surface << ";stroke:" << style.non_interacting_overlay_stroke << ";stroke-width:" << layout.stroke
        << "}\n"
        << "  </style>\n"
        << "  <rect class=\"background\" x=\"" << -layout.padding << "\" y=\"" << -layout.padding << "\" width=\""
        << layout.width + 2 * layout.padding << "\" height=\"" << layout.height + 2 * layout.padding << "\"/>\n"
        << "  <g transform=\"translate(" << -layout.viewport.min_x << ' ' << layout.viewport.max_y
        << ") scale(1 -1)\" shape-rendering=\"crispEdges\">\n";

    bins(out);
    if (movable_overlay)
      write_overlay_paths<CellClass::Movable>(out, board, classes);
    write_overlay_paths<CellClass::Macro>(out, board, classes);
    write_overlay_paths<CellClass::Fixed>(out, board, classes);
    write_overlay_paths<CellClass::FixedNonInteracting>(out, board, classes);
    out << "  </g>\n</svg>\n";
  });
}

class UtilizationSvgWriter final : public Renderer {
public:
  explicit UtilizationSvgWriter(RenderOptions opts) : opts_(opts) {}

  void render(const Board &board, const std::filesystem::path &out_path) const override {
    const auto layout = density_layout<UtilizationGrid>(board, opts_);
    const auto grid = board.utilization(layout.bin_size);
    const auto &style = rendering_style::palette(opts_.dark_mode);
    write_density_svg(
        out_path, board, opts_, layout,
        [&](SvgOutput &out) {
          out << grid.columns << " by " << grid.rows << " bins of size " << grid.bin_size
              << "; green is low utilization, red is 100 percent or greater, gray is not placeable";
        },
        [&](SvgOutput &out) {
          write_grid_bins(
              out, grid,
              [&](SvgOutput &out, const UtilizationBin &bin, double x, double y, double width, double height, std::uint64_t, std::uint64_t) {
                const auto util = bin.utilization();
                out << "    <rect class=\"bin\" x=\"" << x << "\" y=\"" << y << "\" width=\"" << width << "\" height=\"" << height << "\" fill=\"";
                if (util)
                  write_utilization_color(out, *util, style);
                else
                  out << style.unavailable;
                out << "\"/>\n";
              });
        },
        true);
  }

private:
  RenderOptions opts_;
};

class PinDensitySvgWriter final : public Renderer {
public:
  explicit PinDensitySvgWriter(RenderOptions opts) : opts_(opts) {}

  void render(const Board &board, const std::filesystem::path &out_path) const override {
    const auto layout = density_layout<PinDensityGrid>(board, opts_);
    const auto grid = board.pin_density(layout.bin_size);
    const auto util_grid = board.utilization(layout.bin_size);

    std::vector<double> densities;
    densities.reserve(grid.bins.size());
    for (const auto &bin : grid.bins)
      if (bin.pin_count != 0)
        densities.push_back(bin.density());

    std::sort(densities.begin(), densities.end());

    // A percentile ceiling prevents a handful of pin hot spots from washing
    // out useful differences across the rest of the heatmap.
    double ceiling = 1.0;
    if (!densities.empty()) {
      const auto idx = static_cast<std::size_t>(std::ceil(0.95 * static_cast<double>(densities.size()))) - 1;
      ceiling = densities[idx];
    }

    const auto &style = rendering_style::palette(opts_.dark_mode);
    write_density_svg(
        out_path, board, opts_, layout,
        [&](SvgOutput &out) {
          out << grid.columns << " by " << grid.rows << " bins of size " << grid.bin_size << "; color saturates at the 95th percentile, " << ceiling
              << " pins per square placement unit";
        },
        [&](SvgOutput &out) {
          write_grid_bins(
              out, grid,
              [&](SvgOutput &out, const PinDensityBin &bin, double x, double y, double width, double height, std::uint64_t col, std::uint64_t row) {
                const auto placeable = util_grid.at(col, row).utilization().has_value();
                out << "    <rect class=\"bin\" x=\"" << x << "\" y=\"" << y << "\" width=\"" << width << "\" height=\"" << height << "\" fill=\"";
                if (placeable)
                  write_utilization_color(out, bin.density() / ceiling, style);
                else
                  out << style.unavailable;
                out << "\"><title>" << bin.pin_count << " pins; density " << bin.density() << "</title></rect>\n";
              });
        },
        true);
  }

private:
  RenderOptions opts_;
};

class CellDensitySvgWriter final : public Renderer {
public:
  explicit CellDensitySvgWriter(RenderOptions opts) : opts_(opts) {}

  void render(const Board &board, const std::filesystem::path &out_path) const override {
    const auto layout = density_layout<CellDensityGrid>(board, opts_);
    const auto grid = board.cell_density(layout.bin_size);
    const auto &style = rendering_style::palette(opts_.dark_mode);
    write_density_svg(
        out_path, board, opts_, layout,
        [&](SvgOutput &out) {
          out << grid.columns << " by " << grid.rows << " bins of size " << grid.bin_size
              << "; density is movable-object overlap divided by capacity after fixed physical objects are removed";
        },
        [&](SvgOutput &out) {
          write_grid_bins(
              out, grid,
              [&](SvgOutput &out, const CellDensityBin &bin, double x, double y, double width, double height, std::uint64_t, std::uint64_t) {
                const auto dens = bin.density();
                out << "    <rect class=\"bin\" x=\"" << x << "\" y=\"" << y << "\" width=\"" << width << "\" height=\"" << height << "\" fill=\"";
                if (dens)
                  write_utilization_color(out, *dens, style);
                else
                  out << style.unavailable;
                out << "\"><title>" << bin.movable_area << " movable-object area; " << bin.available_area << " available area; density ";
                if (dens)
                  out << *dens;
                else
                  out << "unavailable";
                out << "</title></rect>\n";
              });
        },
        false);
  }

private:
  RenderOptions opts_;
};

} // namespace

std::unique_ptr<Renderer> make_renderer(std::string_view format, RenderOptions opts) {
  const auto norm = detail::lower(format);
  if (norm == "svg")
    return std::make_unique<SvgWriter>(opts);
  if (norm == "utilization-svg")
    return std::make_unique<UtilizationSvgWriter>(opts);
  if (norm == "pin-density-svg")
    return std::make_unique<PinDensitySvgWriter>(opts);
  if (norm == "cell-density-svg")
    return std::make_unique<CellDensitySvgWriter>(opts);
  throw Error("unsupported output format '" + std::string(format) + "'");
}

} // namespace placement
