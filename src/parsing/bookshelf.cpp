#include "placement/parsing/parser.hpp"

#include "common.hpp"

#include "placement/error.hpp"

#include <fstream>
#include <limits>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_set>

namespace placement {
namespace {

using parsing_detail::ascii_iequal;
using parsing_detail::lower;
using parsing_detail::NameIndex;
using parsing_detail::number;
using parsing_detail::orientation;
using parsing_detail::pin_direction;

class Record {
public:
  [[nodiscard]] const std::vector<std::string_view> &fields() {
    // Views avoid copying every field. They remain valid until the next call
    // to Records::next() reuses the line buffer. Reusing the vector also
    // avoids an allocation for every record in multi-million-line files.
    fields_.clear();
    std::size_t position = 0;
    while (position < line_.size()) {
      while (position < line_.size() && static_cast<unsigned char>(line_[position]) <= static_cast<unsigned char>(' '))
        ++position;
      const auto begin = position;
      while (position < line_.size() && static_cast<unsigned char>(line_[position]) > static_cast<unsigned char>(' '))
        ++position;
      if (begin != position)
        fields_.emplace_back(line_.data() + begin, position - begin);
    }
    return fields_;
  }

private:
  friend class Records;

  std::string line_;
  std::vector<std::string_view> fields_;
};

class Records {
public:
  explicit Records(const std::filesystem::path &path) : path_(path), input_(path) {
    if (!input_)
      throw Error("cannot open " + path.string());
  }

  bool next(Record &record) {
    while (std::getline(input_, record.line_)) {
      ++line_number_;

      const auto comment = record.line_.find('#');
      if (comment != std::string::npos)
        record.line_.erase(comment);

      if (record.line_.find_first_not_of(" \t\r\n") != std::string::npos)
        return true;
    }

    if (input_.bad())
      throw Error("failed while reading " + path_.string());
    return false;
  }

  [[noreturn]] void fail(std::string_view message) const {
    throw Error(path_.string() + ':' + std::to_string(line_number_) + ": " + std::string(message));
  }

private:
  std::filesystem::path path_;
  std::ifstream input_;
  std::uint64_t line_number_{};
};

void require_header(Records &records, std::string_view expected) {
  Record record;
  if (!records.next(record))
    records.fail("empty component file");

  const auto &fields = record.fields();
  if (fields.size() < 3 || lower(fields[0]) != "ucla" || lower(fields[1]) != expected)
    records.fail("expected 'UCLA " + std::string(expected) + " <version>' header");
}

struct Components {
  std::filesystem::path nodes;
  std::filesystem::path nets;
  std::optional<std::filesystem::path> weights;
  std::filesystem::path placement;
  std::filesystem::path rows;
};

[[nodiscard]] Components parse_aux(const std::filesystem::path &path) {
  Records records(path);
  Record record;

  if (!records.next(record))
    records.fail("empty AUX manifest");

  const auto &fields = record.fields();
  if (fields.size() < 3 || fields[1] != ":")
    records.fail("expected '<format> : <component files>'");

  const auto format = lower(fields[0]);
  if (format != "rowbasedplacement" && format != "stdcellplacement")
    records.fail("unsupported AUX format '" + std::string(fields[0]) + "'");

  Components components;
  std::unordered_set<std::string> suffixes;
  for (std::size_t i = 2; i < fields.size(); ++i) {
    const std::filesystem::path component(fields[i]);

    const auto suffix = lower(component.extension().string());
    if (!suffixes.insert(suffix).second)
      records.fail("duplicate component suffix '" + suffix + "'");

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
      records.fail("unsupported component suffix '" + suffix + "'");
  }

  if (components.nodes.empty() || components.nets.empty() || components.placement.empty() || components.rows.empty())
    records.fail("manifest requires .nodes, .nets, .pl, and .scl components");

  return components;
}

void parse_nodes(const std::filesystem::path &path, Board &board) {
  Records records(path);
  require_header(records, "nodes");
  Record record;
  std::optional<std::uint64_t> declared_nodes;
  std::optional<std::uint64_t> declared_terminals;
  std::uint64_t terminal_count = 0;

  while (records.next(record)) {
    const auto &fields = record.fields();
    if (fields[0] == "NumNodes") {
      if (fields.size() < 3 || fields[1] != ":")
        records.fail("malformed NumNodes");

      declared_nodes = number<std::uint64_t>(fields[2], records, "node count");
      if (*declared_nodes > std::numeric_limits<std::uint32_t>::max())
        records.fail("node count exceeds placement model limit");

      board.cells.reserve(static_cast<std::size_t>(*declared_nodes));
      continue;
    }

    if (fields[0] == "NumTerminals") {
      if (fields.size() < 3 || fields[1] != ":")
        records.fail("malformed NumTerminals");

      declared_terminals = number<std::uint64_t>(fields[2], records, "terminal count");
      continue;
    }

    if (!declared_nodes)
      records.fail("NumNodes must precede node records");
    if (fields.size() < 3)
      records.fail("node record requires name, width, and height");

    Cell cell;
    cell.name = fields[0];
    cell.width = number<double>(fields[1], records, "cell width");
    cell.height = number<double>(fields[2], records, "cell height");

    if (cell.width < 0 || cell.height < 0)
      records.fail("cell dimensions cannot be negative");

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
        records.fail("unknown node kind '" + std::string(fields[3]) + "'");
      ++terminal_count;
    }

    board.cells.push_back(std::move(cell));
    if (board.cells.size() > *declared_nodes)
      records.fail("more nodes than declared");
  }

