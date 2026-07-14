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
MODEL_SOURCES := src/model.cpp

PARSING_OBJECTS := $(PARSING_SOURCES:src/%.cpp=$(OBJ_DIR)/%.o)
RENDERING_OBJECTS := $(RENDERING_SOURCES:src/%.cpp=$(OBJ_DIR)/%.o)
SERIALIZATION_OBJECTS := $(SERIALIZATION_SOURCES:src/%.cpp=$(OBJ_DIR)/%.o)
MODEL_OBJECTS := $(MODEL_SOURCES:src/%.cpp=$(OBJ_DIR)/%.o)
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
DREAMPLACE_PL_FILES := $(wildcard data/ispd2005-dreamplace/*.gp.pl)
DREAMPLACE_DESIGNS := $(basename $(basename $(notdir $(DREAMPLACE_PL_FILES))))
DREAMPLACE_OUTPUT_TARGETS := $(addprefix dreamplace-output-,$(DREAMPLACE_DESIGNS))
FREE_OUTPUT_AUX_FILES := $(wildcard data/ispd2005free/*/*_allfree.aux)
FREE_OUTPUT_DESIGNS := $(notdir $(patsubst %/,%,$(dir $(FREE_OUTPUT_AUX_FILES))))
FREE_OUTPUT_TARGETS := $(addprefix free-output-,$(FREE_OUTPUT_DESIGNS))
FREE_DREAMPLACE_PL_FILES := $(wildcard data/ispd2005free-dreamplace/*.macro.gp.pl)
FREE_DREAMPLACE_DESIGNS := $(patsubst %.macro.gp.pl,%,$(notdir $(FREE_DREAMPLACE_PL_FILES)))
FREE_DREAMPLACE_OUTPUT_TARGETS := $(addprefix free-dreamplace-output-,$(FREE_DREAMPLACE_DESIGNS))

.PHONY: all test valgrind outputs $(OUTPUT_TARGETS) $(DREAMPLACE_OUTPUT_TARGETS) \
	$(FREE_OUTPUT_TARGETS) $(FREE_DREAMPLACE_OUTPUT_TARGETS) clean clean-outputs format

all: $(PARSE_BIN) $(RENDER_BIN)

$(PARSE_BIN): $(PARSING_OBJECTS) $(SERIALIZATION_OBJECTS) $(MODEL_OBJECTS) $(PARSE_OBJECT) | $(BIN_DIR)
	$(CXX) $(CXXFLAGS) $^ -o $@

$(RENDER_BIN): $(RENDERING_OBJECTS) $(SERIALIZATION_OBJECTS) $(MODEL_OBJECTS) $(RENDER_OBJECT) | $(BIN_DIR)
	$(CXX) $(CXXFLAGS) $^ -o $@

$(TEST_BIN): $(PARSING_OBJECTS) $(RENDERING_OBJECTS) \
		$(SERIALIZATION_OBJECTS) $(MODEL_OBJECTS) $(TEST_OBJECT) | $(BIN_DIR)
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

valgrind:
	+VALGRIND_BUILD_DIR="$(VALGRIND_BUILD_DIR)" \
		VALGRIND_CXXFLAGS="$(VALGRIND_CXXFLAGS)" \
		VALGRIND_FLAGS="$(VALGRIND_FLAGS)" ./test/valgrind_smoke.sh

outputs: all $(OUTPUT_TARGETS) $(DREAMPLACE_OUTPUT_TARGETS) \
	$(FREE_OUTPUT_TARGETS) $(FREE_DREAMPLACE_OUTPUT_TARGETS)

$(OUTPUT_TARGETS): output-%: $(PARSE_BIN) $(RENDER_BIN)
	@mkdir -p "out/ispd2005/$*"
	$(PARSE_BIN) "data/ispd2005/$*/$*.dp.aux" "out/ispd2005/$*/placement.placebin"
	$(RENDER_BIN) "out/ispd2005/$*/placement.placebin" "out/ispd2005/$*/placement.svg"
	$(RENDER_BIN) --output-format utilization-svg "out/ispd2005/$*/placement.placebin" \
		"out/ispd2005/$*/utilization.svg"
	$(RENDER_BIN) --output-format pin-density-svg "out/ispd2005/$*/placement.placebin" \
		"out/ispd2005/$*/pin-density.svg"
	$(RENDER_BIN) --output-format cell-density-svg "out/ispd2005/$*/placement.placebin" \
		"out/ispd2005/$*/cell-density.svg"

$(DREAMPLACE_OUTPUT_TARGETS): dreamplace-output-%: $(PARSE_BIN) $(RENDER_BIN)
	@mkdir -p "out/ispd2005-dreamplace/$*"
	$(PARSE_BIN) --placement-file "data/ispd2005-dreamplace/$*.gp.pl" \
		"data/ispd2005/$*/$*.dp.aux" "out/ispd2005-dreamplace/$*/placement.placebin"
	$(RENDER_BIN) "out/ispd2005-dreamplace/$*/placement.placebin" \
		"out/ispd2005-dreamplace/$*/placement.svg"
	$(RENDER_BIN) --output-format utilization-svg "out/ispd2005-dreamplace/$*/placement.placebin" \
		"out/ispd2005-dreamplace/$*/utilization.svg"
	$(RENDER_BIN) --output-format pin-density-svg "out/ispd2005-dreamplace/$*/placement.placebin" \
		"out/ispd2005-dreamplace/$*/pin-density.svg"
	$(RENDER_BIN) --output-format cell-density-svg "out/ispd2005-dreamplace/$*/placement.placebin" \
		"out/ispd2005-dreamplace/$*/cell-density.svg"

$(FREE_OUTPUT_TARGETS): free-output-%: $(PARSE_BIN) $(RENDER_BIN)
	@mkdir -p "out/ispd2005free/$*"
	$(PARSE_BIN) --placement-file "data/ispd2005free/$*/$*_allfree.pl" \
		"data/ispd2005free/$*/$*.aux" \
		"out/ispd2005free/$*/placement.placebin"
	$(RENDER_BIN) "out/ispd2005free/$*/placement.placebin" \
		"out/ispd2005free/$*/placement.svg"
	$(RENDER_BIN) --output-format utilization-svg "out/ispd2005free/$*/placement.placebin" \
		"out/ispd2005free/$*/utilization.svg"
	$(RENDER_BIN) --output-format pin-density-svg "out/ispd2005free/$*/placement.placebin" \
		"out/ispd2005free/$*/pin-density.svg"
	$(RENDER_BIN) --output-format cell-density-svg "out/ispd2005free/$*/placement.placebin" \
		"out/ispd2005free/$*/cell-density.svg"

$(FREE_DREAMPLACE_OUTPUT_TARGETS): free-dreamplace-output-%: $(PARSE_BIN) $(RENDER_BIN)
	@mkdir -p "out/ispd2005free-dreamplace/$*"
	$(PARSE_BIN) --placement-file "data/ispd2005free-dreamplace/$*.macro.gp.pl" \
		"data/ispd2005free/$*/$*.aux" \
		"out/ispd2005free-dreamplace/$*/placement.placebin"
	$(RENDER_BIN) "out/ispd2005free-dreamplace/$*/placement.placebin" \
		"out/ispd2005free-dreamplace/$*/placement.svg"
	$(RENDER_BIN) --output-format utilization-svg \
		"out/ispd2005free-dreamplace/$*/placement.placebin" \
		"out/ispd2005free-dreamplace/$*/utilization.svg"
	$(RENDER_BIN) --output-format pin-density-svg \
		"out/ispd2005free-dreamplace/$*/placement.placebin" \
		"out/ispd2005free-dreamplace/$*/pin-density.svg"
	$(RENDER_BIN) --output-format cell-density-svg \
		"out/ispd2005free-dreamplace/$*/placement.placebin" \
		"out/ispd2005free-dreamplace/$*/cell-density.svg"

format:
	@command -v $(CLANG_FORMAT) >/dev/null || { \
		echo "$(CLANG_FORMAT) is not installed" >&2; exit 1; }
	$(CLANG_FORMAT) -i $(FORMAT_SOURCES)

clean:
	rm -rf $(BUILD_DIR)

clean-outputs:
	rm -rf out

-include $(PARSING_OBJECTS:.o=.d) $(RENDERING_OBJECTS:.o=.d) \
	$(SERIALIZATION_OBJECTS:.o=.d) $(MODEL_OBJECTS:.o=.d) $(PARSE_OBJECT:.o=.d) \
	$(RENDER_OBJECT:.o=.d) $(TEST_OBJECT:.o=.d)
