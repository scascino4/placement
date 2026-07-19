#include "placement/serialization/serializer.hpp"

#include "../atomic_output.hpp"
#include "../text.hpp"
#include "placement/error.hpp"

#include <algorithm>
#include <array>
#include <bit>
#include <concepts>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <string>
#include <type_traits>

namespace placement {
namespace {

constexpr std::array<char, 8> MAGIC{'P', 'L', 'A', 'C', 'E', 'B', 'I', 'N'};
constexpr std::uint64_t MAX_RECORDS = 1'000'000'000;
constexpr std::uint32_t MAX_STRING = 64 * 1024 * 1024;
constexpr std::uint32_t MAX_WEIGHTS = 1'000'000;
constexpr std::size_t IO_BUF_SIZE = 256 * 1024;

// Min encoded sizes omit variable-length contents and optional fields.
// Expressing them in terms of their wire fields keeps the count guards aligned
// with the read and write routines below.
constexpr std::uint64_t STRING_LENGTH_BYTES = sizeof(std::uint32_t);
constexpr std::uint64_t WEIGHT_COUNT_BYTES = sizeof(std::uint32_t);
constexpr std::uint64_t RECORD_COUNT_BYTES = sizeof(std::uint64_t);
constexpr std::uint64_t REAL_BYTES = sizeof(std::uint64_t);
constexpr std::uint64_t BOOLEAN_BYTES = sizeof(std::uint8_t);
constexpr std::uint64_t ENUM_BYTES = sizeof(std::uint8_t);
constexpr std::uint64_t SYMMETRY_BYTES = sizeof(std::uint8_t);
constexpr std::uint64_t MIN_CELL_BYTES = STRING_LENGTH_BYTES + 2 * REAL_BYTES + ENUM_BYTES + 2 * BOOLEAN_BYTES + WEIGHT_COUNT_BYTES;
constexpr std::uint64_t MIN_ROW_BYTES = 4 * REAL_BYTES + ENUM_BYTES + SYMMETRY_BYTES + RECORD_COUNT_BYTES;
constexpr std::uint64_t MIN_NET_BYTES = STRING_LENGTH_BYTES + 2 * RECORD_COUNT_BYTES + WEIGHT_COUNT_BYTES;
constexpr std::uint64_t MIN_PIN_BYTES = sizeof(std::uint32_t) + ENUM_BYTES + 2 * REAL_BYTES;
constexpr std::uint64_t MIN_SUBROW_BYTES = REAL_BYTES + RECORD_COUNT_BYTES;

class BinarySerializer final : public Serializer {
public:
  void write(const Board &board, const std::filesystem::path &out) const override;
  [[nodiscard]] Board read(const std::filesystem::path &in) const override;
};

// These small wrappers keep byte order and bounds checks in one place. The
// format is explicitly little-endian, independent of the host architecture.
class Output {
public:
  explicit Output(const std::filesystem::path &path) {
    stream_.open(path, std::ios::binary);
    if (!stream_)
      throw Error("cannot create " + path.string());
  }

  void bytes(const void *data, std::size_t size) {
    auto *src = static_cast<const char *>(data);
    while (size != 0) {
      if (used_ == buf_.size())
        flush_buf();

      const auto count = std::min(size, buf_.size() - used_);
      std::memcpy(buf_.data() + used_, src, count);
      used_ += count;
      src += count;
      size -= count;
    }
  }

  template <std::unsigned_integral T> void integer(T value) {
    if (buf_.size() - used_ < sizeof(T))
      flush_buf();

    for (std::size_t i = 0; i < sizeof(T); ++i)
      buf_[used_++] = static_cast<char>(static_cast<std::uint8_t>(value >> (i * 8)));
  }

  void real(double value) { integer(std::bit_cast<std::uint64_t>(value)); }

  void boolean(bool value) { integer<std::uint8_t>(value); }

  void string(std::string_view value) {
    if (value.size() > MAX_STRING)
      throw Error("string exceeds binary format limit");

    integer(static_cast<std::uint32_t>(value.size()));
    bytes(value.data(), value.size());
  }

  void weights(const std::vector<double> &values) {
    if (values.size() > MAX_WEIGHTS)
      throw Error("weight vector exceeds binary format limit");

    integer(static_cast<std::uint32_t>(values.size()));
    for (const auto value : values)
      real(value);
  }

