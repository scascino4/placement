#include "placement/parsing/parser.hpp"

#include "placement/error.hpp"

#include <algorithm>
#include <bit>
#include <cctype>
#include <charconv>
#include <cmath>
#include <fstream>
#include <functional>
#include <limits>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <type_traits>
#include <unordered_set>

namespace placement {
namespace {

void trim(std::string &text) {
  const auto first = text.find_first_not_of(" \t\r\n");
  if (first == std::string_view::npos) {
    text.clear();
    return;
  }
  const auto last = text.find_last_not_of(" \t\r\n");
  if (last + 1 < text.size())
    text.resize(last + 1);
  if (first != 0)
    text.erase(0, first);
}

void tokens(const std::string &line, std::vector<std::string_view> &result) {
  // Views avoid copying every field. Callers consume them before `line` is
  // changed by the next read. Reusing the vector also avoids an allocation
  // for every record in multi-million-line benchmark files.
  result.clear();
  std::size_t pos = 0;
  while (pos < line.size()) {
    pos = line.find_first_not_of(" \t\r\n", pos);
    if (pos == std::string::npos)
      break;

    const auto end = line.find_first_of(" \t\r\n", pos);
    result.emplace_back(line.data() + pos, (end == std::string::npos ? line.size() : end) - pos);
    pos = end == std::string::npos ? line.size() : end;
  }
}

class Lines {
public:
  explicit Lines(const std::filesystem::path &path) : path_(path), input_(path) {
    if (!input_) {
      throw Error("cannot open " + path.string());
    }
  }

  bool next(std::string &line) {
    while (std::getline(input_, line)) {
      ++number_;

      const auto comment = line.find('#');
      if (comment != std::string::npos) {
        line.erase(comment);
      }

      trim(line);
      if (!line.empty()) {
        return true;
      }
    }

    return false;
  }

  [[noreturn]] void fail(std::string_view message) const {
    throw Error(path_.string() + ':' + std::to_string(number_) + ": " + std::string(message));
  }

