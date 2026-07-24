#include "suites.hpp"

#include <limits>

namespace placement::test {
namespace {

Row row(double width, double height) {
  Row res;
  res.height = height;
  res.site_spacing = 1;
  res.subrows.push_back({0, static_cast<std::uint64_t>(width)});
  return res;
}

void orient_offset_test() {
  const auto check_offset = [](Orientation orientation, double expected_x, double expected_y) {
    const auto [x, y] = orient_offset(2, 3, orientation);
    check(x == expected_x && y == expected_y, "oriented offset");
  };

  check_offset(Orientation::N, 2, 3);
  check_offset(Orientation::E, 3, -2);
  check_offset(Orientation::S, -2, -3);
  check_offset(Orientation::W, -3, 2);
  check_offset(Orientation::FN, -2, 3);
  check_offset(Orientation::FE, 3, 2);
  check_offset(Orientation::FS, 2, -3);
  check_offset(Orientation::FW, -3, -2);
}

void placed_rectangle_test() {
  Cell cell;
  cell.width = 3;
  cell.height = 7;
  cell.location.emplace();
  cell.location->x = 2;
  cell.location->y = 5;

  for (const auto orientation : {Orientation::N, Orientation::S, Orientation::FN, Orientation::FS}) {
    cell.location->orientation = orientation;
    const auto rect = placed_rectangle(cell);
    check(rect.x == 2 && rect.y == 5 && rect.width == 3 && rect.height == 7, "unrotated cell footprint");
  }
  for (const auto orientation : {Orientation::E, Orientation::W, Orientation::FE, Orientation::FW}) {
    cell.location->orientation = orientation;
    const auto rect = placed_rectangle(cell);
    check(rect.width == 7 && rect.height == 3, "rotated cell footprint");
  }

  cell.location->width = 11;
  cell.location->height = 13;
  cell.location->orientation = Orientation::E;
  const auto overridden = placed_rectangle(cell);
  check(overridden.width == 13 && overridden.height == 11, "placement dimensions are applied before rotation");

  Row placement_row;
  placement_row.coordinate = 7;
  placement_row.height = 2;
  placement_row.site_width = 1;
  placement_row.site_spacing = 3;
  const auto row_rect = subrow_rectangle(placement_row, {5, 4});
  check(row_rect.x == 5 && row_rect.y == 7 && row_rect.width == 12 && row_rect.height == 2, "subrow extent uses site spacing");
}

void utilization_test() {
  Board board;
  board.rows.push_back(row(20, 10));

  Cell movable;
  movable.name = "movable";
  movable.width = 10;
  movable.height = 5;
  movable.location.emplace();
  movable.location->x = 5;
  board.cells.push_back(movable);

  Cell macro;
  macro.name = "macro";
  macro.width = 2;
  macro.height = 10;
  macro.macro = true;
  macro.location.emplace();
  macro.location->status = PlacementStatus::Movable;
  board.cells.push_back(macro);

  const auto grid = board.utilization(10);
  check(grid.columns == 2 && grid.rows == 1 && grid.bins.size() == 2, "utilization grid dimensions");
  check(close(grid.at(0, 0).movable_area, 45) && close(grid.at(1, 0).movable_area, 25), "movable cells and macros contribute utilization");
  check(close(grid.at(0, 0).placeable_area, 100) && close(grid.at(1, 0).placeable_area, 100), "movable macros do not reduce placeable area");
  check(close(*grid.at(0, 0).utilization(), 0.45) && close(*grid.at(1, 0).utilization(), 0.25), "utilization ratios");

  const auto placeable_grid = board.utilization(10, false);
  check(close(placeable_grid.at(0, 0).movable_area, 0) && close(placeable_grid.at(1, 0).movable_area, 0),
        "placeability-only grids skip movable overlap");

  Board fragmented;
  auto fragmented_row = row(0, 10);
  fragmented_row.subrows = {{0, 4}, {6, 4}};
  fragmented.rows.push_back(fragmented_row);
  macro.width = 4;
  macro.location->x = 3;
  macro.location->status = PlacementStatus::Fixed;
  fragmented.cells.push_back(macro);
  const auto fragmented_grid = fragmented.utilization(10);
  check(close(fragmented_grid.at(0, 0).placeable_area, 60), "fixed blockage excludes only its intersection with legal rows");

  expect_error([&] { (void)board.utilization(0); }, "finite and positive");
  expect_error([&] { (void)grid.at(2, 0); }, "out of bounds");
}

void pin_density_test() {
  Board board;
  board.rows.push_back(row(40, 20));

  Cell north;
  north.width = 4;
  north.height = 2;
  north.location.emplace();
  board.cells.push_back(north);

  Cell east = north;
  east.location->x = 20;
  east.location->orientation = Orientation::E;
  board.cells.push_back(east);

  board.pins.push_back({0, PinDirection::Input, 1, 0});
  board.pins.push_back({1, PinDirection::Output, 0, 6});
  board.cells.emplace_back();
  board.pins.push_back({2, PinDirection::Unknown, 0, 0});

  const auto grid = board.pin_density(10);
  check(grid.columns == 4 && grid.rows == 2 && grid.bins.size() == 8, "pin density grid dimensions");
  check(grid.at(0, 0).pin_count == 1 && grid.at(2, 0).pin_count == 1, "oriented pins are assigned to bins");
  check(close(grid.at(0, 0).density(), 0.01), "pin density uses clipped bin area");
  expect_error([&] { (void)board.pin_density(0); }, "finite and positive");
  expect_error([&] { (void)grid.at(4, 0); }, "out of bounds");

  board.pins.push_back({99, PinDirection::Unknown, 0, 0});
  expect_error([&] { (void)board.pin_density(10); }, "invalid cell reference");
}

void cell_density_test() {
  Board board;
  board.rows.push_back(row(25, 10));

  Cell movable;
  movable.width = 10;
  movable.height = 10;
  movable.location.emplace();
  movable.location->x = 15;
  board.cells.push_back(movable);

  Cell macro = movable;
  macro.macro = true;
  macro.location->x = 0;
  macro.location->status = PlacementStatus::Fixed;
  board.cells.push_back(macro);

  Cell non_interacting = movable;
  non_interacting.kind = CellKind::TerminalNonInteracting;
  non_interacting.location->x = 5;
  non_interacting.location->status = PlacementStatus::FixedNonInteracting;
  board.cells.push_back(non_interacting);

  Cell unplaced = movable;
  unplaced.location.reset();
  board.cells.push_back(unplaced);

  const auto grid = board.cell_density(10);
  check(grid.columns == 3 && grid.rows == 1 && grid.bins.size() == 3, "cell density grid dimensions");
  const auto edge = grid.bin_rectangle(2, 0);
  check(edge.x == 20 && edge.y == 0 && edge.width == 5 && edge.height == 10, "density grid exposes clipped bin geometry");
  check(!grid.at(0, 0).density() && close(grid.at(0, 0).movable_area, 0) && close(grid.at(0, 0).available_area, 0),
        "fixed macros consume capacity without contributing density");
  check(close(grid.at(1, 0).movable_area, 50) && close(*grid.at(1, 0).density(), 0.5), "cell density splits movable cells at bin boundaries");
  check(close(grid.at(2, 0).available_area, 50) && close(*grid.at(2, 0).density(), 1),
        "cell density clips edge bins and excludes non-interacting cells");

  board.cells[1].location->status = PlacementStatus::Movable;
  const auto macro_grid = board.cell_density(10);
  check(close(*macro_grid.at(0, 0).density(), 1) && close(macro_grid.at(0, 0).movable_area, 100) && close(macro_grid.at(0, 0).available_area, 100),
        "movable macros contribute cell density");
  expect_error([&] { (void)board.cell_density(0); }, "finite and positive");
  expect_error([&] { (void)grid.at(3, 0); }, "out of bounds");
}

void density_validation_test() {
  Board empty;
  expect_error([&] { (void)empty.utilization(10); }, "without a non-empty placement region");
  expect_error([&] { (void)empty.pin_density(10); }, "without a non-empty placement region");
  expect_error([&] { (void)empty.cell_density(10); }, "without a non-empty placement region");

  Board board;
  board.rows.push_back(row(10, 10));
  board.cells.emplace_back();
  board.cells[0].width = 1;
  board.cells[0].height = 1;
  board.cells[0].location.emplace();
  board.cells[0].location->x = std::numeric_limits<double>::quiet_NaN();
  expect_error([&] { (void)board.utilization(10); }, "non-finite or negative geometry");
  expect_error([&] { (void)board.cell_density(10); }, "non-finite or negative geometry");

  board.cells[0].location->x = 9;
  board.pins.push_back({0, PinDirection::Input, std::numeric_limits<double>::infinity(), 0});
  expect_error([&] { (void)board.pin_density(10); }, "non-finite pin geometry");

  board.pins[0].offset_x = 0.5;
  const auto boundary = board.pin_density(10);
  check(boundary.at(0, 0).pin_count == 1, "pins on the upper grid boundary belong to the last bin");
  check(PinDensityBin{}.density() == 0, "zero-area pin bins have zero density");
}

} // namespace

Tests model_tests() {
  return {{"oriented offset", orient_offset_test},  {"oriented cell footprint", placed_rectangle_test},
          {"utilization grid", utilization_test},   {"pin density grid", pin_density_test},
          {"cell density grid", cell_density_test}, {"density validation", density_validation_test}};
}

} // namespace placement::test
