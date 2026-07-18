#include "placement/parsing/parser.hpp"

#include "common.hpp"

#include "placement/error.hpp"

#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <cstdint>
#include <fstream>
#include <functional>
#include <limits>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace placement {
namespace {

using parsing_detail::NameIndex;
using parsing_detail::number;
using parsing_detail::orientation;
using parsing_detail::pin_direction;
using parsing_detail::validate_unique_names;

class Tokens {
public:
  explicit Tokens(const std::filesystem::path &path) : path_(path), in_(path, std::ios::binary) {
    if (!in_)
      throw Error("cannot open " + path.string());
  }

  bool next() {
    token_ = {};
    spill_.clear();

    while (true) {
      if (!ensure_available())
        return false;
      const auto ch = static_cast<unsigned char>(buf_[pos_]);
      if (kind(ch) == Comment) {
        while (true) {
          while (pos_ != avail_ && buf_[pos_] != '\n')
            ++pos_;
          if (pos_ != avail_)
            break;
          if (!refill())
            return false;
        }
        ++pos_;
        ++line_;
        continue;
      }
      if (kind(ch) == Space) {
        ++pos_;
        if (ch == '\n')
          ++line_;
        continue;
      }
      break;
    }

    token_line_ = line_;
    auto ch = static_cast<unsigned char>(buf_[pos_]);
    if (kind(ch) == Punctuation) {
      token_ = std::string_view(buf_.data() + pos_, 1);
      ++pos_;
      return true;
    }

    if (ch == '"') {
      ++pos_;
      bool escaped = false;
      while (true) {
        if (!ensure_available())
          fail("unterminated quoted string");
        ch = static_cast<unsigned char>(buf_[pos_++]);
        if (ch == '\n')
          ++line_;
        if (ch == '"' && !escaped) {
          token_ = spill_;
          return true;
        }
        spill_.push_back(static_cast<char>(ch));
        escaped = ch == '\\' && !escaped;
        if (ch != '\\')
          escaped = false;
      }
    }

    while (true) {
      const auto begin = pos_;
      while (pos_ != avail_) {
        ch = static_cast<unsigned char>(buf_[pos_]);
        if (kind(ch) != Regular) {
          if (spill_.empty())
            token_ = std::string_view(buf_.data() + begin, pos_ - begin);
          else {
            spill_.append(buf_.data() + begin, pos_ - begin);
            token_ = spill_;
          }
          return true;
        }
        ++pos_;
      }

      spill_.append(buf_.data() + begin, pos_ - begin);
      if (!refill()) {
        token_ = spill_;
        return true;
      }
    }
  }

  void require(std::string_view description) {
    if (!next())
      fail("unexpected end while reading " + std::string(description));
  }

  void expect(std::string_view expected) {
    require("'" + std::string(expected) + "'");
    if (token_ != expected)
      fail("expected '" + std::string(expected) + "', found '" + std::string(token_) + "'");
  }

  [[nodiscard]] std::string_view token() const { return token_; }

  [[noreturn]] void fail(std::string_view message) const {
    throw Error(path_.string() + ':' + std::to_string(token_line_) + ": " + std::string(message));
  }

private:
  enum CharacterKind : std::uint8_t { Regular, Space, Punctuation, Comment };

  static constexpr auto CHARACTER_KINDS = [] {
    std::array<CharacterKind, 256> res{};
    res[static_cast<unsigned char>(' ')] = Space;
    res[static_cast<unsigned char>('\t')] = Space;
    res[static_cast<unsigned char>('\r')] = Space;
    res[static_cast<unsigned char>('\n')] = Space;
    res[static_cast<unsigned char>('\f')] = Space;
    res[static_cast<unsigned char>('(')] = Punctuation;
    res[static_cast<unsigned char>(')')] = Punctuation;
    res[static_cast<unsigned char>(';')] = Punctuation;
    res[static_cast<unsigned char>('#')] = Comment;
    return res;
  }();

  [[nodiscard]] static CharacterKind kind(unsigned char ch) { return CHARACTER_KINDS[ch]; }

  bool ensure_available() { return pos_ != avail_ || refill(); }

  bool refill() {
    in_.read(buf_.data(), static_cast<std::streamsize>(buf_.size()));
    avail_ = static_cast<std::size_t>(in_.gcount());
    pos_ = 0;
    if (avail_ == 0 && !in_.eof())
      throw Error("failed while reading " + path_.string());
    return avail_ != 0;
  }

