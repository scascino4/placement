CXX := clang++
CLANG_FORMAT ?= clang-format
CPPFLAGS := -Iinclude
CXXFLAGS ?= -std=c++23 -O2 -Wall -Wextra -Wpedantic -Wconversion -Wshadow
DEPFLAGS := -MMD -MP

BUILD_DIR := build
OBJ_DIR := $(BUILD_DIR)/obj
BIN_DIR := $(BUILD_DIR)/bin

CORE_SOURCES := src/bookshelf_parser.cpp src/binary.cpp src/svg_writer.cpp
CORE_OBJECTS := $(CORE_SOURCES:src/%.cpp=$(OBJ_DIR)/%.o)
PARSE_OBJECT := $(OBJ_DIR)/parse_main.o
RENDER_OBJECT := $(OBJ_DIR)/render_main.o
TEST_OBJECT := $(OBJ_DIR)/test_main.o

PARSE_BIN := $(BIN_DIR)/placement_parse
RENDER_BIN := $(BIN_DIR)/placement_render
TEST_BIN := $(BIN_DIR)/placement_tests

.PHONY: all test outputs clean clean-outputs format

all: $(PARSE_BIN) $(RENDER_BIN)

$(PARSE_BIN): $(CORE_OBJECTS) $(PARSE_OBJECT) | $(BIN_DIR)
	$(CXX) $(CXXFLAGS) $^ -o $@

$(RENDER_BIN): $(CORE_OBJECTS) $(RENDER_OBJECT) | $(BIN_DIR)
	$(CXX) $(CXXFLAGS) $^ -o $@

$(TEST_BIN): $(CORE_OBJECTS) $(TEST_OBJECT) | $(BIN_DIR)
	$(CXX) $(CXXFLAGS) $^ -o $@

$(OBJ_DIR)/%.o: src/%.cpp
	@mkdir -p $(@D)
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) $(DEPFLAGS) -c $< -o $@

$(OBJ_DIR)/test_main.o: test/test_main.cpp
	@mkdir -p $(@D)
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) $(DEPFLAGS) -c $< -o $@

$(BIN_DIR):
	mkdir -p $@

test: $(TEST_BIN)
	$(TEST_BIN)

outputs: all
	@mkdir -p out/parsed out/svg
	@set -eu; \
	for aux in data/ispd2005/*/*.dp.aux; do \
		design=$$(basename "$$(dirname "$$aux")"); \
		$(PARSE_BIN) "$$aux" "out/parsed/$$design.placebin"; \
		$(RENDER_BIN) "out/parsed/$$design.placebin" "out/svg/$$design.svg"; \
	done

format:
	@command -v $(CLANG_FORMAT) >/dev/null || { \
		echo "$(CLANG_FORMAT) is not installed" >&2; exit 1; }
	$(CLANG_FORMAT) -i include/placement/*.hpp src/*.cpp test/*.cpp

clean:
	rm -rf $(BUILD_DIR)

clean-outputs:
	rm -rf out

-include $(CORE_OBJECTS:.o=.d) $(PARSE_OBJECT:.o=.d) $(RENDER_OBJECT:.o=.d) \
	$(TEST_OBJECT:.o=.d)
