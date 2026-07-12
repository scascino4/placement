#include "placement/serialization/serializer.hpp"

#include "placement/error.hpp"

#include <algorithm>
#include <array>
#include <bit>
#include <cctype>
#include <chrono>
#include <concepts>
#include <cstdint>
#include <fstream>
#include <limits>
#include <string>
#include <system_error>
#include <type_traits>

namespace placement {
namespace {

constexpr std::array<char, 8> MAGIC{'P', 'L', 'A', 'C', 'E', 'B', 'I', 'N'};
constexpr std::uint16_t MAJOR_VERSION = 1;
constexpr std::uint16_t MINOR_VERSION = 0;
constexpr std::uint64_t MAX_RECORDS = 1'000'000'000;
constexpr std::uint32_t MAX_STRING = 64 * 1024 * 1024;
constexpr std::uint32_t MAX_WEIGHTS = 1'000'000;

class BinarySerializer final : public Serializer {
public:
  void write(const Board &board, const std::filesystem::path &output) const override;
  [[nodiscard]] Board read(const std::filesystem::path &input) const override;
};

[[nodiscard]] std::string lower(std::string_view value) {
  std::string result(value);
  std::ranges::transform(result, result.begin(), [](unsigned char character) { return static_cast<char>(std::tolower(character)); });
  return result;
}

// These small wrappers keep byte order and bounds checks in one place. The
// format is explicitly little-endian, independent of the host architecture.
class Output {
public:
  explicit Output(const std::filesystem::path &path) : stream_(path, std::ios::binary) {
    if (!stream_) {
      throw Error("cannot create " + path.string());
    }
  }

  void bytes(const void *data, std::size_t size) {
    stream_.write(static_cast<const char *>(data), static_cast<std::streamsize>(size));
    if (!stream_) {
      throw Error("failed while writing binary placement");
    }
  }

  template <std::unsigned_integral T> void integer(T value) {
    std::array<std::uint8_t, sizeof(T)> encoded{};
    for (std::size_t i = 0; i < encoded.size(); ++i)
      encoded[i] = static_cast<std::uint8_t>(value >> (i * 8));
    bytes(encoded.data(), encoded.size());
  }

  void real(double value) { integer(std::bit_cast<std::uint64_t>(value)); }

  void string(std::string_view value) {
    if (value.size() > MAX_STRING) {
      throw Error("string exceeds binary format limit");
    }
    integer(static_cast<std::uint32_t>(value.size()));
    bytes(value.data(), value.size());
  }

  void weights(const std::vector<double> &values) {
    if (values.size() > MAX_WEIGHTS) {
      throw Error("weight vector exceeds binary format limit");
    }
    integer(static_cast<std::uint32_t>(values.size()));
    for (const auto value : values)
      real(value);
  }

  void finish() {
    stream_.flush();
    if (!stream_)
      throw Error("failed while finalizing binary placement");
  }

private:
  std::ofstream stream_;
};

class Input {
public:
  explicit Input(const std::filesystem::path &path) : path_(path), stream_(path, std::ios::binary) {
    if (!stream_) {
      throw Error("cannot open " + path.string());
    }
  }

  void bytes(void *data, std::size_t size) {
    stream_.read(static_cast<char *>(data), static_cast<std::streamsize>(size));
    if (stream_.gcount() != static_cast<std::streamsize>(size))
      throw Error(path_.string() + ": truncated binary placement");
  }

  template <std::unsigned_integral T> [[nodiscard]] T integer() {
    std::array<std::uint8_t, sizeof(T)> encoded{};
    bytes(encoded.data(), encoded.size());
    std::uint64_t value{};
    for (std::size_t i = 0; i < encoded.size(); ++i)
      value |= static_cast<std::uint64_t>(encoded[i]) << (i * 8);
    return static_cast<T>(value);
  }

  [[nodiscard]] double real() { return std::bit_cast<double>(integer<std::uint64_t>()); }

  [[nodiscard]] std::string string() {
    const auto size = integer<std::uint32_t>();
    if (size > MAX_STRING) {
      throw Error(path_.string() + ": invalid string length");
    }
    std::string value(size, '\0');
    bytes(value.data(), value.size());
    return value;
  }

  [[nodiscard]] std::vector<double> weights() {
    const auto size = integer<std::uint32_t>();
    if (size > MAX_WEIGHTS) {
      throw Error(path_.string() + ": invalid weight count");
    }
    std::vector<double> values(size);
    for (auto &value : values)
      value = real();
    return values;
  }

