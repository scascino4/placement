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
  std::uint64_t first_pin{};
  std::uint64_t pin_count{};
  std::vector<double> weights;
};

struct Board {
  std::string name;
  std::vector<Cell> cells;
  std::vector<Row> rows;
  std::vector<Net> nets;
  std::vector<Pin> pins;
};

} // namespace placement