  [[nodiscard]] const std::filesystem::path &path() const { return path_; }

private:
  std::filesystem::path path_;
  std::ifstream input_;
  std::uint64_t number_{};
};

template <typename T> [[nodiscard]] T number(std::string_view token, const Lines &lines, std::string_view description) {
  T value{};
  const auto *begin = token.data();
  const auto *end = begin + token.size();
  const auto [ptr, error] = std::from_chars(begin, end, value);
  if (error != std::errc{} || ptr != end) {
    lines.fail("invalid " + std::string(description) + " '" + std::string(token) + "'");
  }
  if constexpr (std::is_floating_point_v<T>) {
    if (!std::isfinite(value))
      lines.fail("non-finite " + std::string(description) + " is not allowed");
  }
  return value;
}

[[nodiscard]] std::string lower(std::string_view value) {
  std::string result(value);
  std::ranges::transform(result, result.begin(), [](unsigned char character) { return static_cast<char>(std::tolower(character)); });
  return result;
}

[[nodiscard]] Orientation orientation(std::string_view value, const Lines &lines) {
  const auto normalized = lower(value);
  if (normalized == "n" || normalized == "1")
    return Orientation::N;
  if (normalized == "e")
    return Orientation::E;
  if (normalized == "s")
    return Orientation::S;
  if (normalized == "w")
    return Orientation::W;
  if (normalized == "fn")
    return Orientation::FN;
  if (normalized == "fe")
    return Orientation::FE;
  if (normalized == "fs")
    return Orientation::FS;
  if (normalized == "fw")
    return Orientation::FW;
  lines.fail("unknown orientation '" + std::string(value) + "'");
}

[[nodiscard]] PinDirection pin_direction(std::string_view value, const Lines &lines) {
  const auto normalized = lower(value);
  if (normalized == "i")
    return PinDirection::Input;
  if (normalized == "o")
    return PinDirection::Output;
  if (normalized == "b")
    return PinDirection::Bidirectional;
  if (normalized == "u")
    return PinDirection::Unknown;
  lines.fail("unknown pin direction '" + std::string(value) + "'");
}

void require_header(Lines &lines, std::string_view expected) {
  std::string line;
  std::vector<std::string_view> fields;
  if (!lines.next(line)) {
    lines.fail("empty component file");
  }
  tokens(line, fields);
  if (fields.size() < 3 || lower(fields[0]) != "ucla" || lower(fields[1]) != expected) {
    lines.fail("expected 'UCLA " + std::string(expected) + " <version>' header");
  }
}

struct Components {
  std::filesystem::path nodes;
  std::filesystem::path nets;
  std::optional<std::filesystem::path> weights;
  std::filesystem::path placement;
  std::filesystem::path rows;
};

[[nodiscard]] Components parse_aux(const std::filesystem::path &path) {
  Lines lines(path);
  std::string line;
  std::vector<std::string_view> fields;

  if (!lines.next(line)) {
    lines.fail("empty AUX manifest");
  }

  tokens(line, fields);
  if (fields.size() < 3 || fields[1] != ":") {
    lines.fail("expected '<format> : <component files>'");
  }

  const auto format = lower(fields[0]);
  if (format != "rowbasedplacement" && format != "stdcellplacement") {
    lines.fail("unsupported AUX format '" + std::string(fields[0]) + "'");
  }

  Components components;
  std::unordered_set<std::string> suffixes;
  for (std::size_t i = 2; i < fields.size(); ++i) {
    const std::filesystem::path component(fields[i]);

    const auto suffix = lower(component.extension().string());
    if (!suffixes.insert(suffix).second) {
      lines.fail("duplicate component suffix '" + suffix + "'");
    }

    const auto resolved = path.parent_path() / component;
    if (suffix == ".nodes")
      components.nodes = resolved;
    else if (suffix == ".nets")
      components.nets = resolved;
    else if (suffix == ".wts")
      components.weights = resolved;
    else if (suffix == ".pl")
      components.placement = resolved;
    else if (suffix == ".scl")
      components.rows = resolved;
    else
      lines.fail("unsupported component suffix '" + suffix + "'");
  }

  if (components.nodes.empty() || components.nets.empty() || components.placement.empty() || components.rows.empty()) {
    lines.fail("manifest requires .nodes, .nets, .pl, and .scl components");
  }

  return components;
}

void parse_nodes(const std::filesystem::path &path, Board &board) {
  Lines lines(path);
  require_header(lines, "nodes");
  std::optional<std::uint64_t> declared_nodes;
  std::optional<std::uint64_t> declared_terminals;
  std::uint64_t terminal_count = 0;
  std::string line;
  std::vector<std::string_view> fields;

  while (lines.next(line)) {
    tokens(line, fields);
    if (fields[0] == "NumNodes") {
      if (fields.size() < 3 || fields[1] != ":")
        lines.fail("malformed NumNodes");

      declared_nodes = number<std::uint64_t>(fields[2], lines, "node count");
      if (*declared_nodes > std::numeric_limits<std::uint32_t>::max())
        lines.fail("node count exceeds placement model limit");

      board.cells.reserve(static_cast<std::size_t>(*declared_nodes));
      continue;
    }

    if (fields[0] == "NumTerminals") {
      if (fields.size() < 3 || fields[1] != ":")
        lines.fail("malformed NumTerminals");
      declared_terminals = number<std::uint64_t>(fields[2], lines, "terminal count");
      continue;
    }

    if (!declared_nodes)
      lines.fail("NumNodes must precede node records");
    if (fields.size() < 3)
      lines.fail("node record requires name, width, and height");

    Cell cell;
    cell.name = fields[0];
    cell.width = number<double>(fields[1], lines, "cell width");
    cell.height = number<double>(fields[2], lines, "cell height");

    if (cell.width < 0 || cell.height < 0)
      lines.fail("cell dimensions cannot be negative");

    if (fields.size() >= 4) {
      const auto kind = lower(fields[3]);
      if (kind == "terminal") {
        cell.kind = CellKind::Terminal;
        // Row-based Bookshelf benchmarks use terminal nodes for physical
        // macros. Keep that physical identity even when a placement override
        // makes those macros movable.
        cell.macro = true;
      } else if (kind == "terminal_ni")
        cell.kind = CellKind::TerminalNonInteracting;
      else
        lines.fail("unknown node kind '" + std::string(fields[3]) + "'");
      ++terminal_count;
    }

    board.cells.push_back(std::move(cell));
    if (board.cells.size() > *declared_nodes)
      lines.fail("more nodes than declared");
  }

  if (!declared_nodes || !declared_terminals)
    lines.fail("missing node declarations");
  if (board.cells.size() != *declared_nodes)
    lines.fail("NumNodes does not match parsed node records");
  if (terminal_count != *declared_terminals)
    lines.fail("NumTerminals does not match parsed terminal records");
}

template <typename Record> class NameIndex {
public:
  NameIndex(const std::vector<Record> &records, const std::filesystem::path &path, std::string_view kind) : records_(records) {
    if (records.size() >= EMPTY)
      throw Error(path.string() + ": " + std::string(kind) + " count exceeds placement model limit");

    // Open addressing stores one compact integer per slot. In contrast,
    // std::unordered_map allocates a separate node for every name, which is
    // particularly expensive for designs with millions of cells and nets.
    const auto required = records.size() + records.size() / 2 + 1;
    slots_.assign(std::bit_ceil(required), EMPTY);
    for (std::uint32_t i = 0; i < records.size(); ++i) {
      auto slot = initial_slot(records[i].name);
      while (slots_[slot] != EMPTY) {
        if (records_[slots_[slot]].name == records[i].name)
          throw Error(path.string() + ": duplicate " + std::string(kind) + " name '" + records[i].name + "'");
        slot = (slot + 1) & (slots_.size() - 1);
      }
      slots_[slot] = i;
    }
  }

  [[nodiscard]] std::optional<std::uint32_t> find(std::string_view name) const {
    auto slot = initial_slot(name);
    while (slots_[slot] != EMPTY) {
      const auto index = slots_[slot];
      if (records_[index].name == name)
        return index;
      slot = (slot + 1) & (slots_.size() - 1);
    }
    return std::nullopt;
  }

private:
  static constexpr std::uint32_t EMPTY = std::numeric_limits<std::uint32_t>::max();

  [[nodiscard]] std::size_t initial_slot(std::string_view name) const { return std::hash<std::string_view>{}(name) & (slots_.size() - 1); }

  const std::vector<Record> &records_;
  std::vector<std::uint32_t> slots_;
};

using CellIndex = NameIndex<Cell>;
using NetIndex = NameIndex<Net>;

void parse_nets(const std::filesystem::path &path, Board &board, const CellIndex &cell_index) {
  Lines lines(path);
  require_header(lines, "nets");
  std::optional<std::uint64_t> declared_nets;
  std::optional<std::uint64_t> declared_pins;
  std::string line;
  std::vector<std::string_view> fields;

  while (lines.next(line)) {
    tokens(line, fields);
    if (fields[0] == "NumNets") {
      if (fields.size() < 3 || fields[1] != ":")
        lines.fail("malformed NumNets");

      declared_nets = number<std::uint64_t>(fields[2], lines, "net count");
      board.nets.reserve(static_cast<std::size_t>(*declared_nets));
      continue;
    }

    if (fields[0] == "NumPins") {
      if (fields.size() < 3 || fields[1] != ":")
        lines.fail("malformed NumPins");
      declared_pins = number<std::uint64_t>(fields[2], lines, "pin count");
      board.pins.reserve(static_cast<std::size_t>(*declared_pins));
      continue;
    }

    if (fields[0] != "NetDegree")
      lines.fail("expected NetDegree record");
    if (!declared_nets || !declared_pins)
      lines.fail("net declarations must precede records");
    if (fields.size() < 3 || fields[1] != ":")
      lines.fail("malformed NetDegree");

    const auto degree = number<std::uint64_t>(fields[2], lines, "net degree");
    Net net;
    net.name = fields.size() >= 4 ? std::string(fields[3]) : "net_" + std::to_string(board.nets.size());

    // Pins are appended directly to one shared vector so parsing remains
    // memory-efficient even for benchmarks with millions of pins.
    net.first_pin = board.pins.size();
    net.pin_count = degree;
    for (std::uint64_t i = 0; i < degree; ++i) {
      if (!lines.next(line))
        lines.fail("unexpected end inside net pins");
      tokens(line, fields);
      if (fields.size() < 2)
        lines.fail("pin record requires cell and direction");
      const auto cell = cell_index.find(fields[0]);
      if (!cell)
        lines.fail("pin references unknown cell '" + std::string(fields[0]) + "'");
      Pin pin;
      pin.cell = *cell;
      pin.direction = pin_direction(fields[1], lines);
      if (fields.size() >= 5 && fields[2] == ":") {
        pin.offset_x = number<double>(fields[3], lines, "pin X offset");
        pin.offset_y = number<double>(fields[4], lines, "pin Y offset");
      } else if (fields.size() > 2) {
        lines.fail("pin offsets require ': <x> <y>'");
      }
      board.pins.push_back(pin);
    }

    board.nets.push_back(std::move(net));
    if (board.nets.size() > *declared_nets || board.pins.size() > *declared_pins)
      lines.fail("more nets or pins than declared");
  }

  if (!declared_nets || !declared_pins)
    lines.fail("missing net declarations");
  if (board.nets.size() != *declared_nets)
    lines.fail("NumNets does not match records");
  if (board.pins.size() != *declared_pins)
    lines.fail("NumPins does not match records");
}

void parse_weights(const std::filesystem::path &path, Board &board, const CellIndex &cell_index, const NetIndex &net_index) {
  Lines lines(path);
  require_header(lines, "wts");
  std::optional<std::size_t> node_weight_count;
  std::optional<std::size_t> net_weight_count;
  std::unordered_set<std::string> seen;
  std::string line;
  std::vector<std::string_view> fields;

  while (lines.next(line)) {
    tokens(line, fields);
    if (fields.size() < 2)
      lines.fail("weight record requires at least one value");
    if (!seen.emplace(fields[0]).second)
      lines.fail("duplicate weight record for '" + std::string(fields[0]) + "'");

    std::vector<double> values;
    values.reserve(fields.size() - 1);
    for (std::size_t i = 1; i < fields.size(); ++i)
      values.push_back(number<double>(fields[i], lines, "weight"));

    // Bookshelf permits different vector dimensions for cells and nets, but
    // records of the same kind must agree so downstream consumers see a
    // consistent feature vector.
    if (const auto cell = cell_index.find(fields[0])) {
      if (node_weight_count && *node_weight_count != values.size())
        lines.fail("inconsistent node weight dimension");
      node_weight_count = values.size();
      board.cells[*cell].weights = std::move(values);
    } else if (const auto net = net_index.find(fields[0])) {
      if (net_weight_count && *net_weight_count != values.size())
        lines.fail("inconsistent net weight dimension");
      net_weight_count = values.size();
      board.nets[*net].weights = std::move(values);
    } else {
      lines.fail("weight references unknown cell or net '" + std::string(fields[0]) + "'");
    }
  }
}

void parse_rows(const std::filesystem::path &path, Board &board) {
  Lines lines(path);
  require_header(lines, "scl");
  std::string line;
  std::vector<std::string_view> fields;

  if (!lines.next(line))
    lines.fail("missing NumRows");

  tokens(line, fields);
  if (fields.size() < 3 || fields[0] != "NumRows" || fields[1] != ":")
    lines.fail("expected NumRows declaration");

  const auto declared = number<std::uint64_t>(fields[2], lines, "row count");
  board.rows.reserve(static_cast<std::size_t>(declared));

  while (lines.next(line)) {
    tokens(line, fields);
    if (fields.size() < 2 || fields[0] != "CoreRow" || lower(fields[1]) != "horizontal")
      lines.fail("expected 'CoreRow Horizontal'");

    Row row;
    bool coordinate = false;
    bool spacing = false;
    bool width = false;
    bool ended = false;

    while (lines.next(line)) {
      tokens(line, fields);

      if (fields[0] == "End") {
        ended = true;
        break;
      }

      if (fields.size() < 3 || fields[1] != ":")
        lines.fail("malformed row property");

      if (fields[0] == "Coordinate") {
        row.coordinate = number<double>(fields[2], lines, "row coordinate");
        coordinate = true;
      } else if (fields[0] == "Height") {
        row.height = number<double>(fields[2], lines, "row height");
      } else if (fields[0] == "Sitewidth") {
        row.site_width = number<double>(fields[2], lines, "site width");
        width = true;
      } else if (fields[0] == "Sitespacing") {
        row.site_spacing = number<double>(fields[2], lines, "site spacing");
        spacing = true;
      } else if (fields[0] == "Siteorient") {
        row.site_orientation = orientation(fields[2], lines);
      } else if (fields[0] == "Sitesymmetry") {
        // Store the independent Bookshelf symmetry capabilities as a compact
        // bit mask; see Row::symmetry in the format-neutral model.
        for (std::size_t i = 2; i < fields.size(); ++i) {
          const auto value = lower(fields[i]);
          if (value == "x")
            row.symmetry |= 1;
          else if (value == "y")
            row.symmetry |= 2;
          else if (value == "rot90" || value == "r90")
            row.symmetry |= 4;
          else if (value != "1" && value != "none")
            lines.fail("unknown site symmetry '" + std::string(fields[i]) + "'");
        }
      } else if (fields[0] == "SubrowOrigin") {
        if (fields.size() < 6 || fields[3] != "NumSites" || fields[4] != ":")
          lines.fail("SubrowOrigin requires 'NumSites : <count>'");
        row.subrows.push_back({number<double>(fields[2], lines, "subrow origin"), number<std::uint64_t>(fields[5], lines, "site count")});
      } else {
        lines.fail("unknown row property '" + std::string(fields[0]) + "'");
      }
    }

    if (!ended)
      lines.fail("unterminated CoreRow");
    if (!coordinate || !spacing || row.subrows.empty())
      lines.fail("row requires Coordinate, Sitespacing, and at least one subrow");
    if (!width)
      row.site_width = row.site_spacing;
    if (row.height < 0 || row.site_width <= 0 || row.site_spacing <= 0)
      lines.fail("invalid row dimensions");

    board.rows.push_back(std::move(row));
    if (board.rows.size() > declared)
      lines.fail("more rows than declared");
  }

  if (board.rows.size() != declared)
    lines.fail("NumRows does not match row records");
}

[[nodiscard]] std::optional<std::pair<double, double>> parse_dims(std::string_view field, const Lines &lines) {
  const auto equal = field.find('=');
  if (equal == std::string_view::npos || lower(field.substr(0, equal)) != "dims")
    return std::nullopt;

  auto value = field.substr(equal + 1);
  if (!value.empty() && value.front() == '(')
    value.remove_prefix(1);
  if (!value.empty() && value.back() == ')')
    value.remove_suffix(1);

  const auto comma = value.find(',');
  if (comma == std::string_view::npos)
    lines.fail("DIMS requires DIMS=(width,height)");

  return std::pair{number<double>(value.substr(0, comma), lines, "DIMS width"), number<double>(value.substr(comma + 1), lines, "DIMS height")};
}

void parse_placement(const std::filesystem::path &path, Board &board, const CellIndex &cell_index) {
  Lines lines(path);
  require_header(lines, "pl");
  std::string line;
  std::vector<std::string_view> fields;

  while (lines.next(line)) {
    tokens(line, fields);
    if (fields.size() < 3)
      lines.fail("placement requires cell, X, and Y");

    const auto cell = cell_index.find(fields[0]);
    if (!cell)
      lines.fail("placement references unknown cell '" + std::string(fields[0]) + "'");
    if (board.cells[*cell].location)
      lines.fail("duplicate placement for '" + std::string(fields[0]) + "'");

    Location location;
    location.x = number<double>(fields[1], lines, "placement X");
    location.y = number<double>(fields[2], lines, "placement Y");

    // Optional fields appear in several equivalent Bookshelf spellings. Skip
    // standalone separators, then interpret status, DIMS, or orientation.
    for (std::size_t i = 3; i < fields.size(); ++i) {
      auto field = fields[i];
      if (field == ":" || field == "/")
        continue;
      if (field.starts_with('/'))
        field.remove_prefix(1);
      const auto normalized = lower(field);
      if (normalized == "fixed")
        location.status = PlacementStatus::Fixed;
      else if (normalized == "fixed_ni")
        location.status = PlacementStatus::FixedNonInteracting;
      else if (const auto dimensions = parse_dims(field, lines)) {
        location.width = dimensions->first;
        location.height = dimensions->second;
      } else {
        location.orientation = orientation(field, lines);
      }
    }

    if ((location.width && *location.width < 0) || (location.height && *location.height < 0))
      lines.fail("placement dimensions cannot be negative");

    board.cells[*cell].location = location;
  }
}

class BookshelfParser final : public Parser {
public:
  explicit BookshelfParser(ParseOptions options) : options_(std::move(options)) {}

  [[nodiscard]] Board parse(const std::filesystem::path &input) const override {
    auto components = parse_aux(input);
    if (options_.placement_override)
      components.placement = *options_.placement_override;

    Board board;
    board.name = input.stem().stem().string();

    // Parse in dependency order. Names are resolved once into compact numeric
    // indices; the core Board therefore remains independent of Bookshelf.
    parse_nodes(components.nodes, board);
    const CellIndex cell_index(board.cells, components.nodes, "cell");

    parse_nets(components.nets, board, cell_index);
    {
      // The net index is only needed for the optional weight file. Limit its
      // lifetime because large designs can contain millions of names.
      const NetIndex net_index(board.nets, components.nets, "net");
      if (components.weights)
        parse_weights(*components.weights, board, cell_index, net_index);
    }

    parse_rows(components.rows, board);
    parse_placement(components.placement, board, cell_index);
    return board;
  }

private:
  ParseOptions options_;
};

} // namespace

std::unique_ptr<Parser> make_parser(std::string_view format, ParseOptions options) {
  if (lower(format) == "bookshelf")
    return std::make_unique<BookshelfParser>(std::move(options));
  throw Error("unsupported input format '" + std::string(format) + "'");
}

} // namespace placement