  template <typename Enum> [[nodiscard]] Enum enumeration(std::uint8_t max, std::string_view name) {
    const auto value = integer<std::uint8_t>();
    if (value > max)
      throw Error(path_.string() + ": invalid " + std::string(name));
    return static_cast<Enum>(value);
  }

  void require_end() {
    char byte{};
    if (stream_.read(&byte, 1)) {
      throw Error(path_.string() + ": trailing binary data");
    }
    if (!stream_.eof()) {
      throw Error(path_.string() + ": failed while reading binary placement");
    }
  }

  [[nodiscard]] const std::filesystem::path &path() const { return path_; }

private:
  std::filesystem::path path_;
  std::ifstream stream_;
};

[[nodiscard]] std::filesystem::path temp_path(const std::filesystem::path &output) {
  const auto tick = std::chrono::steady_clock::now().time_since_epoch().count();
  return output.string() + ".tmp." + std::to_string(tick);
}

template <typename Function> void atomic_output(const std::filesystem::path &output, Function &&function) {
  if (!output.parent_path().empty()) {
    std::filesystem::create_directories(output.parent_path());
  }

  // Never expose a partially written placement. The fallback removal handles
  // platforms whose rename operation cannot replace an existing file.
  const auto temp = temp_path(output);
  try {
    function(temp);
    std::error_code error;
    std::filesystem::rename(temp, output, error);
    if (error) {
      std::filesystem::remove(output, error);
      error.clear();
      std::filesystem::rename(temp, output, error);
    }
    if (error) {
      throw Error("cannot replace " + output.string() + ": " + error.message());
    }
  } catch (...) {
    std::error_code ignored;
    std::filesystem::remove(temp, ignored);
    throw;
  }
}

void check_count(std::uint64_t count, const Input &input, std::string_view name) {
  if (count > MAX_RECORDS)
    throw Error(input.path().string() + ": invalid " + std::string(name) + " count");
}

} // namespace

void BinarySerializer::write(const Board &board, const std::filesystem::path &output) const {
  atomic_output(output, [&](const auto &temp) {
    Output writer(temp);

    // The fixed-size header leaves flags available for future compatible
    // additions while major versions continue to identify incompatible data.
    writer.bytes(MAGIC.data(), MAGIC.size());
    writer.integer(MAJOR_VERSION);
    writer.integer(MINOR_VERSION);
    writer.integer<std::uint32_t>(0);
    writer.string(board.name);
    writer.integer(static_cast<std::uint64_t>(board.cells.size()));
    writer.integer(static_cast<std::uint64_t>(board.rows.size()));
    writer.integer(static_cast<std::uint64_t>(board.nets.size()));
    writer.integer(static_cast<std::uint64_t>(board.pins.size()));

    for (const auto &cell : board.cells) {
      writer.string(cell.name);
      writer.real(cell.width);
      writer.real(cell.height);
      writer.integer(static_cast<std::uint8_t>(cell.kind));
      writer.integer<std::uint8_t>(cell.location.has_value());
      if (cell.location) {
        writer.real(cell.location->x);
        writer.real(cell.location->y);
        writer.integer(static_cast<std::uint8_t>(cell.location->orientation));
        writer.integer(static_cast<std::uint8_t>(cell.location->status));
        const bool has_dimensions = cell.location->width && cell.location->height;
        writer.integer<std::uint8_t>(has_dimensions);
        if (has_dimensions) {
          writer.real(*cell.location->width);
          writer.real(*cell.location->height);
        }
      }
      writer.weights(cell.weights);
    }

    for (const auto &row : board.rows) {
      writer.real(row.coordinate);
      writer.real(row.height);
      writer.real(row.site_width);
      writer.real(row.site_spacing);
      writer.integer(static_cast<std::uint8_t>(row.site_orientation));
      writer.integer(row.symmetry);
      writer.integer(static_cast<std::uint64_t>(row.subrows.size()));
      for (const auto &subrow : row.subrows) {
        writer.real(subrow.origin);
        writer.integer(subrow.site_count);
      }
    }

    for (const auto &net : board.nets) {
      writer.string(net.name);
      writer.integer(net.first_pin);
      writer.integer(net.pin_count);
      writer.weights(net.weights);
    }

    for (const auto &pin : board.pins) {
      writer.integer(pin.cell);
      writer.integer(static_cast<std::uint8_t>(pin.direction));
      writer.real(pin.offset_x);
      writer.real(pin.offset_y);
    }
    writer.finish();
  });
}

Board BinarySerializer::read(const std::filesystem::path &input) const {
  Input reader(input);
  std::array<char, MAGIC.size()> magic{};
  reader.bytes(magic.data(), magic.size());
  if (magic != MAGIC)
    throw Error(input.string() + ": invalid binary magic");
  const auto major = reader.integer<std::uint16_t>();
  const auto minor = reader.integer<std::uint16_t>();

  // Newer minor versions remain readable as long as they only use fields and
  // flags understood by this reader.
  (void)minor;
  if (major != MAJOR_VERSION) {
    throw Error(input.string() + ": unsupported binary major version");
  }
  if (reader.integer<std::uint32_t>() != 0)
    throw Error(input.string() + ": unsupported binary flags");

  Board board;
  board.name = reader.string();

  const auto cell_count = reader.integer<std::uint64_t>();
  const auto row_count = reader.integer<std::uint64_t>();
  const auto net_count = reader.integer<std::uint64_t>();
  const auto pin_count = reader.integer<std::uint64_t>();

  check_count(cell_count, reader, "cell");
  check_count(row_count, reader, "row");
  check_count(net_count, reader, "net");
  check_count(pin_count, reader, "pin");

  board.cells.reserve(static_cast<std::size_t>(cell_count));
  board.rows.reserve(static_cast<std::size_t>(row_count));
  board.nets.reserve(static_cast<std::size_t>(net_count));
  board.pins.reserve(static_cast<std::size_t>(pin_count));

  for (std::uint64_t i = 0; i < cell_count; ++i) {
    Cell cell;
    cell.name = reader.string();
    cell.width = reader.real();
    cell.height = reader.real();
    cell.kind = reader.enumeration<CellKind>(2, "cell kind");

    const auto has_location = reader.integer<std::uint8_t>();
    if (has_location > 1) {
      throw Error(input.string() + ": invalid placement presence flag");
    }

    if (has_location) {
      Location location;
      location.x = reader.real();
      location.y = reader.real();
      location.orientation = reader.enumeration<Orientation>(7, "orientation");
      location.status = reader.enumeration<PlacementStatus>(2, "placement status");

      const auto has_dimensions = reader.integer<std::uint8_t>();
      if (has_dimensions > 1) {
        throw Error(input.string() + ": invalid dimensions presence flag");
      }

      if (has_dimensions) {
        location.width = reader.real();
        location.height = reader.real();
      }

      cell.location = location;
    }

    cell.weights = reader.weights();
    board.cells.push_back(std::move(cell));
  }

  for (std::uint64_t i = 0; i < row_count; ++i) {
    Row row;
    row.coordinate = reader.real();
    row.height = reader.real();
    row.site_width = reader.real();
    row.site_spacing = reader.real();
    row.site_orientation = reader.enumeration<Orientation>(7, "row orientation");

    row.symmetry = reader.integer<std::uint8_t>();
    if (row.symmetry > 7) {
      throw Error(input.string() + ": invalid row symmetry");
    }

    const auto subrow_count = reader.integer<std::uint64_t>();
    check_count(subrow_count, reader, "subrow");
    row.subrows.reserve(static_cast<std::size_t>(subrow_count));
    for (std::uint64_t subrow = 0; subrow < subrow_count; ++subrow)
      row.subrows.push_back({reader.real(), reader.integer<std::uint64_t>()});
    board.rows.push_back(std::move(row));
  }

  for (std::uint64_t i = 0; i < net_count; ++i) {
    Net net;
    net.name = reader.string();
    net.first_pin = reader.integer<std::uint64_t>();
    net.pin_count = reader.integer<std::uint64_t>();
    if (net.first_pin > pin_count || net.pin_count > pin_count - net.first_pin)
      throw Error(input.string() + ": net pin range is out of bounds");
    net.weights = reader.weights();
    board.nets.push_back(std::move(net));
  }

  for (std::uint64_t i = 0; i < pin_count; ++i) {
    Pin pin;

    pin.cell = reader.integer<std::uint32_t>();
    if (pin.cell >= cell_count) {
      throw Error(input.string() + ": pin cell index is out of bounds");
    }

    pin.direction = reader.enumeration<PinDirection>(3, "pin direction");
    pin.offset_x = reader.real();
    pin.offset_y = reader.real();
    board.pins.push_back(pin);
  }

  reader.require_end();
  return board;
}

std::unique_ptr<Serializer> make_serializer(std::string_view format) {
  if (lower(format) == "binary")
    return std::make_unique<BinarySerializer>();
  throw Error("unsupported serialization format '" + std::string(format) + "'");
}

} // namespace placement
