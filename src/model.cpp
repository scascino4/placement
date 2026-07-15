#include "placement/model.hpp"

#include "placement/error.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <numeric>
#include <string_view>
#include <tuple>
#include <utility>

namespace placement {
PlacedRectangle placed_rectangle(const Cell &cell) {
  const auto &location = *cell.location;
  double width = location.width.value_or(cell.width);
  double height = location.height.value_or(cell.height);
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

namespace {

struct Point {
  double x{};
  double y{};
};

void validate(const PlacedRectangle &rect);

struct Bounds {
  double min_x{std::numeric_limits<double>::infinity()};
  double min_y{std::numeric_limits<double>::infinity()};
  double max_x{-std::numeric_limits<double>::infinity()};
  double max_y{-std::numeric_limits<double>::infinity()};

  void include(const PlacedRectangle &rect) {
    validate(rect);
    min_x = std::min(min_x, rect.x);
    min_y = std::min(min_y, rect.y);
    max_x = std::max(max_x, rect.right());
    max_y = std::max(max_y, rect.top());
  }

  [[nodiscard]] bool empty() const { return !std::isfinite(min_x) || max_x <= min_x || max_y <= min_y; }
};

[[nodiscard]] bool movable(const Cell &cell) {
  return cell.kind == CellKind::Movable && cell.location && cell.location->status == PlacementStatus::Movable;
}

[[nodiscard]] bool fixed(const Cell &cell) { return cell.location && !movable(cell); }

[[nodiscard]] Point pin_position(const Cell &cell, const Pin &pin) {
  const auto rect = placed_rectangle(cell);
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
  return {rect.x + rect.width / 2.0 + x, rect.y + rect.height / 2.0 + y};
}

enum class Area { Movable,
                  Placeable };

void validate(const PlacedRectangle &rect) {
  if (!std::isfinite(rect.x) || !std::isfinite(rect.y) || !std::isfinite(rect.width) || !std::isfinite(rect.height) || rect.width < 0 ||
      rect.height < 0)
    throw Error("cannot calculate placement density for non-finite or negative geometry");
}

[[nodiscard]] Bounds placement_bounds(const std::vector<Row> &rows) {
  Bounds bounds;
  for (const auto &row : rows)
    for (const auto &subrow : row.subrows)
      bounds.include({subrow.origin, row.coordinate, static_cast<double>(subrow.site_count) * row.site_spacing, row.height});
  return bounds;
}

template <typename Grid>
struct GridTraits;

template <>
struct GridTraits<UtilizationGrid> {
  static constexpr std::string_view kind = "utilization";
};

template <>
struct GridTraits<PinDensityGrid> {
  static constexpr std::string_view kind = "pin density";
};

template <>
struct GridTraits<CellDensityGrid> {
  static constexpr std::string_view kind = "cell density";
};

template <typename Grid>
[[nodiscard]] Grid make_grid(const std::vector<Row> &rows, double bin_size) {
  constexpr auto kind = GridTraits<Grid>::kind;
  if (!std::isfinite(bin_size) || bin_size <= 0)
    throw Error(std::string(kind) + " bin size must be finite and positive");

  const auto bounds = placement_bounds(rows);
  if (bounds.empty())
    throw Error("cannot calculate " + std::string(kind) + " without a non-empty placement region");

  const auto columns = static_cast<std::uint64_t>(std::ceil((bounds.max_x - bounds.min_x) / bin_size));
  const auto row_count = static_cast<std::uint64_t>(std::ceil((bounds.max_y - bounds.min_y) / bin_size));
  if (columns == 0 || row_count == 0 || columns > std::numeric_limits<std::size_t>::max() / row_count)
    throw Error(std::string(kind) + " grid is too large");

  Grid grid{bounds.min_x, bounds.min_y, bounds.max_x, bounds.max_y, bin_size, columns, row_count, {}};
  grid.bins.resize(static_cast<std::size_t>(columns * row_count));
  return grid;
}

template <typename Grid, typename Function>
void for_each_bin(Grid &grid, Function function) {
  for (std::uint64_t row = 0; row < grid.rows; ++row) {
    const auto height = std::min(grid.bin_size, grid.max_y - (grid.min_y + static_cast<double>(row) * grid.bin_size));
    for (std::uint64_t column = 0; column < grid.columns; ++column) {
      const auto width = std::min(grid.bin_size, grid.max_x - (grid.min_x + static_cast<double>(column) * grid.bin_size));
      function(grid.bins[static_cast<std::size_t>(row * grid.columns + column)], width * height);
    }
  }
}

template <typename Grid>
[[nodiscard]] const auto &bin_at(const Grid &grid, std::uint64_t column, std::uint64_t row) {
  if (column >= grid.columns || row >= grid.rows)
    throw Error(std::string(GridTraits<Grid>::kind) + " bin index is out of bounds");
  return grid.bins[static_cast<std::size_t>(row * grid.columns + column)];
}

template <typename Grid, typename Accumulate>
void add_overlap(Grid &grid, const PlacedRectangle &rect, Accumulate accumulate) {
  validate(rect);
  if (rect.width == 0 || rect.height == 0)
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
      auto &bin = grid.bins[static_cast<std::size_t>(row * grid.columns + col)];
      accumulate(bin, overlap_w * overlap_h);
    }
  }
}

void add_overlap(UtilizationGrid &grid, const PlacedRectangle &rect, Area area, double scale = 1.0) {
  add_overlap(grid, rect, [area, scale](UtilizationBin &bin, double overlap) {
    if (area == Area::Movable)
      bin.movable_area += scale * overlap;
    else
      bin.placeable_area += scale * overlap;
  });
}

} // namespace