  std::filesystem::path path_;
  std::ifstream in_;
  std::array<char, 256 * 1024> buf_{};
  std::size_t pos_{};
  std::size_t avail_{};
  std::uint64_t line_{1};
  std::uint64_t token_line_{1};
  std::string_view token_;
  std::string spill_;
};

template <typename T> [[nodiscard]] T next_number(Tokens &tokens, std::string_view description) {
  tokens.require(description);
  return number<T>(tokens.token(), tokens, description);
}

void skip_statement(Tokens &tokens) {
  while (tokens.next())
    if (tokens.token() == ";")
      return;
  tokens.fail("unterminated statement");
}

// LEF definitions such as LAYER and VIA end with the definition name rather
// than the opening keyword. The opening keyword is the current token.
void skip_lef_definition(Tokens &tokens) {
  tokens.require("LEF block name");
  const std::string name(tokens.token());

  while (tokens.next()) {
    if (tokens.token() != "END")
      continue;
    tokens.require("block end name");
    if (tokens.token() == name)
      return;
  }
  tokens.fail("unterminated " + std::string(name) + " block");
}

// Blocks such as UNITS and PROPERTYDEFINITIONS repeat the opening keyword after
// END. The keyword is the current token, so callers cannot pass a mismatched
// arbitrary string.
void skip_lef_keyword_block(Tokens &tokens) {
  const std::string name(tokens.token());
  while (tokens.next()) {
    if (tokens.token() != "END")
      continue;
    tokens.expect(name);
    return;
  }
  tokens.fail("unterminated " + name + " block");
}

void skip_lef_obstruction(Tokens &tokens) {
  while (tokens.next())
    if (tokens.token() == "END")
      return;
  tokens.fail("unterminated macro obstruction");
}

enum class CountRequirement { Exact, UpperBound };

class CountedSection {
public:
  explicit CountedSection(Tokens &tokens, CountRequirement requirement = CountRequirement::Exact)
      : tokens_(tokens), name_(tokens.token()), requirement_(requirement), declared_(next_number<std::uint64_t>(tokens, name_ + " count")) {
    tokens_.expect(";");
  }

  [[nodiscard]] std::uint64_t declared() const { return declared_; }

  // Advances to the next record and consumes its leading '-'. A false result
  // means END <section> was consumed and the declared count was validated.
  bool next() {
    if (!tokens_.next())
      tokens_.fail("unterminated " + name_ + " section");

    if (tokens_.token() == "END") {
      tokens_.expect(name_);
      if (requirement_ == CountRequirement::Exact && parsed_ != declared_)
        tokens_.fail(name_ + " count does not match records");
      return false;
    }

    if (tokens_.token() != "-")
      tokens_.fail("expected " + name_ + " record");
    if (++parsed_ > declared_)
      tokens_.fail("more " + name_ + " records than declared");
    return true;
  }

private:
  Tokens &tokens_;
  std::string name_;
  CountRequirement requirement_;
  std::uint64_t declared_{};
  std::uint64_t parsed_{};
};

void skip_counted_section(Tokens &tokens, CountRequirement requirement = CountRequirement::Exact) {
  CountedSection section(tokens, requirement);
  while (section.next())
    skip_statement(tokens);
}

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

  [[nodiscard]] bool empty() const { return !std::isfinite(min_x); }
};

struct SiteDefinition {
  std::string name;
  double width{};
  double height{};
  std::uint8_t symmetry{};
};

struct MacroPinDefinition {
  std::string name;
  PinDirection direction{PinDirection::Unknown};
  double x{};
  double y{};
};

struct MacroDefinition {
  std::string name;
  double width{};
  double height{};
  bool macro{};
  std::vector<MacroPinDefinition> pins;
  std::vector<std::uint32_t> pin_slots;

  [[nodiscard]] const MacroPinDefinition *find_pin(std::string_view pin_name) const {
    const auto empty = std::numeric_limits<std::uint32_t>::max();
    auto slot = std::hash<std::string_view>{}(pin_name) & (pin_slots.size() - 1);
    while (pin_slots[slot] != empty) {
      const auto idx = pin_slots[slot];
      if (pins[idx].name == pin_name)
        return &pins[idx];
      slot = (slot + 1) & (pin_slots.size() - 1);
    }
    return nullptr;
  }
};

template <typename Definition> struct DefinitionTraits;

template <> struct DefinitionTraits<SiteDefinition> {
  static constexpr std::string_view kind = "site";
};

template <> struct DefinitionTraits<MacroDefinition> {
  static constexpr std::string_view kind = "macro";
};

template <typename Definition> void finalize_definitions(std::vector<Definition> &defs) {
  std::sort(defs.begin(), defs.end(), [](const Definition &lhs, const Definition &rhs) { return lhs.name < rhs.name; });
  for (std::size_t i = 1; i < defs.size(); ++i)
    if (defs[i - 1].name == defs[i].name)
      throw Error("LEF inputs contain duplicate " + std::string(DefinitionTraits<Definition>::kind) + " name '" + defs[i].name + "'");
}