  void finish() {
    flush_buf();
    stream_.flush();
    if (!stream_)
      throw Error("failed while finalizing binary placement");
  }

private:
  void flush_buf() {
    if (used_ == 0)
      return;

    stream_.write(buf_.data(), static_cast<std::streamsize>(used_));
    if (!stream_)
      throw Error("failed while writing binary placement");
    used_ = 0;
  }

  std::array<char, IO_BUF_SIZE> buf_{};
  std::size_t used_{};
  std::ofstream stream_;
};

class Input {
public:
  explicit Input(const std::filesystem::path &path) : path_(path) {
    stream_.open(path, std::ios::binary);
    if (!stream_)
      throw Error("cannot open " + path.string());

    stream_.seekg(0, std::ios::end);
    const auto end = stream_.tellg();
    if (end < 0)
      throw Error("cannot determine size of " + path.string());
    total_size_ = static_cast<std::uint64_t>(end);
    stream_.seekg(0, std::ios::beg);
    if (!stream_)
      throw Error("cannot read " + path.string());
  }

  void bytes(void *data, std::size_t size) {
    auto *dst = static_cast<char *>(data);
    while (size != 0) {
      if (pos_ == avail_)
        refill(1);

      const auto count = std::min(size, avail_ - pos_);
      std::memcpy(dst, buf_.data() + pos_, count);
      pos_ += count;
      dst += count;
      size -= count;
      consumed_ += count;
    }
  }

  template <std::unsigned_integral T> [[nodiscard]] T integer() {
    if (avail_ - pos_ < sizeof(T))
      refill(sizeof(T));

    std::uint64_t value{};
    for (std::size_t i = 0; i < sizeof(T); ++i)
      value |= static_cast<std::uint64_t>(static_cast<unsigned char>(buf_[pos_++])) << (i * 8);
    consumed_ += sizeof(T);
    return static_cast<T>(value);
  }

  [[nodiscard]] double real() { return std::bit_cast<double>(integer<std::uint64_t>()); }

  [[nodiscard]] std::string string() {
    const auto size = integer<std::uint32_t>();
    if (size > MAX_STRING)
      throw Error(path_.string() + ": invalid string length");
    if (size > remaining_bytes())
      throw Error(path_.string() + ": truncated binary placement");

    std::string value(size, '\0');
    bytes(value.data(), value.size());
    return value;
  }

