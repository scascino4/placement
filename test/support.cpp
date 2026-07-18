#include "support.hpp"

#include "placement/parsing/parser.hpp"

#include <chrono>
#include <fstream>
#include <iterator>

namespace placement::test {

TemporaryDirectory::TemporaryDirectory() {
  const auto id = std::chrono::steady_clock::now().time_since_epoch().count();
  path_ = std::filesystem::temp_directory_path() / ("placement-tests-" + std::to_string(id));
  std::filesystem::create_directories(path_);
}

TemporaryDirectory::~TemporaryDirectory() {
  std::error_code ignored;
  std::filesystem::remove_all(path_, ignored);
}

void write(const std::filesystem::path &path, std::string_view contents) {
  std::ofstream output(path, std::ios::binary);
  output << contents;
  if (!output)
    throw std::runtime_error("test could not write " + path.string());
}

std::string read(const std::filesystem::path &path) {
  std::ifstream input(path, std::ios::binary);
  return {std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
}

bool contains_parts(std::string_view contents, std::initializer_list<std::string_view> parts) {
  std::string expected;
  for (const auto part : parts)
    expected += part;
  return contents.find(expected) != std::string_view::npos;
}

std::string_view attribute_value(std::string_view contents, std::string_view name) {
  const auto prefix = std::string(name) + "=\"";
  const auto begin = contents.find(prefix);
  if (begin == std::string_view::npos)
    throw std::runtime_error("missing SVG attribute " + std::string(name));
  const auto value_begin = begin + prefix.size();
  const auto end = contents.find('"', value_begin);
  if (end == std::string_view::npos)
    throw std::runtime_error("unterminated SVG attribute " + std::string(name));
  return contents.substr(value_begin, end - value_begin);
}

void bookshelf_fixture(const std::filesystem::path &directory) {
  write(directory / "tiny.aux", "# arbitrary component order\n"
                                "RowBasedPlacement : tiny.pl tiny.scl tiny.wts tiny.nets tiny.nodes\n");
  write(directory / "tiny.nodes", "UCLA nodes 1.0\n"
                                  "# cells\n"
                                  "NumNodes : 4\nNumTerminals : 2\n"
                                  "a 2 4\nb 6 3 terminal\nc 5 2 terminal_NI\nd 1.5 2.5e0\n");
  write(directory / "tiny.nets", "UCLA nets 1.0\nNumNets : 2\nNumPins : 4\n"
                                 "NetDegree : 3 net0\n"
                                 "a I : -0.5 1\nb O : 2 0\nc B : 0 0\n"
                                 "NetDegree : 1 net1\nd U\n");
  write(directory / "tiny.wts", "UCLA wts 1.0\na 1 2\nb 3 4\nnet0 2.5\nnet1 1.5\n");
  write(directory / "tiny.scl", "UCLA scl 1.0\nNumRows : 1\nCoreRow Horizontal\n"
                                " Coordinate : 10\n Height : 4\n Sitespacing : 2\n"
                                " Siteorient : FS\n Sitesymmetry : X Y ROT90\n"
                                " SubrowOrigin : 5 NumSites : 3\n"
                                " SubrowOrigin : 20 NumSites : 2\nEnd\n");
  write(directory / "tiny.pl", "UCLA pl 1.0\n"
                               "a 10.5 20 : E\n"
                               "b 30 10 : N /FIXED\n"
                               "c 40 10 : FW /FIXED_NI DIMS=(7,8)\n");
}

void lefdef_fixture(const std::filesystem::path &directory) {
  write(directory / "tech.lef", "VERSION 5.8 ;\n"
                                "UNITS DATABASE MICRONS 1000 ; END UNITS\n"
                                "PROPERTYDEFINITIONS\n"
                                "  LIBRARY NOTE STRING ;\n"
                                "END PROPERTYDEFINITIONS\n"
                                "SITE core\n"
                                "  CLASS CORE ;\n"
                                "  SYMMETRY X Y ;\n"
                                "  SIZE 1 BY 2 ;\n"
                                "END core\n"
                                "END LIBRARY\n");
  write(directory / "cells.lef", "VERSION 5.8 ;\n"
                                 "MACRO NAND\n"
                                 "  PROPERTY NOTE \"embedded ; END NAND\" ;\n"
                                 "  CLASS CORE ;\n"
                                 "  ORIGIN 0 0 ;\n"
                                 "  SIZE 2 BY 2 ;\n"
                                 "  SITE core ;\n"
                                 "  PIN A DIRECTION INPUT ;\n"
                                 "    PORT\n"
                                 "      LAYER metal1 ;\n"
                                 "      RECT 0 0.5 0.5 1.5 ;\n"
                                 "    END\n"
                                 "  END A\n"
                                 "  PIN Y\n"
                                 "    DIRECTION OUTPUT ;\n"
                                 "    USE SIGNAL ;\n"
                                 "    PORT\n"
                                 "      LAYER metal1 ;\n"
                                 "      RECT 1.5 0.5 1.75 1.5 ;\n"
                                 "      RECT 1.75 0.75 2 1.25 ;\n"
                                 "    END\n"
                                 "  END Y\n"
                                 "END NAND\n"
                                 "MACRO BLOCK\n"
                                 "  CLASS BLOCK ;\n"
                                 "  ORIGIN 0 0 ;\n"
                                 "  SIZE 4 BY 4 ;\n"
                                 "  PIN B DIRECTION INPUT ;\n"
                                 "    PORT LAYER metal1 ; RECT 0 0 1 1 ; END\n"
                                 "  END B\n"
                                 "  OBS LAYER metal1 ; RECT 0 0 4 4 ; END\n"
                                 "END BLOCK\n"
                                 "END LIBRARY\n");
  write(directory / "design.def", "# compact LEF/DEF fixture\n"
                                  "VERSION 5.8 ;\n"
                                  "DESIGN tiny_def ;\n"
                                  "UNITS DISTANCE MICRONS 100 ;\n"
                                  "DIEAREA ( 0 0 ) ( 1000 400 ) ;\n"
                                  "ROW row0 core 0 0 FS DO 10 BY 1 STEP 100 0 ;\n"
                                  "TRACKS X 0 DO 10 STEP 100 LAYER metal1 ;\n"
                                  "COMPONENTS 2 ;\n"
                                  "  - u1 NAND + PLACED ( 100 0 ) FS ;\n"
                                  "  - block BLOCK + FIXED ( 600 0 ) N ;\n"
                                  "END COMPONENTS\n"
                                  "PINS 1 ;\n"
                                  "  - IN + NET n1\n"
                                  "    + DIRECTION INPUT\n"
                                  "    + PLACED ( 0 100 ) N\n"
                                  "    + LAYER metal1 ( -10 -10 ) ( 10 10 ) ;\n"
                                  "END PINS\n"
                                  "BLOCKAGES 1 ;\n"
                                  "  - PLACEMENT RECT ( 300 0 ) ( 500 200 ) ;\n"
                                  "END BLOCKAGES\n"
                                  "SPECIALNETS 0 ;\n"
                                  "END SPECIALNETS\n"
                                  "NETS 2 ;\n"
                                  "  - n1 ( PIN IN ) ( u1 A ) ;\n"
                                  "  - n2 ( u1 Y ) ( block B ) + NONDEFAULTRULE DWDS ;\n"
                                  "END NETS\n"
                                  "END DESIGN\n");
}

Board parse_bookshelf_fixture(const std::filesystem::path &directory) {
  auto parser = make_parser();
  return parser->parse(directory / "tiny.aux");
}

Board parse_lefdef_fixture(const std::filesystem::path &directory) {
  LefDefParseOptions options;
  options.lef_files = {directory / "tech.lef", directory / "cells.lef"};
  auto parser = make_parser(std::move(options));
  return parser->parse(directory / "design.def");
}

} // namespace placement::test