template <typename Definition> [[nodiscard]] std::vector<std::uint32_t> make_definition_index(const std::vector<Definition> &defs) {
  constexpr auto empty = std::numeric_limits<std::uint32_t>::max();
  const auto required = defs.size() + defs.size() / 2 + 1;
  std::vector<std::uint32_t> slots(std::bit_ceil(required), empty);
  for (std::uint32_t i = 0; i < defs.size(); ++i) {
    auto slot = std::hash<std::string_view>{}(defs[i].name) & (slots.size() - 1);
    while (slots[slot] != empty)
      slot = (slot + 1) & (slots.size() - 1);
    slots[slot] = i;
  }
  return slots;
}

template <typename Definition>
[[nodiscard]] std::optional<std::uint32_t> find_definition(const std::vector<Definition> &defs, const std::vector<std::uint32_t> &slots,
                                                           std::string_view name) {
  constexpr auto empty = std::numeric_limits<std::uint32_t>::max();
  auto slot = std::hash<std::string_view>{}(name) & (slots.size() - 1);
  while (slots[slot] != empty) {
    const auto idx = slots[slot];
    if (defs[idx].name == name)
      return idx;
    slot = (slot + 1) & (slots.size() - 1);
  }
  return std::nullopt;
}

class Library {
public:
  void add(SiteDefinition site) { sites_.push_back(std::move(site)); }
  void add(MacroDefinition macro) { macros_.push_back(std::move(macro)); }

  void finalize() {
    if (sites_.empty())
      throw Error("LEF inputs contain no placement sites");
    if (macros_.empty())
      throw Error("LEF inputs contain no macros");
    if (sites_.size() >= std::numeric_limits<std::uint32_t>::max() || macros_.size() >= std::numeric_limits<std::uint32_t>::max())
      throw Error("LEF definition count exceeds placement model limit");

    // Sort once for deterministic duplicate checks, then use compact open
    // addressing for the millions of DEF references into these definitions.
    finalize_definitions(sites_);
    finalize_definitions(macros_);
    site_slots_ = make_definition_index(sites_);
    macro_slots_ = make_definition_index(macros_);
  }

  [[nodiscard]] std::optional<std::uint32_t> find_site(std::string_view name) const { return find_definition(sites_, site_slots_, name); }
  [[nodiscard]] std::optional<std::uint32_t> find_macro(std::string_view name) const { return find_definition(macros_, macro_slots_, name); }
  [[nodiscard]] std::size_t macro_count() const { return macros_.size(); }
  [[nodiscard]] const SiteDefinition &site(std::uint32_t idx) const { return sites_[idx]; }
  [[nodiscard]] const MacroDefinition &macro(std::uint32_t idx) const { return macros_[idx]; }

private:
  std::vector<SiteDefinition> sites_;
  std::vector<MacroDefinition> macros_;
  std::vector<std::uint32_t> site_slots_;
  std::vector<std::uint32_t> macro_slots_;
};

void parse_lef_rectangle(Tokens &tokens, Bounds &bounds) {
  const auto x0 = next_number<double>(tokens, "rectangle X coordinate");
  const auto y0 = next_number<double>(tokens, "rectangle Y coordinate");
  const auto x1 = next_number<double>(tokens, "rectangle X coordinate");
  const auto y1 = next_number<double>(tokens, "rectangle Y coordinate");
  bounds.include(x0, y0, x1, y1);
  skip_statement(tokens);
}

void parse_lef_port(Tokens &tokens, Bounds &bounds) {
  while (tokens.next()) {
    if (tokens.token() == "END")
      return;
    if (tokens.token() == "RECT")
      parse_lef_rectangle(tokens, bounds);
    else if (tokens.token() != ";")
      skip_statement(tokens);
  }
  tokens.fail("unterminated LEF port");
}

[[nodiscard]] MacroPinDefinition parse_lef_pin(Tokens &tokens) {
  tokens.require("macro pin name");
  MacroPinDefinition pin;
  pin.name = tokens.token();
  Bounds geometry;
  bool direction_seen = false;

  while (tokens.next()) {
    if (tokens.token() == "END") {
      tokens.require("macro pin end name");
      if (tokens.token() != pin.name)
        tokens.fail("expected END " + pin.name);
      if (!direction_seen)
        tokens.fail("macro pin '" + pin.name + "' has no direction");
      if (geometry.empty())
        tokens.fail("macro pin '" + pin.name + "' has no rectangular geometry");
      pin.x = (geometry.min_x + geometry.max_x) / 2.0;
      pin.y = (geometry.min_y + geometry.max_y) / 2.0;
      return pin;
    }

    if (tokens.token() == "DIRECTION") {
      tokens.require("macro pin direction");
      pin.direction = pin_direction(tokens.token(), tokens);
      direction_seen = true;
      skip_statement(tokens);
    } else if (tokens.token() == "PORT") {
      parse_lef_port(tokens, geometry);
    } else if (tokens.token() != ";") {
      skip_statement(tokens);
    }
  }
  tokens.fail("unterminated macro pin '" + pin.name + "'");
}