  [[nodiscard]] std::vector<double> weights() {
    const auto size = integer<std::uint32_t>();
    if (size > MAX_WEIGHTS)
      throw Error(path_.string() + ": invalid weight count");
    if (size > remaining_bytes() / sizeof(double))
      throw Error(path_.string() + ": truncated binary placement");

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

  [[nodiscard]] bool boolean(std::string_view name) {
    const auto value = integer<std::uint8_t>();
    if (value > 1)
      throw Error(path_.string() + ": invalid " + std::string(name));

    return value != 0;
  }

  void require_end() {
    if (pos_ != avail_)
      throw Error(path_.string() + ": trailing binary data");

    char byte{};
    if (stream_.read(&byte, 1))
      throw Error(path_.string() + ": trailing binary data");
    if (!stream_.eof())
      throw Error(path_.string() + ": failed while reading binary placement");
  }

  [[nodiscard]] const std::filesystem::path &path() const { return path_; }
  [[nodiscard]] std::uint64_t remaining_bytes() const { return consumed_ <= total_size_ ? total_size_ - consumed_ : 0; }

private:
  void refill(std::size_t required) {
    const auto left = avail_ - pos_;
    if (left != 0)
      std::memmove(buf_.data(), buf_.data() + pos_, left);

    stream_.read(buf_.data() + left, static_cast<std::streamsize>(buf_.size() - left));
    const auto read = stream_.gcount();
    pos_ = 0;
    avail_ = left + static_cast<std::size_t>(read);
    if (avail_ < required)
      throw Error(path_.string() + ": truncated binary placement");
  }

  std::filesystem::path path_;
  std::array<char, IO_BUF_SIZE> buf_{};
  std::size_t pos_{};
  std::size_t avail_{};
  std::uint64_t total_size_{};
  std::uint64_t consumed_{};
  std::ifstream stream_;
};

[[nodiscard]] std::uint64_t checked_min_bytes(std::uint64_t count, const Input &in, std::string_view name, std::uint64_t min_record_bytes,
                                              std::uint64_t prior_min_bytes = 0) {
  if (count > MAX_RECORDS)
    throw Error(in.path().string() + ": invalid " + std::string(name) + " count");
  const auto remaining = in.remaining_bytes();
  if (prior_min_bytes > remaining || count > (remaining - prior_min_bytes) / min_record_bytes)
    throw Error(in.path().string() + ": impossible " + std::string(name) + " count for remaining binary data");
  return prior_min_bytes + count * min_record_bytes;
}

} // namespace

void BinarySerializer::write(const Board &board, const std::filesystem::path &out) const {
  detail::atomic_output(out, [&](const auto &tmp) {
    Output writer(tmp);

    writer.bytes(MAGIC.data(), MAGIC.size());
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
      writer.boolean(cell.macro);
      writer.boolean(cell.location.has_value());
      if (cell.location) {
        writer.real(cell.location->x);
        writer.real(cell.location->y);
        writer.integer(static_cast<std::uint8_t>(cell.location->orientation));
        writer.integer(static_cast<std::uint8_t>(cell.location->status));
        const bool has_dims = cell.location->width && cell.location->height;
        writer.boolean(has_dims);
        if (has_dims) {
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

Board BinarySerializer::read(const std::filesystem::path &in) const {
  Input reader(in);
  std::array<char, MAGIC.size()> magic{};
  reader.bytes(magic.data(), magic.size());
  if (magic != MAGIC)
    throw Error(in.string() + ": invalid binary magic");

  Board board;
  board.name = reader.string();

  const auto cell_count = reader.integer<std::uint64_t>();
  const auto row_count = reader.integer<std::uint64_t>();
  const auto net_count = reader.integer<std::uint64_t>();
  const auto pin_count = reader.integer<std::uint64_t>();

  auto min_payload_bytes = checked_min_bytes(cell_count, reader, "cell", MIN_CELL_BYTES);
  min_payload_bytes = checked_min_bytes(row_count, reader, "row", MIN_ROW_BYTES, min_payload_bytes);
  min_payload_bytes = checked_min_bytes(net_count, reader, "net", MIN_NET_BYTES, min_payload_bytes);
  (void)checked_min_bytes(pin_count, reader, "pin", MIN_PIN_BYTES, min_payload_bytes);

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
    cell.macro = reader.boolean("macro flag");

    if (reader.boolean("placement presence flag")) {
      Location loc;
      loc.x = reader.real();
      loc.y = reader.real();
      loc.orientation = reader.enumeration<Orientation>(7, "orientation");
      loc.status = reader.enumeration<PlacementStatus>(2, "placement status");

      if (reader.boolean("dimensions presence flag")) {
        loc.width = reader.real();
        loc.height = reader.real();
      }

      cell.location = loc;
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
    if (row.symmetry > 7)
      throw Error(in.string() + ": invalid row symmetry");

    const auto subrow_count = reader.integer<std::uint64_t>();
    (void)checked_min_bytes(subrow_count, reader, "subrow", MIN_SUBROW_BYTES);
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
      throw Error(in.string() + ": net pin range is out of bounds");

    net.weights = reader.weights();
    board.nets.push_back(std::move(net));
  }

  for (std::uint64_t i = 0; i < pin_count; ++i) {
    Pin pin;

    pin.cell = reader.integer<std::uint32_t>();
    if (pin.cell >= cell_count)
      throw Error(in.string() + ": pin cell index is out of bounds");

    pin.direction = reader.enumeration<PinDirection>(3, "pin direction");
    pin.offset_x = reader.real();
    pin.offset_y = reader.real();
    board.pins.push_back(pin);
  }

  reader.require_end();

  return board;
}

std::unique_ptr<Serializer> make_serializer(std::string_view format) {
  const auto norm = detail::lower(format);
  if (norm == "binary")
    return std::make_unique<BinarySerializer>();
  throw Error("unsupported serialization format '" + std::string(format) + "'");
}

} // namespace placement
