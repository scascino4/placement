#pragma once

#include "support.hpp"

namespace placement::test {

[[nodiscard]] Tests parsing_tests();
[[nodiscard]] Tests model_tests();
[[nodiscard]] Tests serialization_tests();
[[nodiscard]] Tests rendering_tests();

} // namespace placement::test