[[nodiscard]] SiteDefinition parse_lef_site(Tokens &tokens) {
  tokens.require("site name");
  SiteDefinition site;
  site.name = tokens.token();
  bool size_seen = false;

  while (tokens.next()) {
    if (tokens.token() == "END") {
      tokens.require("site end name");
      if (tokens.token() != site.name)
        tokens.fail("expected END " + site.name);
      if (!size_seen || site.width <= 0 || site.height <= 0)
        tokens.fail("site '" + site.name + "' requires positive dimensions");
      return site;
    }

    if (tokens.token() == "SIZE") {
      site.width = next_number<double>(tokens, "site width");
      tokens.expect("BY");
      site.height = next_number<double>(tokens, "site height");
      tokens.expect(";");
      size_seen = true;
    } else if (tokens.token() == "SYMMETRY") {
      while (tokens.next() && tokens.token() != ";")
        if (tokens.token() == "X")
          site.symmetry |= 1;
        else if (tokens.token() == "Y")
          site.symmetry |= 2;
        else if (tokens.token() == "R90")
          site.symmetry |= 4;
        else
          tokens.fail("unknown site symmetry '" + std::string(tokens.token()) + "'");
    } else if (tokens.token() != ";") {
      skip_statement(tokens);
    }
  }
  tokens.fail("unterminated site '" + site.name + "'");
}

[[nodiscard]] MacroDefinition parse_lef_macro(Tokens &tokens) {
  tokens.require("macro name");
  MacroDefinition macro;
  macro.name = tokens.token();
  bool size_seen = false;
  double origin_x = 0;
  double origin_y = 0;

  while (tokens.next()) {
    if (tokens.token() == "END") {
      tokens.require("macro end name");
      if (tokens.token() != macro.name)
        tokens.fail("expected END " + macro.name);
      if (!size_seen || macro.width <= 0 || macro.height <= 0)
        tokens.fail("macro '" + macro.name + "' requires positive dimensions");
      if (origin_x != 0 || origin_y != 0)
        tokens.fail("macro '" + macro.name + "' has an unsupported nonzero origin");
      std::sort(macro.pins.begin(), macro.pins.end(),
                [](const MacroPinDefinition &lhs, const MacroPinDefinition &rhs) { return lhs.name < rhs.name; });
      for (std::size_t i = 1; i < macro.pins.size(); ++i)
        if (macro.pins[i - 1].name == macro.pins[i].name)
          tokens.fail("duplicate macro pin name '" + macro.pins[i].name + "'");
      if (macro.pins.size() >= std::numeric_limits<std::uint32_t>::max())
        tokens.fail("macro pin count exceeds placement model limit");
      macro.pin_slots = make_definition_index(macro.pins);
      return macro;
    }

    if (tokens.token() == "CLASS") {
      tokens.require("macro class");
      macro.macro = tokens.token() != "CORE";
      skip_statement(tokens);
    } else if (tokens.token() == "ORIGIN") {
      origin_x = next_number<double>(tokens, "macro origin X");
      origin_y = next_number<double>(tokens, "macro origin Y");
      tokens.expect(";");
    } else if (tokens.token() == "SIZE") {
      macro.width = next_number<double>(tokens, "macro width");
      tokens.expect("BY");
      macro.height = next_number<double>(tokens, "macro height");
      tokens.expect(";");
      size_seen = true;
    } else if (tokens.token() == "PIN") {
      macro.pins.push_back(parse_lef_pin(tokens));
    } else if (tokens.token() == "OBS") {
      skip_lef_obstruction(tokens);
    } else if (tokens.token() != ";") {
      skip_statement(tokens);
    }
  }
  tokens.fail("unterminated macro '" + macro.name + "'");
}

void parse_lef(const std::filesystem::path &path, Library &library) {
  Tokens tokens(path);
  while (tokens.next()) {
    if (tokens.token() == "SITE") {
      library.add(parse_lef_site(tokens));
    } else if (tokens.token() == "MACRO") {
      library.add(parse_lef_macro(tokens));
    } else if (tokens.token() == "LAYER" || tokens.token() == "VIA" || tokens.token() == "VIARULE" || tokens.token() == "NONDEFAULTRULE") {
      skip_lef_definition(tokens);
    } else if (tokens.token() == "PROPERTYDEFINITIONS" || tokens.token() == "UNITS") {
      skip_lef_keyword_block(tokens);
    } else if (tokens.token() == "END") {
      tokens.require("LEF end name");
      if (tokens.token() == "LIBRARY")
        return;
      tokens.fail("unexpected LEF END " + std::string(tokens.token()));
    } else if (tokens.token() != ";") {
      skip_statement(tokens);
    }
  }
}

