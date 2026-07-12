#include "placement/model.hpp"

#include "placement/error.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <numeric>
#include <tuple>
#include <utility>

namespace placement {
namespace {

struct Rectangle {
  double x{};
  double y{};
  double w{};
  double h{};

  [[nodiscard]] double right() const { return x + w; }
  [[nodiscard]] double top() const { return y + h; }
};

struct Point {
  double x{};
  double y{};
};

[[nodiscard]] Rectangle cell_rectangle(const Cell &cell) {
  const auto &location = *cell.location;
  double w = location.width.value_or(cell.width);
  double h = location.height.value_or(cell.height);
  switch (location.orientation) {
  case Orientation::E:
  case Orientation::W:
  case Orientation::FE:
  case Orientation::FW:
    std::swap(w, h);
    break;
  default:
    break;
  }
  return {location.x, location.y, w, h};
}

[[nodiscard]] bool movable(const Cell &cell) {
  return cell.kind == CellKind::Movable && cell.location && cell.location->status == PlacementStatus::Movable;
}

[[nodiscard]] bool fixed(const Cell &cell) { return cell.location && !movable(cell); }

[[nodiscard]] Point pin_position(const Cell &cell, const Pin &pin) {
  const auto rect = cell_rectangle(cell);
  double x = pin.offset_x;
  double y = pin.offset_y;
  switch (cell.location->orientation) {
  case Orientation::N:
    break;
  case Orientation::E:
    std::tie(x, y) = std::pair{y, -x};
    break;
  case Orientation::S:
    x = -x;
    y = -y;
    break;
  case Orientation::W:
    std::tie(x, y) = std::pair{-y, x};
    break;
  case Orientation::FN:
    x = -x;
    break;
  case Orientation::FE:
    std::swap(x, y);
    break;
  case Orientation::FS:
    y = -y;
    break;
  case Orientation::FW:
    std::tie(x, y) = std::pair{-y, -x};
    break;
  }
  return {rect.x + rect.w / 2.0 + x, rect.y + rect.h / 2.0 + y};
}

enum class Area { Movable, Placeable };

void validate(const Rectangle &rect) {
  if (!std::isfinite(rect.x) || !std::isfinite(rect.y) || !std::isfinite(rect.w) || !std::isfinite(rect.h) || rect.w < 0 || rect.h < 0)
    throw Error("cannot calculate utilization for non-finite or negative geometry");
}

void add_overlap(UtilizationGrid &grid, const Rectangle &rect, Area area, double scale = 1.0) {
  validate(rect);
  if (rect.w == 0 || rect.h == 0)
    return;

  const auto x0 = std::max(rect.x, grid.min_x);
  const auto y0 = std::max(rect.y, grid.min_y);
  const auto x1 = std::min(rect.right(), grid.max_x);
  const auto y1 = std::min(rect.top(), grid.max_y);
  if (x0 >= x1 || y0 >= y1)
    return;

  const auto col0 = static_cast<std::uint64_t>((x0 - grid.min_x) / grid.bin_size);
  const auto row0 = static_cast<std::uint64_t>((y0 - grid.min_y) / grid.bin_size);
  const auto col1 = std::min(grid.columns - 1, static_cast<std::uint64_t>((std::nextafter(x1, x0) - grid.min_x) / grid.bin_size));
  const auto row1 = std::min(grid.rows - 1, static_cast<std::uint64_t>((std::nextafter(y1, y0) - grid.min_y) / grid.bin_size));

  for (auto row = row0; row <= row1; ++row) {
    const auto bin_y = grid.min_y + static_cast<double>(row) * grid.bin_size;
    const auto overlap_h = std::min(y1, bin_y + grid.bin_size) - std::max(y0, bin_y);

    for (auto col = col0; col <= col1; ++col) {
      const auto bin_x = grid.min_x + static_cast<double>(col) * grid.bin_size;
      const auto overlap_w = std::min(x1, bin_x + grid.bin_size) - std::max(x0, bin_x);
      const auto overlap = scale * overlap_w * overlap_h;
      auto &bin = grid.bins[static_cast<std::size_t>(row * grid.columns + col)];

      if (area == Area::Movable)
        bin.movable_area += overlap;
      else
        bin.placeable_area += overlap;
    }
  }
}

} // namespace

std::optional<double> UtilizationBin::utilization() const {
  if (placeable_area <= 0)
    return std::nullopt;
  return movable_area / placeable_area;
}

const UtilizationBin &UtilizationGrid::at(std::uint64_t column, std::uint64_t row) const {
  if (column >= columns || row >= rows)
    throw Error("utilization bin index is out of bounds");
  return bins[static_cast<std::size_t>(row * columns + column)];
}

double PinDensityBin::density() const {
  if (area <= 0)
    return 0;
  return static_cast<double>(pin_count) / area;
}

const PinDensityBin &PinDensityGrid::at(std::uint64_t column, std::uint64_t row) const {
  if (column >= columns || row >= rows)
    throw Error("pin density bin index is out of bounds");
  return bins[static_cast<std::size_t>(row * columns + column)];
}

UtilizationGrid Board::utilization(double bin_size) const {
  if (!std::isfinite(bin_size) || bin_size <= 0)
    throw Error("utilization bin size must be finite and positive");

  auto min_x = std::numeric_limits<double>::infinity();
  auto min_y = std::numeric_limits<double>::infinity();
  auto max_x = -std::numeric_limits<double>::infinity();
  auto max_y = -std::numeric_limits<double>::infinity();

  for (const auto &row : rows) {
    for (const auto &subrow : row.subrows) {
      const Rectangle rect{subrow.origin, row.coordinate, static_cast<double>(subrow.site_count) * row.site_spacing, row.height};
      validate(rect);
      min_x = std::min(min_x, rect.x);
      min_y = std::min(min_y, rect.y);
      max_x = std::max(max_x, rect.right());
      max_y = std::max(max_y, rect.top());
    }
  }

  if (!std::isfinite(min_x) || max_x <= min_x || max_y <= min_y)
    throw Error("cannot calculate utilization without a non-empty placement region");

  const auto cols = static_cast<std::uint64_t>(std::ceil((max_x - min_x) / bin_size));
  const auto row_count = static_cast<std::uint64_t>(std::ceil((max_y - min_y) / bin_size));
  if (cols == 0 || row_count == 0 || cols > std::numeric_limits<std::size_t>::max() / row_count)
    throw Error("utilization grid is too large");

  UtilizationGrid grid{min_x, min_y, max_x, max_y, bin_size, cols, row_count, {}};
  grid.bins.resize(static_cast<std::size_t>(cols * row_count));

  for (const auto &row : rows) {
    for (const auto &subrow : row.subrows)
      add_overlap(grid, {subrow.origin, row.coordinate, static_cast<double>(subrow.site_count) * row.site_spacing, row.height}, Area::Placeable);
  }

  std::vector<std::size_t> row_order(rows.size());
  std::iota(row_order.begin(), row_order.end(), 0);
  std::sort(row_order.begin(), row_order.end(), [this](std::size_t lhs, std::size_t rhs) { return rows[lhs].coordinate < rows[rhs].coordinate; });

  double max_row_height = 0;
  for (const auto &row : rows)
    max_row_height = std::max(max_row_height, row.height);

  for (const auto &cell : cells) {
    if (!cell.location)
      continue;
    const auto rect = cell_rectangle(cell);
    if (movable(cell)) {
      add_overlap(grid, rect, Area::Movable);
    } else if (fixed(cell)) {
      // Only rows that can intersect the fixed cell need to be examined.
      const auto first =
          std::ranges::lower_bound(row_order, rect.y - max_row_height, {}, [this](std::size_t index) { return rows[index].coordinate; });

      for (auto it = first; it != row_order.end(); ++it) {
        const auto &row = rows[*it];
        if (row.coordinate >= rect.top())
          break;

        const auto y0 = std::max(row.coordinate, rect.y);
        const auto y1 = std::min(row.coordinate + row.height, rect.top());
        if (y0 >= y1)
          continue;

        for (const auto &subrow : row.subrows) {
          const auto subrow_right = subrow.origin + static_cast<double>(subrow.site_count) * row.site_spacing;
          const auto x0 = std::max(subrow.origin, rect.x);
          const auto x1 = std::min(subrow_right, rect.right());

          if (x0 < x1)
            add_overlap(grid, {x0, y0, x1 - x0, y1 - y0}, Area::Placeable, -1.0);
        }
      }
    }
  }

  for (auto &bin : grid.bins)
    bin.placeable_area = std::max(0.0, bin.placeable_area);

  return grid;
}

PinDensityGrid Board::pin_density(double bin_size) const {
  if (!std::isfinite(bin_size) || bin_size <= 0)
    throw Error("pin density bin size must be finite and positive");

  auto min_x = std::numeric_limits<double>::infinity();
  auto min_y = std::numeric_limits<double>::infinity();
  auto max_x = -std::numeric_limits<double>::infinity();
  auto max_y = -std::numeric_limits<double>::infinity();

  for (const auto &row : rows) {
    for (const auto &subrow : row.subrows) {
      const Rectangle rect{subrow.origin, row.coordinate, static_cast<double>(subrow.site_count) * row.site_spacing, row.height};
      validate(rect);
      min_x = std::min(min_x, rect.x);
      min_y = std::min(min_y, rect.y);
      max_x = std::max(max_x, rect.right());
      max_y = std::max(max_y, rect.top());
    }
  }

  if (!std::isfinite(min_x) || max_x <= min_x || max_y <= min_y)
    throw Error("cannot calculate pin density without a non-empty placement region");

  const auto columns = static_cast<std::uint64_t>(std::ceil((max_x - min_x) / bin_size));
  const auto row_count = static_cast<std::uint64_t>(std::ceil((max_y - min_y) / bin_size));
  if (columns == 0 || row_count == 0 || columns > std::numeric_limits<std::size_t>::max() / row_count)
    throw Error("pin density grid is too large");

  PinDensityGrid grid{min_x, min_y, max_x, max_y, bin_size, columns, row_count, {}};
  grid.bins.resize(static_cast<std::size_t>(columns * row_count));
  for (std::uint64_t row = 0; row < row_count; ++row) {
    const auto height = std::min(bin_size, max_y - (min_y + static_cast<double>(row) * bin_size));
    for (std::uint64_t column = 0; column < columns; ++column) {
      const auto width = std::min(bin_size, max_x - (min_x + static_cast<double>(column) * bin_size));
      grid.bins[static_cast<std::size_t>(row * columns + column)].area = width * height;
    }
  }

  for (const auto &pin : pins) {
    if (pin.cell >= cells.size())
      throw Error("cannot calculate pin density with an invalid cell reference");
    const auto &cell = cells[pin.cell];
    if (!cell.location)
      continue;
    const auto point = pin_position(cell, pin);
    if (!std::isfinite(point.x) || !std::isfinite(point.y))
      throw Error("cannot calculate pin density for non-finite pin geometry");
    if (point.x < min_x || point.x > max_x || point.y < min_y || point.y > max_y)
      continue;
    const auto column = point.x == max_x ? columns - 1 : static_cast<std::uint64_t>((point.x - min_x) / bin_size);
    const auto row = point.y == max_y ? row_count - 1 : static_cast<std::uint64_t>((point.y - min_y) / bin_size);
    ++grid.bins[static_cast<std::size_t>(row * columns + column)].pin_count;
  }

  return grid;
}

} // namespace placement
