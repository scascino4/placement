#include "placement/rendering/renderer.hpp"

#include "placement/error.hpp"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cmath>
#include <fstream>
#include <iomanip>
#include <limits>
#include <string>

namespace placement {
namespace {

[[nodiscard]] std::string lower(std::string_view value) {
  std::string result(value);
  std::ranges::transform(result, result.begin(), [](unsigned char character) {
    return static_cast<char>(std::tolower(character));
  });
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
    if (!std::isfinite(rectangle.x) || !std::isfinite(rectangle.y) ||
        !std::isfinite(rectangle.width) || !std::isfinite(rectangle.height) ||
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
  if (cell.kind == CellKind::TerminalNonInteracting ||
      cell.location->status == PlacementStatus::FixedNonInteracting)
    return CellClass::FixedNonInteracting;
  if (cell.kind == CellKind::Terminal || cell.location->status == PlacementStatus::Fixed)
    return CellClass::Fixed;
  return CellClass::Movable;
}

void write_paths(std::ostream &output, const Board &board, CellClass wanted,
                 std::string_view css_class) {
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
    output << 'M' << rectangle.x << ' ' << rectangle.y << 'h' << rectangle.width << 'v'
           << rectangle.height << 'h' << -rectangle.width << 'z';

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

class SvgWriter final : public Renderer {
public:
  void render(const Board &board, const std::filesystem::path &output_path) const override {
    Bounds bounds;
    for (const auto &row : board.rows) {
      for (const auto &subrow : row.subrows)
        bounds.include({subrow.origin, row.coordinate,
                        static_cast<double>(subrow.site_count) * row.site_spacing, row.height});
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

    if (!output_path.parent_path().empty()) {
      std::filesystem::create_directories(output_path.parent_path());
    }

    // Write beside the destination first so a failed render cannot leave a
    // partial SVG at the requested path.
    const auto temporary = temporary_path(output_path);
    try {
      std::ofstream output(temporary);
      if (!output)
        throw Error("cannot create " + output_path.string());

      output << std::setprecision(12);

      output << "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
             << "<svg xmlns=\"http://www.w3.org/2000/svg\" viewBox=\"" << -padding << ' '
             << -padding << ' ' << width + 2 * padding << ' ' << height + 2 * padding
             << "\" preserveAspectRatio=\"xMidYMid meet\">\n"
             << "  <title>" << escape(board.name) << " placement</title>\n"
             << "  <desc>" << board.cells.size() << " cells, " << board.rows.size() << " rows, "
             << board.nets.size() << " nets</desc>\n";

      output
          << "  <style>\n"
          << "    .background{fill:#f8fafc}.row{fill:#e2e8f0;stroke:#94a3b8;stroke-width:" << stroke
          << "}.movable{fill:#3b82f6;stroke:none}.fixed{fill:#ef4444;stroke:#7f1d1d;stroke-width:"
          << stroke << "}.fixed-ni{fill:#f59e0b;stroke:#78350f;stroke-width:" << stroke << "}\n"
          << "  </style>\n";

      output << "  <rect class=\"background\" x=\"" << -padding << "\" y=\"" << -padding
             << "\" width=\"" << width + 2 * padding << "\" height=\"" << height + 2 * padding
             << "\"/>\n";

      // Placement coordinates use an upward-positive Y axis, while SVG uses
      // a downward-positive one. Translate to the computed bounds, then flip
      // the geometry group without also flipping title or descriptive text.
      output << "  <g transform=\"translate(" << -bounds.minimum_x << ' ' << bounds.maximum_y
             << ") scale(1 -1)\" shape-rendering=\"crispEdges\">\n";

      for (const auto &row : board.rows) {
        for (const auto &subrow : row.subrows) {
          output << "    <rect class=\"row\" x=\"" << subrow.origin << "\" y=\"" << row.coordinate
                 << "\" width=\"" << static_cast<double>(subrow.site_count) * row.site_spacing
                 << "\" height=\"" << row.height << "\"/>\n";
        }
      }

      write_paths(output, board, CellClass::Movable, "movable");
      write_paths(output, board, CellClass::Fixed, "fixed");
      write_paths(output, board, CellClass::FixedNonInteracting, "fixed-ni");

      output << "  </g>\n</svg>\n";
      output.flush();
      if (!output)
        throw Error("failed while writing " + output_path.string());
      output.close();

      // Some platforms do not allow rename to replace an existing file, so
      // retry after removing the old destination.
      std::error_code error;
      std::filesystem::rename(temporary, output_path, error);
      if (error) {
        std::filesystem::remove(output_path, error);
        error.clear();
        std::filesystem::rename(temporary, output_path, error);
      }
      if (error)
        throw Error("cannot replace " + output_path.string() + ": " + error.message());
    } catch (...) {
      std::error_code ignored;
      std::filesystem::remove(temporary, ignored);
      throw;
    }
  }
};

} // namespace

std::unique_ptr<Renderer> make_renderer(std::string_view format) {
  if (lower(format) == "svg")
    return std::make_unique<SvgWriter>();
  throw Error("unsupported output format '" + std::string(format) + "'");
}

} // namespace placement