[[nodiscard]] std::pair<double, double> oriented_offset(double x, double y, Orientation value) {
  switch (value) {
  case Orientation::N:
    return {x, y};
  case Orientation::E:
    return {y, -x};
  case Orientation::S:
    return {-x, -y};
  case Orientation::W:
    return {-y, x};
  case Orientation::FN:
    return {-x, y};
  case Orientation::FE:
    return {y, x};
  case Orientation::FS:
    return {x, -y};
  case Orientation::FW:
    return {-y, -x};
  }
  return {x, y};
}

struct TopPinState {
  PinDirection direction{PinDirection::Unknown};
  std::string net_name;
  bool connected{};
};

class MasterIndices {
public:
  explicit MasterIndices(std::size_t macro_count) : narrow_(macro_count < NARROW_TOP_PIN) {}

  void reserve(std::size_t count) {
    if (narrow_)
      narrow_indices_.reserve(count);
    else
      wide_indices_.reserve(count);
  }

  void push_macro(std::uint32_t macro) {
    if (narrow_)
      narrow_indices_.push_back(static_cast<std::uint16_t>(macro));
    else
      wide_indices_.push_back(macro);
  }

  void push_top_pin() {
    if (narrow_)
      narrow_indices_.push_back(NARROW_TOP_PIN);
    else
      wide_indices_.push_back(WIDE_TOP_PIN);
  }

  [[nodiscard]] std::optional<std::uint32_t> operator[](std::size_t idx) const {
    if (narrow_) {
      const auto value = narrow_indices_[idx];
      return value == NARROW_TOP_PIN ? std::nullopt : std::optional<std::uint32_t>(value);
    }
    const auto value = wide_indices_[idx];
    return value == WIDE_TOP_PIN ? std::nullopt : std::optional<std::uint32_t>(value);
  }

private:
  static constexpr std::uint16_t NARROW_TOP_PIN = std::numeric_limits<std::uint16_t>::max();
  static constexpr std::uint32_t WIDE_TOP_PIN = std::numeric_limits<std::uint32_t>::max();

  bool narrow_{};
  std::vector<std::uint16_t> narrow_indices_;
  std::vector<std::uint32_t> wide_indices_;
};

class DefParser {
public:
  DefParser(const std::filesystem::path &path, const Library &library)
      : path_(path), tokens_(path), library_(library), masters_(library.macro_count()) {}

  [[nodiscard]] Board parse() {
    while (tokens_.next()) {
      if (tokens_.token() == "DESIGN") {
        tokens_.require("design name");
        board_.name = tokens_.token();
        tokens_.expect(";");
      } else if (tokens_.token() == "UNITS") {
        parse_units();
      } else if (tokens_.token() == "ROW") {
        parse_row();
      } else if (tokens_.token() == "COMPONENTS") {
        parse_components();
      } else if (tokens_.token() == "PINS") {
        parse_pins();
      } else if (tokens_.token() == "BLOCKAGES") {
        parse_blockages();
      } else if (tokens_.token() == "NETS") {
        parse_nets();
      } else if (tokens_.token() == "VIAS") {
        // Several ISPD 2015 DEFs overstate this count. The routing vias are not
        // part of Board, so accept fewer records while still enforcing the
        // declared count as an upper bound.
        skip_counted_section(tokens_, CountRequirement::UpperBound);
      } else if (tokens_.token() == "NONDEFAULTRULES" || tokens_.token() == "SPECIALNETS" || tokens_.token() == "REGIONS" ||
                 tokens_.token() == "GROUPS") {
        skip_counted_section(tokens_);
      } else if (tokens_.token() == "END") {
        tokens_.require("DEF end name");
        if (tokens_.token() != "DESIGN")
          tokens_.fail("unexpected DEF END " + std::string(tokens_.token()));
        ended_ = true;
        break;
      } else if (tokens_.token() != ";") {
        skip_statement(tokens_);
      }
    }

    if (!ended_)
      tokens_.fail("missing END DESIGN");
    if (board_.name.empty())
      tokens_.fail("missing DESIGN statement");
    if (!units_seen_)
      tokens_.fail("missing DEF distance units");
    if (!comps_seen_ || !pins_seen_ || !nets_seen_)
      tokens_.fail("DEF requires COMPONENTS, PINS, and NETS sections");
    if (board_.rows.empty())
      tokens_.fail("DEF contains no placement rows");

    apply_blockages();
    validate_unique_names(board_.nets, path_);
    return std::move(board_);
  }

private:
  static constexpr std::uint32_t NO_MACRO = std::numeric_limits<std::uint32_t>::max();

