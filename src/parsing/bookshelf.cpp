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
    std::size_t pos = 0;
    while (pos < line_.size()) {
      while (pos < line_.size() && static_cast<unsigned char>(line_[pos]) <= static_cast<unsigned char>(' '))
        ++pos;
      const auto begin = pos;
      while (pos < line_.size() && static_cast<unsigned char>(line_[pos]) > static_cast<unsigned char>(' '))
        ++pos;
      if (begin != pos)
        fields_.emplace_back(line_.data() + begin, pos - begin);
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
  explicit Records(const std::filesystem::path &path) : path_(path), in_(path) {
    if (!in_)
      throw Error("cannot open " + path.string());
  }

  bool next(Record &record) {
    while (std::getline(in_, record.line_)) {
      ++line_;

      const auto comment = record.line_.find('#');
      if (comment != std::string::npos)
        record.line_.erase(comment);

      bool has_field = false;
      for (const char ch : record.line_) {
        if (static_cast<unsigned char>(ch) > static_cast<unsigned char>(' ')) {
          has_field = true;
          break;
        }
      }
      if (has_field)
        return true;
    }

    if (in_.bad())
      throw Error("failed while reading " + path_.string());
    return false;
  }

  [[noreturn]] void fail(std::string_view message) const { throw Error(path_.string() + ':' + std::to_string(line_) + ": " + std::string(message)); }

private:
  std::filesystem::path path_;
  std::ifstream in_;
  std::uint64_t line_{};
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

  Components parts;
  std::unordered_set<std::string> suffixes;
  // Component paths are relative to the AUX file, not to the process working
  // directory. Suffixes identify their roles in the Bookshelf family.
  for (std::size_t i = 2; i < fields.size(); ++i) {
    const std::filesystem::path component(fields[i]);

    const auto suffix = lower(component.extension().string());
    if (!suffixes.insert(suffix).second)
      records.fail("duplicate component suffix '" + suffix + "'");

    const auto resolved = path.parent_path() / component;
    if (suffix == ".nodes")
      parts.nodes = resolved;
    else if (suffix == ".nets")
      parts.nets = resolved;
    else if (suffix == ".wts")
      parts.weights = resolved;
    else if (suffix == ".pl")
      parts.placement = resolved;
    else if (suffix == ".scl")
      parts.rows = resolved;
    else
      records.fail("unsupported component suffix '" + suffix + "'");
  }

  if (parts.nodes.empty() || parts.nets.empty() || parts.placement.empty() || parts.rows.empty())
    records.fail("manifest requires .nodes, .nets, .pl, and .scl components");

  return parts;
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

void parse_nets(const std::filesystem::path &path, Board &board, const CellIndex &cell_idx) {
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

      const auto cell = cell_idx.find(pin_fields[0]);
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

void parse_weights(const std::filesystem::path &path, Board &board, const CellIndex &cell_idx) {
  Records records(path);
  require_header(records, "wts");
  Record record;
  std::optional<std::size_t> cell_weight_size;
  std::optional<std::size_t> net_weight_size;
  std::optional<NetIndex> net_idx;

  while (records.next(record)) {
    const auto &fields = record.fields();
    if (fields.size() < 2)
      records.fail("weight record requires at least one value");

    std::vector<double> *target;
    std::optional<std::size_t> *expected_size;
    std::string_view kind;
    if (const auto cell = cell_idx.find(fields[0])) {
      target = &board.cells[*cell].weights;
      expected_size = &cell_weight_size;
      kind = "node";
    } else {
      // Most benchmark weight files contain only the header. Defer the
      // multi-million-net lookup table until a record actually needs it.
      if (!net_idx)
        net_idx.emplace(board.nets, path);
      const auto net = net_idx->find(fields[0]);
      if (!net)
        records.fail("weight references unknown cell or net '" + std::string(fields[0]) + "'");
      target = &board.nets[*net].weights;
      expected_size = &net_weight_size;
      kind = "net";
    }
    if (!target->empty())
      records.fail("duplicate weight record for '" + std::string(fields[0]) + "'");

    std::vector<double> values;
    values.reserve(fields.size() - 1);
    for (std::size_t i = 1; i < fields.size(); ++i)
      values.push_back(number<double>(fields[i], records, "weight"));

    // Bookshelf permits different vector dimensions for cells and nets, but
    // records of the same kind must agree so downstream consumers see a
    // consistent feature vector.
    if (*expected_size && **expected_size != values.size())
      records.fail("inconsistent " + std::string(kind) + " weight dimension");
    *expected_size = values.size();
    *target = std::move(values);
  }
}

void parse_rows(const std::filesystem::path &path, Board &board) {
  Records records(path);
  require_header(records, "scl");
  Record record;

  if (!records.next(record))
    records.fail("missing NumRows");

  const auto &decl = record.fields();
  if (decl.size() < 3 || decl[0] != "NumRows" || decl[1] != ":")
    records.fail("expected NumRows declaration");

  const auto declared = number<std::uint64_t>(decl[2], records, "row count");
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
      const auto &props = record.fields();

      if (props[0] == "End") {
        ended = true;
        break;
      }

      if (props.size() < 3 || props[1] != ":")
        records.fail("malformed row property");

      if (props[0] == "Coordinate") {
        row.coordinate = number<double>(props[2], records, "row coordinate");
        coordinate = true;
      } else if (props[0] == "Height") {
        row.height = number<double>(props[2], records, "row height");
      } else if (props[0] == "Sitewidth") {
        row.site_width = number<double>(props[2], records, "site width");
        width = true;
      } else if (props[0] == "Sitespacing") {
        row.site_spacing = number<double>(props[2], records, "site spacing");
        spacing = true;
      } else if (props[0] == "Siteorient") {
        row.site_orientation = orientation(props[2], records);
      } else if (props[0] == "Sitesymmetry") {
        // Store the independent Bookshelf symmetry capabilities as a compact
        // bit mask; see Row::symmetry in the format-neutral model.
        for (std::size_t i = 2; i < props.size(); ++i) {
          const auto value = lower(props[i]);
          if (value == "x")
            row.symmetry |= 1;
          else if (value == "y")
            row.symmetry |= 2;
          else if (value == "rot90" || value == "r90")
            row.symmetry |= 4;
          else if (value != "1" && value != "none")
            records.fail("unknown site symmetry '" + std::string(props[i]) + "'");
        }
      } else if (props[0] == "SubrowOrigin") {
        if (props.size() < 6 || props[3] != "NumSites" || props[4] != ":")
          records.fail("SubrowOrigin requires 'NumSites : <count>'");
        row.subrows.push_back({number<double>(props[2], records, "subrow origin"), number<std::uint64_t>(props[5], records, "site count")});
      } else {
        records.fail("unknown row property '" + std::string(props[0]) + "'");
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

void parse_placement(const std::filesystem::path &path, Board &board, const CellIndex &cell_idx) {
  Records records(path);
  require_header(records, "pl");
  Record record;

  while (records.next(record)) {
    const auto &fields = record.fields();
    if (fields.size() < 3)
      records.fail("placement requires cell, X, and Y");

    const auto cell = cell_idx.find(fields[0]);
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
      else {
        // DIMS is the only key/value placement option; all other fields here
        // are orientation spellings.
        const auto equal = field.find('=');
        if (equal != std::string_view::npos && ascii_iequal(field.substr(0, equal), "dims")) {
          auto value = field.substr(equal + 1);
          if (!value.empty() && value.front() == '(')
            value.remove_prefix(1);
          if (!value.empty() && value.back() == ')')
            value.remove_suffix(1);

          const auto comma = value.find(',');
          if (comma == std::string_view::npos)
            records.fail("DIMS requires DIMS=(width,height)");

          location.width = number<double>(value.substr(0, comma), records, "DIMS width");
          location.height = number<double>(value.substr(comma + 1), records, "DIMS height");
          continue;
        }
        location.orientation = orientation(field, records);
      }
    }

    if ((location.width && *location.width < 0) || (location.height && *location.height < 0))
      records.fail("placement dimensions cannot be negative");

    // Store placements on the already-created cells so all later references
    // continue to use the stable indices built from the nodes file.
    board.cells[*cell].location = location;
  }
}

class BookshelfParser final : public Parser {
public:
  explicit BookshelfParser(BookshelfParseOptions opts) : opts_(std::move(opts)) {}

  [[nodiscard]] Board parse(const std::filesystem::path &in) const override {
    auto parts = parse_aux(in);
    if (opts_.placement_override)
      parts.placement = *opts_.placement_override;

    Board board;
    board.name = in.stem().stem().string();

    // Parse in dependency order. Names are resolved once into compact numeric
    // indices; the core Board therefore remains independent of Bookshelf.
    parse_nodes(parts.nodes, board);
    const CellIndex cell_idx(board.cells, parts.nodes);

    parse_nets(parts.nets, board, cell_idx);
    if (parts.weights)
      parse_weights(*parts.weights, board, cell_idx);

    parse_rows(parts.rows, board);
    parse_placement(parts.placement, board, cell_idx);

    return board;
  }

private:
  BookshelfParseOptions opts_;
};

} // namespace

std::unique_ptr<Parser> make_parser(BookshelfParseOptions opts) { return std::make_unique<BookshelfParser>(std::move(opts)); }

} // namespace placement
