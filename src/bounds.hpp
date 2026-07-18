#pragma once

#include "placement/model.hpp"

#include <algorithm>
#include <cmath>
#include <limits>

namespace placement::detail {

struct Bounds {
  double min_x{std::numeric_limits<double>::infinity()};
  double min_y{std::numeric_limits<double>::infinity()};
  double max_x{-std::numeric_limits<double>::infinity()};
  double max_y{-std::numeric_limits<double>::infinity()};

  void include(double x0, double y0, double x1, double y1) {
    min_x = std::min({min_x, x0, x1});
    min_y = std::min({min_y, y0, y1});
    max_x = std::max({max_x, x0, x1});
    max_y = std::max({max_y, y0, y1});
  }

  void include(const PlacedRectangle &rect) { include(rect.x, rect.y, rect.right(), rect.top()); }

  [[nodiscard]] bool empty() const { return !std::isfinite(min_x); }
  [[nodiscard]] bool has_area() const { return !empty() && max_x > min_x && max_y > min_y; }
};

} // namespace placement::detail