  [[nodiscard]] double scale(double value) const {
    if (!units_seen_)
      tokens_.fail("UNITS must precede geometric records");
    return value / static_cast<double>(db_units_);
  }

  [[nodiscard]] double next_scaled(std::string_view description) { return scale(next_number<double>(tokens_, description)); }

  void parse_units() {
    if (units_seen_)
      tokens_.fail("duplicate UNITS statement");
    tokens_.expect("DISTANCE");
    tokens_.expect("MICRONS");
    db_units_ = next_number<std::uint64_t>(tokens_, "DEF database units");
    if (db_units_ == 0)
      tokens_.fail("DEF database units must be positive");
    tokens_.expect(";");
    units_seen_ = true;
  }

  void parse_row() {
    if (!units_seen_)
      tokens_.fail("UNITS must precede ROW statements");
    tokens_.require("row name");
    tokens_.require("row site name");
    const auto site = library_.find_site(tokens_.token());
    if (!site)
      tokens_.fail("row references unknown site '" + std::string(tokens_.token()) + "'");

    const auto x = next_scaled("row X coordinate");
    const auto y = next_scaled("row Y coordinate");
    tokens_.require("row orientation");
    const auto row_orient = orientation(tokens_.token(), tokens_);
    tokens_.expect("DO");
    const auto x_count = next_number<std::uint64_t>(tokens_, "row X site count");
    tokens_.expect("BY");
    const auto y_count = next_number<std::uint64_t>(tokens_, "row Y site count");
    tokens_.expect("STEP");
    const auto x_step = next_scaled("row X step");
    const auto y_step = next_scaled("row Y step");
    tokens_.expect(";");
    if (x_count == 0 || y_count == 0 || x_step <= 0 || (y_count > 1 && y_step <= 0))
      tokens_.fail("invalid row repetition");
    if (y_count > std::numeric_limits<std::size_t>::max() - board_.rows.size())
      tokens_.fail("row count exceeds placement model limit");

    const auto &def = library_.site(*site);
    board_.rows.reserve(board_.rows.size() + static_cast<std::size_t>(y_count));
    for (std::uint64_t i = 0; i < y_count; ++i) {
      Row row;
      row.coordinate = y + static_cast<double>(i) * y_step;
      row.height = def.height;
      row.site_width = def.width;
      row.site_spacing = x_step;
      row.site_orientation = row_orient;
      row.symmetry = def.symmetry;
      row.subrows.push_back({x, x_count});
      board_.rows.push_back(std::move(row));
    }
  }

  [[nodiscard]] Location parse_component_location(PlacementStatus status) {
    tokens_.expect("(");
    Location loc;
    loc.x = next_scaled("component X coordinate");
    loc.y = next_scaled("component Y coordinate");
    tokens_.expect(")");
    tokens_.require("component orientation");
    loc.orientation = orientation(tokens_.token(), tokens_);
    loc.status = status;
    return loc;
  }

  void parse_components() {
    if (comps_seen_)
      tokens_.fail("duplicate COMPONENTS section");

    CountedSection components(tokens_);
    if (components.declared() >= NO_MACRO)
      tokens_.fail("component count exceeds placement model limit");
    board_.cells.reserve(static_cast<std::size_t>(components.declared()));
    masters_.reserve(static_cast<std::size_t>(components.declared()));

    while (components.next()) {
      tokens_.require("component instance name");
      Cell cell;
      cell.name = tokens_.token();
      tokens_.require("component macro name");
      const auto macro = library_.find_macro(tokens_.token());
      if (!macro)
        tokens_.fail("component references unknown macro '" + std::string(tokens_.token()) + "'");
      const auto &def = library_.macro(*macro);
      cell.width = def.width;
      cell.height = def.height;
      cell.macro = def.macro;

      while (tokens_.next() && tokens_.token() != ";")
        if (tokens_.token() == "PLACED")
          cell.location = parse_component_location(PlacementStatus::Movable);
        else if (tokens_.token() == "FIXED" || tokens_.token() == "COVER")
          cell.location = parse_component_location(PlacementStatus::Fixed);

      board_.cells.push_back(std::move(cell));
      masters_.push_macro(*macro);
    }

    comps_seen_ = true;
  }

  [[nodiscard]] Bounds parse_def_rectangle() {
    tokens_.expect("(");
    const auto x0 = next_scaled("rectangle X coordinate");
    const auto y0 = next_scaled("rectangle Y coordinate");
    tokens_.expect(")");
    tokens_.expect("(");
    const auto x1 = next_scaled("rectangle X coordinate");
    const auto y1 = next_scaled("rectangle Y coordinate");
    tokens_.expect(")");
    Bounds res;
    res.include(x0, y0, x1, y1);
    return res;
  }

