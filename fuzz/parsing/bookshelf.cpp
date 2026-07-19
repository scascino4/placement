#include "../support.hpp"

#include "placement/error.hpp"
#include "placement/parsing/parser.hpp"

namespace placement::fuzz {
namespace {

constexpr std::string_view aux = "RowBasedPlacement : tiny.pl tiny.scl tiny.wts tiny.nets tiny.nodes\n";
constexpr std::string_view nodes = "UCLA nodes 1.0\nNumNodes : 2\nNumTerminals : 1\na 2 4\nb 6 3 terminal\n";
constexpr std::string_view nets = "UCLA nets 1.0\nNumNets : 1\nNumPins : 2\nNetDegree : 2 net0\na I : -0.5 1\nb O : 2 0\n";
constexpr std::string_view weights = "UCLA wts 1.0\na 1\nb 2\nnet0 3\n";
constexpr std::string_view rows = "UCLA scl 1.0\nNumRows : 1\nCoreRow Horizontal\n Coordinate : 0\n Height : 4\n Sitespacing : 1\n Siteorient : N\n "
                                  "SubrowOrigin : 0 NumSites : 16\nEnd\n";
constexpr std::string_view placement = "UCLA pl 1.0\na 1 0 : N\nb 5 0 : N /FIXED\n";

} // namespace

void fuzz_one(Input input) {
  const auto dir = work_dir() / "bookshelf";
  std::filesystem::create_directories(dir);
  write_file(dir / "tiny.aux", mutate(aux, input, 1));
  write_file(dir / "tiny.nodes", mutate(nodes, input, 2));
  write_file(dir / "tiny.nets", mutate(nets, input, 3));
  write_file(dir / "tiny.wts", mutate(weights, input, 4));
  write_file(dir / "tiny.scl", mutate(rows, input, 5));
  write_file(dir / "tiny.pl", mutate(placement, input, 6));

  try {
    (void)make_parser()->parse(dir / "tiny.aux");
  } catch (const Error &) {
  }
}

} // namespace placement::fuzz
