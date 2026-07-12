CXX := clang++
CLANG_FORMAT ?= clang-format
CPPFLAGS := -Iinclude
CXXFLAGS ?= -std=c++23 -O2 -Wall -Wextra -Wpedantic -Wconversion -Wshadow
DEPFLAGS := -MMD -MP

BUILD_DIR := build
OBJ_DIR := $(BUILD_DIR)/obj
BIN_DIR := $(BUILD_DIR)/bin

PARSING_SOURCES := src/parsing/bookshelf.cpp
RENDERING_SOURCES := src/rendering/svg.cpp
SERIALIZATION_SOURCES := src/serialization/binary.cpp

PARSING_OBJECTS := $(PARSING_SOURCES:src/%.cpp=$(OBJ_DIR)/%.o)
RENDERING_OBJECTS := $(RENDERING_SOURCES:src/%.cpp=$(OBJ_DIR)/%.o)
SERIALIZATION_OBJECTS := $(SERIALIZATION_SOURCES:src/%.cpp=$(OBJ_DIR)/%.o)
PARSE_OBJECT := $(OBJ_DIR)/apps/parse_main.o
RENDER_OBJECT := $(OBJ_DIR)/apps/render_main.o
TEST_OBJECT := $(OBJ_DIR)/test_main.o

FORMAT_SOURCES := $(wildcard include/placement/*.hpp \
	include/placement/*/*.hpp \
	src/*.cpp \
	src/*/*.cpp \
	test/*.cpp)

PARSE_BIN := $(BIN_DIR)/placement_parse
RENDER_BIN := $(BIN_DIR)/placement_render
TEST_BIN := $(BIN_DIR)/placement_tests

OUTPUT_AUX_FILES := $(wildcard data/ispd2005/*/*.dp.aux)
OUTPUT_DESIGNS := $(notdir $(patsubst %/,%,$(dir $(OUTPUT_AUX_FILES))))
OUTPUT_TARGETS := $(addprefix output-,$(OUTPUT_DESIGNS))

.PHONY: all test outputs $(OUTPUT_TARGETS) clean clean-outputs format

all: $(PARSE_BIN) $(RENDER_BIN)

$(PARSE_BIN): $(PARSING_OBJECTS) $(SERIALIZATION_OBJECTS) $(PARSE_OBJECT) | $(BIN_DIR)
	$(CXX) $(CXXFLAGS) $^ -o $@

$(RENDER_BIN): $(RENDERING_OBJECTS) $(SERIALIZATION_OBJECTS) $(RENDER_OBJECT) | $(BIN_DIR)
	$(CXX) $(CXXFLAGS) $^ -o $@

$(TEST_BIN): $(PARSING_OBJECTS) $(RENDERING_OBJECTS) \
		$(SERIALIZATION_OBJECTS) $(TEST_OBJECT) | $(BIN_DIR)
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

outputs: all $(OUTPUT_TARGETS)

$(OUTPUT_TARGETS): output-%: $(PARSE_BIN) $(RENDER_BIN)
	@mkdir -p out/parsed out/svg
	$(PARSE_BIN) "data/ispd2005/$*/$*.dp.aux" "out/parsed/$*.placebin"
	$(RENDER_BIN) "out/parsed/$*.placebin" "out/svg/$*.svg"

format:
	@command -v $(CLANG_FORMAT) >/dev/null || { \
		echo "$(CLANG_FORMAT) is not installed" >&2; exit 1; }
	$(CLANG_FORMAT) -i $(FORMAT_SOURCES)

clean:
	rm -rf $(BUILD_DIR)

clean-outputs:
	rm -rf out

-include $(PARSING_OBJECTS:.o=.d) $(RENDERING_OBJECTS:.o=.d) \
	$(SERIALIZATION_OBJECTS:.o=.d) $(PARSE_OBJECT:.o=.d) \
	$(RENDER_OBJECT:.o=.d) $(TEST_OBJECT:.o=.d)