  void parse_pins() {
    if (!comps_seen_)
      tokens_.fail("COMPONENTS must precede PINS");
    if (pins_seen_)
      tokens_.fail("duplicate PINS section");

    CountedSection pins(tokens_);
    if (pins.declared() >= static_cast<std::uint64_t>(NO_MACRO) - board_.cells.size())
      tokens_.fail("cell count exceeds placement model limit");
    board_.cells.reserve(board_.cells.size() + static_cast<std::size_t>(pins.declared()));
    masters_.reserve(board_.cells.capacity());
    first_top_pin_ = board_.cells.size();
    top_pins_.reserve(static_cast<std::size_t>(pins.declared()));

    while (pins.next()) {
      tokens_.require("top-level pin name");
      Cell cell;
      cell.name = tokens_.token();
      cell.kind = CellKind::TerminalNonInteracting;
      PinDirection direction = PinDirection::Unknown;
      bool direction_seen = false;
      std::string net_name;
      std::optional<Location> anchor;
      Bounds geometry;

      while (tokens_.next() && tokens_.token() != ";") {
        if (tokens_.token() == "NET") {
          tokens_.require("top-level pin net name");
          net_name = tokens_.token();
        } else if (tokens_.token() == "DIRECTION") {
          tokens_.require("top-level pin direction");
          direction = pin_direction(tokens_.token(), tokens_);
          direction_seen = true;
        } else if (tokens_.token() == "PLACED" || tokens_.token() == "FIXED" || tokens_.token() == "COVER") {
          anchor = parse_component_location(PlacementStatus::FixedNonInteracting);
        } else if (tokens_.token() == "LAYER") {
          tokens_.require("pin layer name");
          const auto rect = parse_def_rectangle();
          geometry.include(rect.min_x, rect.min_y, rect.max_x, rect.max_y);
        }
      }

      if (!direction_seen)
        tokens_.fail("top-level pin '" + cell.name + "' has no direction");
      if (net_name.empty())
        tokens_.fail("top-level pin '" + cell.name + "' has no net");
      if (geometry.empty())
        tokens_.fail("top-level pin '" + cell.name + "' has no rectangular geometry");
      if (anchor) {
        const auto center_x = (geometry.min_x + geometry.max_x) / 2.0;
        const auto center_y = (geometry.min_y + geometry.max_y) / 2.0;
        const auto [dx, dy] = oriented_offset(center_x, center_y, anchor->orientation);
        anchor->x += dx;
        anchor->y += dy;
        anchor->orientation = Orientation::N;
        cell.location = anchor;
      }

      board_.cells.push_back(std::move(cell));
      masters_.push_top_pin();
      top_pins_.push_back({direction, std::move(net_name), false});
    }

    pins_seen_ = true;
  }

  void parse_blockages() {
    CountedSection blockages(tokens_);
    while (blockages.next()) {
      bool placement = false;
      while (tokens_.next() && tokens_.token() != ";") {
        if (tokens_.token() == "PLACEMENT")
          placement = true;
        else if (tokens_.token() == "LAYER") {
          placement = false;
          tokens_.require("blockage layer name");
        } else if (tokens_.token() == "RECT") {
          const auto rect = parse_def_rectangle();
          if (placement)
            blockages_.push_back(rect);
        }
      }
    }
  }

