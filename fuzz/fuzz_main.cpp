#include "support.hpp"

#include <cstddef>
#include <cstdint>

extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t *data, std::size_t size) {
  const placement::fuzz::Input input(data, size);
  placement::fuzz::fuzz_one(input);
  return 0;
}
