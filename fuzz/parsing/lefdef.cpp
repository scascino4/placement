#include "../support.hpp"

#include "placement/error.hpp"
#include "placement/parsing/parser.hpp"

namespace placement::fuzz {
namespace {

constexpr std::string_view tech =
    "VERSION 5.8 ;\nUNITS DATABASE MICRONS 1000 ; END UNITS\nSITE core\n CLASS CORE ;\n SIZE 1 BY 2 ;\nEND core\nEND LIBRARY\n";
constexpr std::string_view cells = "VERSION 5.8 ;\nMACRO NAND\n CLASS CORE ;\n SIZE 2 BY 2 ;\n SITE core ;\n PIN A\n  DIRECTION INPUT ;\n  PORT\n   "
                                   "LAYER metal1 ;\n   RECT 0 0 1 1 ;\n  END\n END A\nEND NAND\nEND LIBRARY\n";
constexpr std::string_view design =
    "VERSION 5.8 ;\nDESIGN tiny ;\nUNITS DISTANCE MICRONS 100 ;\nDIEAREA ( 0 0 ) ( 1000 400 ) ;\nROW row0 core 0 0 N DO 10 BY 1 STEP 100 0 "
    ";\nCOMPONENTS 1 ;\n - u1 NAND + PLACED ( 100 0 ) N ;\nEND COMPONENTS\nNETS 1 ;\n - n1 ( u1 A ) ;\nEND NETS\nEND DESIGN\n";

} // namespace

void fuzz_one(Input input) {
  const auto dir = work_dir() / "lefdef";
  std::filesystem::create_directories(dir);
  const auto tech_path = dir / "tech.lef";
  const auto cells_path = dir / "cells.lef";
  write_file(tech_path, mutate(tech, input, 11));
  write_file(cells_path, mutate(cells, input, 12));
  write_file(dir / "design.def", mutate(design, input, 13));

  try {
    LefDefParseOptions opts;
    opts.lef_files = {tech_path, cells_path};
    (void)make_parser(std::move(opts))->parse(dir / "design.def");
  } catch (const Error &) {
  }
}

} // namespace placement::fuzz