  void parse_nets() {
    if (!comps_seen_ || !pins_seen_)
      tokens_.fail("COMPONENTS and PINS must precede NETS");
    if (nets_seen_)
      tokens_.fail("duplicate NETS section");

    CountedSection nets(tokens_);
    if (nets.declared() > std::numeric_limits<std::size_t>::max())
      tokens_.fail("net count exceeds placement model limit");
    board_.nets.reserve(static_cast<std::size_t>(nets.declared()));
    const auto pin_capacity = nets.declared() > std::numeric_limits<std::size_t>::max() / 4 ? std::numeric_limits<std::size_t>::max()
                                                                                            : static_cast<std::size_t>(nets.declared()) * 4;
    board_.pins.reserve(pin_capacity);
    const NameIndex<Cell> cell_idx(board_.cells, path_);

    while (nets.next()) {
      tokens_.require("net name");
      Net net;
      net.name = tokens_.token();
      net.first_pin = board_.pins.size();

      while (tokens_.next() && tokens_.token() != ";") {
        if (tokens_.token() == "(") {
          tokens_.require("net endpoint");
          Pin pin;
          if (tokens_.token() == "PIN") {
            tokens_.require("top-level pin endpoint name");
            const auto cell = cell_idx.find(tokens_.token());
            const auto master = cell ? masters_[*cell] : std::nullopt;
            if (!cell || master)
              tokens_.fail("net references unknown top-level pin '" + std::string(tokens_.token()) + "'");
            pin.cell = *cell;
            const auto top_pin = static_cast<std::size_t>(*cell) - first_top_pin_;
            auto &state = top_pins_[top_pin];
            if (state.net_name != net.name)
              tokens_.fail("top-level pin belongs to net '" + state.net_name + "', not '" + net.name + "'");
            if (state.connected)
              tokens_.fail("duplicate net endpoint for top-level pin '" + board_.cells[*cell].name + "'");
            state.connected = true;
            pin.direction = state.direction;
          } else {
            const auto cell = cell_idx.find(tokens_.token());
            const auto master = cell ? masters_[*cell] : std::nullopt;
            if (!master)
              tokens_.fail("net references unknown component '" + std::string(tokens_.token()) + "'");
            pin.cell = *cell;
            const auto &macro = library_.macro(*master);
            tokens_.require("component pin name");
            const auto *def = macro.find_pin(tokens_.token());
            if (!def)
              tokens_.fail("component pin references unknown macro pin '" + std::string(tokens_.token()) + "'");
            pin.direction = def->direction;
            pin.offset_x = def->x - macro.width / 2.0;
            pin.offset_y = def->y - macro.height / 2.0;
          }
          tokens_.expect(")");
          board_.pins.push_back(pin);
        } else if (tokens_.token() == "+") {
          tokens_.require("net attribute");
          if (tokens_.token() != "NONDEFAULTRULE")
            tokens_.fail("unsupported net attribute '" + std::string(tokens_.token()) + "'");
          tokens_.require("nondefault rule name");
        } else {
          tokens_.fail("unexpected token in net record '" + std::string(tokens_.token()) + "'");
        }
      }

      net.pin_count = board_.pins.size() - net.first_pin;
      board_.nets.push_back(std::move(net));
    }

    if (std::ranges::find(top_pins_, false, &TopPinState::connected) != top_pins_.end())
      tokens_.fail("a top-level pin is missing from its declared net");
    nets_seen_ = true;
  }

  [[nodiscard]] static std::uint64_t clamp_site_index(double value, std::uint64_t count) {
    if (value <= 0)
      return 0;
    if (value >= static_cast<double>(count))
      return count;
    return static_cast<std::uint64_t>(value);
  }

  void apply_blockages() {
    for (const auto &blkg : blockages_) {
      for (auto &row : board_.rows) {
        if (blkg.min_y >= row.coordinate + row.height || blkg.max_y <= row.coordinate)
          continue;

        std::vector<Subrow> kept;
        kept.reserve(row.subrows.size() + 1);
        for (const auto &subrow : row.subrows) {
          const auto first_pos = std::floor((blkg.min_x - row.site_width - subrow.origin) / row.site_spacing) + 1.0;
          const auto last_pos = std::ceil((blkg.max_x - subrow.origin) / row.site_spacing);
          const auto first = clamp_site_index(first_pos, subrow.site_count);
          const auto last = clamp_site_index(last_pos, subrow.site_count);
          if (first >= last) {
            kept.push_back(subrow);
            continue;
          }
          if (first != 0)
            kept.push_back({subrow.origin, first});
          if (last != subrow.site_count)
            kept.push_back({subrow.origin + static_cast<double>(last) * row.site_spacing, subrow.site_count - last});
        }
        row.subrows = std::move(kept);
      }
    }
  }

  std::filesystem::path path_;
  Tokens tokens_;
  const Library &library_;
  Board board_;
  MasterIndices masters_;
  std::vector<TopPinState> top_pins_;
  std::vector<Bounds> blockages_;
  std::size_t first_top_pin_{};
  std::uint64_t db_units_{};
  bool units_seen_{};
  bool comps_seen_{};
  bool pins_seen_{};
  bool nets_seen_{};
  bool ended_{};
};

class LefDefParser final : public Parser {
public:
  explicit LefDefParser(LefDefParseOptions opts) : opts_(std::move(opts)) {}

  [[nodiscard]] Board parse(const std::filesystem::path &in) const override {
    if (opts_.lef_files.empty())
      throw Error("LEF/DEF parsing requires at least one --lef-file");

    Library library;
    for (const auto &path : opts_.lef_files)
      parse_lef(path, library);
    library.finalize();

    return DefParser(in, library).parse();
  }

private:
  LefDefParseOptions opts_;
};

} // namespace

std::unique_ptr<Parser> make_parser(LefDefParseOptions opts) { return std::make_unique<LefDefParser>(std::move(opts)); }

} // namespace placement
