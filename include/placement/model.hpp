#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace placement {

enum class CellKind : std::uint8_t { Movable, Terminal, TerminalNonInteracting };
enum class PlacementStatus : std::uint8_t { Movable, Fixed, FixedNonInteracting };
enum class Orientation : std::uint8_t { N, E, S, W, FN, FE, FS, FW };
enum class PinDirection : std::uint8_t { Unknown, Input, Output, Bidirectional };

struct Location {
  double x{};
  double y{};
  Orientation orientation{Orientation::N};
  PlacementStatus status{PlacementStatus::Movable};

  // Some Bookshelf placements override the dimensions declared by the cell.
  // Keeping that detail on the placement preserves the format-neutral cell.
  std::optional<double> width;
  std::optional<double> height;
};

struct Cell {
  std::string name;
  double width{};
  double height{};
  CellKind kind{CellKind::Movable};
  std::optional<Location> location;
  std::vector<double> weights;
};

struct Subrow {
  double origin{};
  std::uint64_t site_count{};
};

struct Row {
  double coordinate{};
  double height{};
  double site_width{};
  double site_spacing{};
  Orientation site_orientation{Orientation::N};
  std::uint8_t symmetry{}; // bit 0: X, bit 1: Y, bit 2: ROT90
  std::vector<Subrow> subrows;
};

struct Pin {
  std::uint32_t cell{};
  PinDirection direction{PinDirection::Unknown};
  double offset_x{};
  double offset_y{};
};

struct Net {
  std::string name;

  // Nets reference contiguous ranges in Board::pins. Flattening connectivity
  // avoids a vector allocation per net on very large designs.
  std::uint64_t first_pin{};
  std::uint64_t pin_count{};
  std::vector<double> weights;
};

struct UtilizationBin {
  double movable_area{};
  double placeable_area{};

  // Bins without usable row area have no utilization value.
  [[nodiscard]] std::optional<double> utilization() const;
};

struct UtilizationGrid {
  double min_x{};
  double min_y{};
  double max_x{};
  double max_y{};
  double bin_size{};
  std::uint64_t columns{};
  std::uint64_t rows{};
  // Row-major bins, starting at the placement region's lower-left corner.
  std::vector<UtilizationBin> bins;

  [[nodiscard]] const UtilizationBin &at(std::uint64_t column, std::uint64_t row) const;
};

struct PinDensityBin {
  std::uint64_t pin_count{};
  double area{};

  [[nodiscard]] double density() const;
};

struct PinDensityGrid {
  double min_x{};
  double min_y{};
  double max_x{};
  double max_y{};
  double bin_size{};
  std::uint64_t columns{};
  std::uint64_t rows{};
  // Row-major bins, starting at the placement region's lower-left corner.
  std::vector<PinDensityBin> bins;

  [[nodiscard]] const PinDensityBin &at(std::uint64_t column, std::uint64_t row) const;
};

struct Board {
  std::string name;
  std::vector<Cell> cells;
  std::vector<Row> rows;
  std::vector<Net> nets;
  std::vector<Pin> pins;

  [[nodiscard]] UtilizationGrid utilization(double bin_size) const;
  [[nodiscard]] PinDensityGrid pin_density(double bin_size) const;
};

} // namespace placement