  if (!declared_nodes || !declared_terminals)
    records.fail("missing node declarations");
  if (board.cells.size() != *declared_nodes)
    records.fail("NumNodes does not match parsed node records");
  if (terminal_count != *declared_terminals)
    records.fail("NumTerminals does not match parsed terminal records");
}

using CellIndex = NameIndex<Cell>;
using NetIndex = NameIndex<Net>;

void parse_nets(const std::filesystem::path &path, Board &board, const CellIndex &cell_index) {
  Records records(path);
  require_header(records, "nets");
  Record record;
  std::optional<std::uint64_t> declared_nets;
  std::optional<std::uint64_t> declared_pins;

  while (records.next(record)) {
    const auto &fields = record.fields();
    if (fields[0] == "NumNets") {
      if (fields.size() < 3 || fields[1] != ":")
        records.fail("malformed NumNets");

      declared_nets = number<std::uint64_t>(fields[2], records, "net count");
      board.nets.reserve(static_cast<std::size_t>(*declared_nets));
      continue;
    }

    if (fields[0] == "NumPins") {
      if (fields.size() < 3 || fields[1] != ":")
        records.fail("malformed NumPins");

      declared_pins = number<std::uint64_t>(fields[2], records, "pin count");
      board.pins.reserve(static_cast<std::size_t>(*declared_pins));
      continue;
    }

    if (fields[0] != "NetDegree")
      records.fail("expected NetDegree record");
    if (!declared_nets || !declared_pins)
      records.fail("net declarations must precede records");
    if (fields.size() < 3 || fields[1] != ":")
      records.fail("malformed NetDegree");

    const auto degree = number<std::uint64_t>(fields[2], records, "net degree");
    Net net;
    net.name = fields.size() >= 4 ? std::string(fields[3]) : "net_" + std::to_string(board.nets.size());

    // Pins are appended directly to one shared vector so parsing remains
    // memory-efficient even for benchmarks with millions of pins.
    net.first_pin = board.pins.size();
    net.pin_count = degree;
    for (std::uint64_t i = 0; i < degree; ++i) {
      if (!records.next(record))
        records.fail("unexpected end inside net pins");

      const auto &pin_fields = record.fields();
      if (pin_fields.size() < 2)
        records.fail("pin record requires cell and direction");

      const auto cell = cell_index.find(pin_fields[0]);
      if (!cell)
        records.fail("pin references unknown cell '" + std::string(pin_fields[0]) + "'");

      Pin pin;
      pin.cell = *cell;
      pin.direction = pin_direction(pin_fields[1], records);
      if (pin_fields.size() >= 5 && pin_fields[2] == ":") {
        pin.offset_x = number<double>(pin_fields[3], records, "pin X offset");
        pin.offset_y = number<double>(pin_fields[4], records, "pin Y offset");
      } else if (pin_fields.size() > 2) {
        records.fail("pin offsets require ': <x> <y>'");
      }

      board.pins.push_back(pin);
    }

    board.nets.push_back(std::move(net));
    if (board.nets.size() > *declared_nets || board.pins.size() > *declared_pins)
      records.fail("more nets or pins than declared");
  }

  if (!declared_nets || !declared_pins)
    records.fail("missing net declarations");
  if (board.nets.size() != *declared_nets)
    records.fail("NumNets does not match records");
  if (board.pins.size() != *declared_pins)
    records.fail("NumPins does not match records");
}

void parse_weights(const std::filesystem::path &path, Board &board, const CellIndex &cell_index) {
  Records records(path);
  require_header(records, "wts");
  Record record;
  std::optional<std::size_t> node_weight_count;
  std::optional<std::size_t> net_weight_count;
  std::unordered_set<std::string> seen;
  std::optional<NetIndex> net_index;

  while (records.next(record)) {
    const auto &fields = record.fields();
    if (fields.size() < 2)
      records.fail("weight record requires at least one value");
    if (!seen.emplace(fields[0]).second)
      records.fail("duplicate weight record for '" + std::string(fields[0]) + "'");

    std::vector<double> values;
    values.reserve(fields.size() - 1);
    for (std::size_t i = 1; i < fields.size(); ++i)
      values.push_back(number<double>(fields[i], records, "weight"));

    // Bookshelf permits different vector dimensions for cells and nets, but
    // records of the same kind must agree so downstream consumers see a
    // consistent feature vector.
    if (const auto cell = cell_index.find(fields[0])) {
      if (node_weight_count && *node_weight_count != values.size())
        records.fail("inconsistent node weight dimension");
      node_weight_count = values.size();
      board.cells[*cell].weights = std::move(values);
    } else {
      // Most benchmark weight files contain only the header. Defer the
      // multi-million-net lookup table until a record actually needs it.
      if (!net_index)
        net_index.emplace(board.nets, path);
      const auto net = net_index->find(fields[0]);
      if (!net)
        records.fail("weight references unknown cell or net '" + std::string(fields[0]) + "'");
      if (net_weight_count && *net_weight_count != values.size())
        records.fail("inconsistent net weight dimension");
      net_weight_count = values.size();
      board.nets[*net].weights = std::move(values);
    }
  }
}

void parse_rows(const std::filesystem::path &path, Board &board) {
  Records records(path);
  require_header(records, "scl");
  Record record;

  if (!records.next(record))
    records.fail("missing NumRows");

  const auto &declaration = record.fields();
  if (declaration.size() < 3 || declaration[0] != "NumRows" || declaration[1] != ":")
    records.fail("expected NumRows declaration");

  const auto declared = number<std::uint64_t>(declaration[2], records, "row count");
  board.rows.reserve(static_cast<std::size_t>(declared));

  while (records.next(record)) {
    const auto &fields = record.fields();
    if (fields.size() < 2 || fields[0] != "CoreRow" || lower(fields[1]) != "horizontal")
      records.fail("expected 'CoreRow Horizontal'");

    Row row;
    bool coordinate = false;
    bool spacing = false;
    bool width = false;
    bool ended = false;

    while (records.next(record)) {
      const auto &property_fields = record.fields();

      if (property_fields[0] == "End") {
        ended = true;
        break;
      }

      if (property_fields.size() < 3 || property_fields[1] != ":")
        records.fail("malformed row property");

      if (property_fields[0] == "Coordinate") {
        row.coordinate = number<double>(property_fields[2], records, "row coordinate");
        coordinate = true;
      } else if (property_fields[0] == "Height") {
        row.height = number<double>(property_fields[2], records, "row height");
      } else if (property_fields[0] == "Sitewidth") {
        row.site_width = number<double>(property_fields[2], records, "site width");
        width = true;
      } else if (property_fields[0] == "Sitespacing") {
        row.site_spacing = number<double>(property_fields[2], records, "site spacing");
        spacing = true;
      } else if (property_fields[0] == "Siteorient") {
        row.site_orientation = orientation(property_fields[2], records);
      } else if (property_fields[0] == "Sitesymmetry") {
        // Store the independent Bookshelf symmetry capabilities as a compact
        // bit mask; see Row::symmetry in the format-neutral model.
        for (std::size_t i = 2; i < property_fields.size(); ++i) {
          const auto value = lower(property_fields[i]);
          if (value == "x")
            row.symmetry |= 1;
          else if (value == "y")
            row.symmetry |= 2;
          else if (value == "rot90" || value == "r90")
            row.symmetry |= 4;
          else if (value != "1" && value != "none")
            records.fail("unknown site symmetry '" + std::string(property_fields[i]) + "'");
        }
      } else if (property_fields[0] == "SubrowOrigin") {
        if (property_fields.size() < 6 || property_fields[3] != "NumSites" || property_fields[4] != ":")
          records.fail("SubrowOrigin requires 'NumSites : <count>'");
        row.subrows.push_back(
            {number<double>(property_fields[2], records, "subrow origin"), number<std::uint64_t>(property_fields[5], records, "site count")});
      } else {
        records.fail("unknown row property '" + std::string(property_fields[0]) + "'");
      }
    }

    if (!ended)
      records.fail("unterminated CoreRow");
    if (!coordinate || !spacing || row.subrows.empty())
      records.fail("row requires Coordinate, Sitespacing, and at least one subrow");
    if (!width)
      row.site_width = row.site_spacing;
    if (row.height < 0 || row.site_width <= 0 || row.site_spacing <= 0)
      records.fail("invalid row dimensions");

    board.rows.push_back(std::move(row));
    if (board.rows.size() > declared)
      records.fail("more rows than declared");
  }

  if (board.rows.size() != declared)
    records.fail("NumRows does not match row records");
}

[[nodiscard]] std::optional<std::pair<double, double>> parse_dims(std::string_view field, const Records &records) {
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
    records.fail("DIMS requires DIMS=(width,height)");

  return std::pair{number<double>(value.substr(0, comma), records, "DIMS width"), number<double>(value.substr(comma + 1), records, "DIMS height")};
}

void parse_placement(const std::filesystem::path &path, Board &board, const CellIndex &cell_index) {
  Records records(path);
  require_header(records, "pl");
  Record record;

  while (records.next(record)) {
    const auto &fields = record.fields();
    if (fields.size() < 3)
      records.fail("placement requires cell, X, and Y");

    const auto cell = cell_index.find(fields[0]);
    if (!cell)
      records.fail("placement references unknown cell '" + std::string(fields[0]) + "'");
    if (board.cells[*cell].location)
      records.fail("duplicate placement for '" + std::string(fields[0]) + "'");

    Location location;
    location.x = number<double>(fields[1], records, "placement X");
    location.y = number<double>(fields[2], records, "placement Y");

    // Optional fields appear in several equivalent Bookshelf spellings. Skip
    // standalone separators, then interpret status, DIMS, or orientation.
    for (std::size_t i = 3; i < fields.size(); ++i) {
      auto field = fields[i];
      if (field == ":" || field == "/")
        continue;

      if (field.starts_with('/'))
        field.remove_prefix(1);

      if (ascii_iequal(field, "fixed"))
        location.status = PlacementStatus::Fixed;
      else if (ascii_iequal(field, "fixed_ni"))
        location.status = PlacementStatus::FixedNonInteracting;
      else if (const auto dimensions = parse_dims(field, records)) {
        location.width = dimensions->first;
        location.height = dimensions->second;
      } else {
        location.orientation = orientation(field, records);
      }
    }

    if ((location.width && *location.width < 0) || (location.height && *location.height < 0))
      records.fail("placement dimensions cannot be negative");

    board.cells[*cell].location = location;
  }
}

class BookshelfParser final : public Parser {
public:
  explicit BookshelfParser(BookshelfParseOptions options) : options_(std::move(options)) {}

  [[nodiscard]] Board parse(const std::filesystem::path &input) const override {
    auto components = parse_aux(input);
    if (options_.placement_override)
      components.placement = *options_.placement_override;

    Board board;
    board.name = input.stem().stem().string();

    // Parse in dependency order. Names are resolved once into compact numeric
    // indices; the core Board therefore remains independent of Bookshelf.
    parse_nodes(components.nodes, board);
    const CellIndex cell_index(board.cells, components.nodes);

    parse_nets(components.nets, board, cell_index);
    if (components.weights)
      parse_weights(*components.weights, board, cell_index);

    parse_rows(components.rows, board);
    parse_placement(components.placement, board, cell_index);

    return board;
  }

private:
  BookshelfParseOptions options_;
};

} // namespace

std::unique_ptr<Parser> make_parser(BookshelfParseOptions options) { return std::make_unique<BookshelfParser>(std::move(options)); }

} // namespace placement