std::optional<double> UtilizationBin::utilization() const {
  if (placeable_area <= 0)
    return std::nullopt;
  return movable_area / placeable_area;
}

const UtilizationBin &UtilizationGrid::at(std::uint64_t column, std::uint64_t row) const { return bin_at(*this, column, row); }

double PinDensityBin::density() const {
  if (area <= 0)
    return 0;
  return static_cast<double>(pin_count) / area;
}

const PinDensityBin &PinDensityGrid::at(std::uint64_t column, std::uint64_t row) const { return bin_at(*this, column, row); }

std::optional<double> CellDensityBin::density() const {
  if (available_area <= 0)
    return std::nullopt;
  return movable_area / available_area;
}

const CellDensityBin &CellDensityGrid::at(std::uint64_t column, std::uint64_t row) const { return bin_at(*this, column, row); }

UtilizationGrid Board::utilization(double bin_size) const {
  auto grid = make_grid<UtilizationGrid>(rows, bin_size);

  for (const auto &row : rows)
    for (const auto &subrow : row.subrows)
      add_overlap(grid, {subrow.origin, row.coordinate, static_cast<double>(subrow.site_count) * row.site_spacing, row.height}, Area::Placeable);

  std::vector<std::size_t> row_order(rows.size());
  std::iota(row_order.begin(), row_order.end(), 0);
  std::sort(row_order.begin(), row_order.end(), [this](std::size_t lhs, std::size_t rhs) { return rows[lhs].coordinate < rows[rhs].coordinate; });

  double max_row_height = 0;
  for (const auto &row : rows)
    max_row_height = std::max(max_row_height, row.height);

  const auto subtract_row_overlap = [&](const PlacedRectangle &rect) {
    // Only rows that can intersect the blockage need to be examined.
    const auto first = std::ranges::lower_bound(row_order, rect.y - max_row_height, {}, [this](std::size_t index) { return rows[index].coordinate; });

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
  };

  for (const auto &cell : cells) {
    if (!cell.location)
      continue;
    const auto rect = placed_rectangle(cell);
    if (cell.macro) {
      // Macros occupy row area regardless of whether they are movable in the
      // current placement. Standard-cell utilization excludes their footprint.
      subtract_row_overlap(rect);
    } else if (movable(cell)) {
      add_overlap(grid, rect, Area::Movable);
    } else if (fixed(cell)) {
      subtract_row_overlap(rect);
    }
  }

  for (auto &bin : grid.bins)
    bin.placeable_area = std::max(0.0, bin.placeable_area);

  return grid;
}

PinDensityGrid Board::pin_density(double bin_size) const {
  auto grid = make_grid<PinDensityGrid>(rows, bin_size);
  for_each_bin(grid, [](PinDensityBin &bin, double area) { bin.area = area; });

  for (const auto &pin : pins) {
    if (pin.cell >= cells.size())
      throw Error("cannot calculate pin density with an invalid cell reference");
    const auto &cell = cells[pin.cell];
    if (!cell.location)
      continue;
    const auto point = pin_position(cell, pin);
    if (!std::isfinite(point.x) || !std::isfinite(point.y))
      throw Error("cannot calculate pin density for non-finite pin geometry");
    if (point.x < grid.min_x || point.x > grid.max_x || point.y < grid.min_y || point.y > grid.max_y)
      continue;
    const auto column = point.x == grid.max_x ? grid.columns - 1 : static_cast<std::uint64_t>((point.x - grid.min_x) / bin_size);
    const auto row = point.y == grid.max_y ? grid.rows - 1 : static_cast<std::uint64_t>((point.y - grid.min_y) / bin_size);
    ++grid.bins[static_cast<std::size_t>(row * grid.columns + column)].pin_count;
  }

  return grid;
}

CellDensityGrid Board::cell_density(double bin_size) const {
  auto grid = make_grid<CellDensityGrid>(rows, bin_size);
  for_each_bin(grid, [](CellDensityBin &bin, double area) { bin.available_area = area; });

  for (const auto &cell : cells) {
    if (!cell.location || cell.kind == CellKind::TerminalNonInteracting || cell.location->status == PlacementStatus::FixedNonInteracting)
      continue;
    if (movable(cell) && !cell.macro)
      add_overlap(grid, placed_rectangle(cell), [](CellDensityBin &bin, double overlap) { bin.movable_area += overlap; });
    else
      add_overlap(grid, placed_rectangle(cell), [](CellDensityBin &bin, double overlap) { bin.available_area -= overlap; });
  }

  for (auto &bin : grid.bins)
    bin.available_area = std::max(0.0, bin.available_area);

  return grid;
}

} // namespace placement
