CXX := clang++
CLANG_FORMAT ?= clang-format
CPPFLAGS := -Iinclude
CXXFLAGS ?= -std=c++23 -O3 -DNDEBUG -Wall -Wextra -Wpedantic -Wconversion -Wshadow
DEPFLAGS := -MMD -MP

BUILD_DIR := build
OBJ_DIR := $(BUILD_DIR)/obj
BIN_DIR := $(BUILD_DIR)/bin

PARSING_SOURCES := src/parsing/bookshelf.cpp src/parsing/lefdef.cpp
RENDERING_SOURCES := src/rendering/svg.cpp
SERIALIZATION_SOURCES := src/serialization/binary.cpp
MODEL_SOURCES := src/model.cpp

PARSING_OBJECTS := $(PARSING_SOURCES:src/%.cpp=$(OBJ_DIR)/%.o)
RENDERING_OBJECTS := $(RENDERING_SOURCES:src/%.cpp=$(OBJ_DIR)/%.o)
SERIALIZATION_OBJECTS := $(SERIALIZATION_SOURCES:src/%.cpp=$(OBJ_DIR)/%.o)
MODEL_OBJECTS := $(MODEL_SOURCES:src/%.cpp=$(OBJ_DIR)/%.o)
PARSE_OBJECT := $(OBJ_DIR)/apps/parse_main.o
RENDER_OBJECT := $(OBJ_DIR)/apps/render_main.o
TEST_SOURCES := $(wildcard test/*.cpp test/*/*.cpp)
TEST_OBJECTS := $(TEST_SOURCES:test/%.cpp=$(OBJ_DIR)/test/%.o)
BENCH_SOURCES := $(wildcard bench/*.cpp)
BENCH_OBJECTS := $(BENCH_SOURCES:bench/%.cpp=$(OBJ_DIR)/bench/%.o)

FORMAT_SOURCES := $(wildcard include/placement/*.hpp \
	include/placement/*/*.hpp \
	src/*.cpp src/*.hpp \
	src/*/*.cpp src/*/*.hpp \
	test/*.cpp test/*.hpp \
	test/*/*.cpp test/*/*.hpp \
	bench/*.cpp \
	fuzz/*.cpp fuzz/*.hpp \
	fuzz/*/*.cpp fuzz/*/*.hpp)

PARSE_BIN := $(BIN_DIR)/placement_parse
RENDER_BIN := $(BIN_DIR)/placement_render
TEST_BIN := $(BIN_DIR)/placement_tests
BENCH_BIN := $(BIN_DIR)/placement_benchmark
BENCH_RUNS ?= 5
BENCH_DIR ?= $(BUILD_DIR)/benchmark
BENCH_BOOKSHELF_DESIGN ?= bigblue4
BENCH_LEFDEF_DESIGN ?= mgc_superblue12
BENCH_QUICK_RUNS ?= 1
BENCH_QUICK_DIR ?= $(BENCH_DIR)/quick
BENCH_QUICK_BOOKSHELF_DESIGN ?= adaptec1
BENCH_QUICK_LEFDEF_DESIGN ?= mgc_fft_b
FUZZ_BUILD_DIR := $(BUILD_DIR)/fuzz
FUZZ_OBJ_DIR := $(FUZZ_BUILD_DIR)/obj
FUZZ_TARGETS := bookshelf lefdef binary model svg
FUZZ_BINS := $(addprefix $(FUZZ_BUILD_DIR)/placement_fuzz_,$(FUZZ_TARGETS))
FUZZ_SOURCES := $(PARSING_SOURCES) $(RENDERING_SOURCES) \
	$(SERIALIZATION_SOURCES) $(MODEL_SOURCES) $(wildcard fuzz/*.cpp fuzz/*/*.cpp)
FUZZ_OBJECTS := $(patsubst %.cpp,$(FUZZ_OBJ_DIR)/%.o,$(FUZZ_SOURCES))
FUZZ_COMMON_OBJECTS := $(FUZZ_OBJ_DIR)/fuzz/fuzz_main.o \
	$(FUZZ_OBJ_DIR)/fuzz/support.o
FUZZ_CXXFLAGS ?= -O1 -g -fno-omit-frame-pointer \
	-fsanitize=fuzzer,address,undefined
FUZZ_CRASH_DIR := fuzz/crashes
FUZZ_SECONDS ?= 60
FUZZ_MAX_LEN ?= 4096
FUZZ_TIMEOUT ?= 5
FUZZ_RSS_LIMIT_MB ?= 1024

ISPD2005_OUTPUT_AUX_FILES := $(wildcard data/ispd2005/*/*.dp.aux)
ISPD2005_OUTPUT_DESIGNS := $(notdir $(patsubst %/,%,$(dir \
	$(ISPD2005_OUTPUT_AUX_FILES))))
ISPD2005_OUTPUT_TARGETS := $(addprefix ispd2005-output-, \
	$(ISPD2005_OUTPUT_DESIGNS))
ISPD2005_DREAMPLACE_PL_FILES := $(wildcard data/ispd2005-dreamplace/*.gp.pl)
ISPD2005_DREAMPLACE_DESIGNS := $(basename $(basename $(notdir \
	$(ISPD2005_DREAMPLACE_PL_FILES))))
ISPD2005_DREAMPLACE_OUTPUT_TARGETS := $(addprefix ispd2005-dreamplace-output-, \
	$(ISPD2005_DREAMPLACE_DESIGNS))
ISPD2005FREE_OUTPUT_AUX_FILES := $(wildcard data/ispd2005free/*/*_allfree.aux)
ISPD2005FREE_OUTPUT_DESIGNS := $(notdir $(patsubst %/,%,$(dir \
	$(ISPD2005FREE_OUTPUT_AUX_FILES))))
ISPD2005FREE_OUTPUT_TARGETS := $(addprefix ispd2005free-output-, \
	$(ISPD2005FREE_OUTPUT_DESIGNS))
ISPD2005FREE_DREAMPLACE_PL_FILES := $(wildcard \
	data/ispd2005free-dreamplace/*_allfree.gp.pl)
ISPD2005FREE_DREAMPLACE_DESIGNS := $(patsubst %_allfree.gp.pl,%,$(notdir \
	$(ISPD2005FREE_DREAMPLACE_PL_FILES)))
ISPD2005FREE_DREAMPLACE_OUTPUT_TARGETS := $(addprefix \
	ispd2005free-dreamplace-output-,$(ISPD2005FREE_DREAMPLACE_DESIGNS))
ISPD2015_OUTPUT_DEF_FILES := $(wildcard \
	data/ispd2015/*/after_legalized.ntup.fix.def)
ISPD2015_OUTPUT_DESIGNS := $(notdir $(patsubst %/,%,$(dir \
	$(ISPD2015_OUTPUT_DEF_FILES))))
ISPD2015_OUTPUT_TARGETS := $(addprefix ispd2015-output-, \
	$(ISPD2015_OUTPUT_DESIGNS))
ISPD2015_DREAMPLACE_DEF_FILES := $(wildcard data/ispd2015-dreamplace/*.gp.def)
ISPD2015_DREAMPLACE_DESIGNS := $(patsubst %.gp.def,%,$(notdir \
	$(ISPD2015_DREAMPLACE_DEF_FILES)))
ISPD2015_DREAMPLACE_OUTPUT_TARGETS := $(addprefix ispd2015-dreamplace-output-, \
	$(ISPD2015_DREAMPLACE_DESIGNS))

.PHONY: all test benchmark benchmark-run benchmark-run-quick fuzz fuzz-run valgrind \
	check-data outputs $(ISPD2005_OUTPUT_TARGETS) \
	$(ISPD2005_DREAMPLACE_OUTPUT_TARGETS) $(ISPD2005FREE_OUTPUT_TARGETS) \
	$(ISPD2005FREE_DREAMPLACE_OUTPUT_TARGETS) $(ISPD2015_OUTPUT_TARGETS) \
	$(ISPD2015_DREAMPLACE_OUTPUT_TARGETS) \
	clean clean-outputs format

all: $(PARSE_BIN) $(RENDER_BIN)

$(PARSE_BIN): $(PARSING_OBJECTS) $(SERIALIZATION_OBJECTS) $(MODEL_OBJECTS) $(PARSE_OBJECT) | $(BIN_DIR)
	$(CXX) $(CXXFLAGS) $^ -o $@

$(RENDER_BIN): $(RENDERING_OBJECTS) $(SERIALIZATION_OBJECTS) $(MODEL_OBJECTS) $(RENDER_OBJECT) | $(BIN_DIR)
	$(CXX) $(CXXFLAGS) $^ -o $@

$(TEST_BIN): $(PARSING_OBJECTS) $(RENDERING_OBJECTS) \
		$(SERIALIZATION_OBJECTS) $(MODEL_OBJECTS) $(TEST_OBJECTS) | $(BIN_DIR)
	$(CXX) $(CXXFLAGS) $^ -o $@

$(BENCH_BIN): $(PARSING_OBJECTS) $(RENDERING_OBJECTS) \
		$(SERIALIZATION_OBJECTS) $(MODEL_OBJECTS) $(BENCH_OBJECTS) | $(BIN_DIR)
	$(CXX) $(CXXFLAGS) $^ -o $@

$(OBJ_DIR)/%.o: src/%.cpp
	@mkdir -p $(@D)
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) $(DEPFLAGS) -c $< -o $@

$(OBJ_DIR)/test/%.o: test/%.cpp
	@mkdir -p $(@D)
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) $(DEPFLAGS) -c $< -o $@

$(OBJ_DIR)/bench/%.o: bench/%.cpp
	@mkdir -p $(@D)
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) $(DEPFLAGS) -c $< -o $@

$(BIN_DIR):
	mkdir -p $@

test: $(TEST_BIN)
	$(TEST_BIN)

benchmark: $(BENCH_BIN)

benchmark-run: $(BENCH_BIN)
	BENCH_RUNS="$(BENCH_RUNS)" \
		BENCH_DIR="$(BENCH_DIR)" \
		BENCH_BOOKSHELF_DESIGN="$(BENCH_BOOKSHELF_DESIGN)" \
		BENCH_LEFDEF_DESIGN="$(BENCH_LEFDEF_DESIGN)" \
		BENCH_CXX="$(CXX)" BENCH_CXXFLAGS="$(CXXFLAGS)" \
		./bench/run.sh "$(BENCH_BIN)"

benchmark-run-quick: $(BENCH_BIN)
	BENCH_RUNS="$(BENCH_QUICK_RUNS)" \
		BENCH_DIR="$(BENCH_QUICK_DIR)" \
		BENCH_BOOKSHELF_DESIGN="$(BENCH_QUICK_BOOKSHELF_DESIGN)" \
		BENCH_LEFDEF_DESIGN="$(BENCH_QUICK_LEFDEF_DESIGN)" \
		BENCH_CXX="$(CXX)" BENCH_CXXFLAGS="$(CXXFLAGS)" \
		./bench/run.sh "$(BENCH_BIN)"

$(FUZZ_OBJ_DIR)/%.o: %.cpp
	@mkdir -p $(@D)
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) $(FUZZ_CXXFLAGS) $(DEPFLAGS) -c $< -o $@

$(FUZZ_BUILD_DIR)/placement_fuzz_bookshelf: $(FUZZ_COMMON_OBJECTS) \
		$(FUZZ_OBJ_DIR)/fuzz/parsing/bookshelf.o \
		$(FUZZ_OBJ_DIR)/src/parsing/bookshelf.o | $(FUZZ_BUILD_DIR)
	$(CXX) $(CXXFLAGS) $(FUZZ_CXXFLAGS) $^ -o $@

$(FUZZ_BUILD_DIR)/placement_fuzz_lefdef: $(FUZZ_COMMON_OBJECTS) \
		$(FUZZ_OBJ_DIR)/fuzz/parsing/lefdef.o \
		$(FUZZ_OBJ_DIR)/src/parsing/lefdef.o \
		$(FUZZ_OBJ_DIR)/src/model.o | $(FUZZ_BUILD_DIR)
	$(CXX) $(CXXFLAGS) $(FUZZ_CXXFLAGS) $^ -o $@

$(FUZZ_BUILD_DIR)/placement_fuzz_binary: $(FUZZ_COMMON_OBJECTS) \
		$(FUZZ_OBJ_DIR)/fuzz/serialization/binary.o \
		$(FUZZ_OBJ_DIR)/src/serialization/binary.o | $(FUZZ_BUILD_DIR)
	$(CXX) $(CXXFLAGS) $(FUZZ_CXXFLAGS) $^ -o $@

$(FUZZ_BUILD_DIR)/placement_fuzz_model: $(FUZZ_COMMON_OBJECTS) \
		$(FUZZ_OBJ_DIR)/fuzz/model.o $(FUZZ_OBJ_DIR)/src/model.o | $(FUZZ_BUILD_DIR)
	$(CXX) $(CXXFLAGS) $(FUZZ_CXXFLAGS) $^ -o $@

$(FUZZ_BUILD_DIR)/placement_fuzz_svg: $(FUZZ_COMMON_OBJECTS) \
		$(FUZZ_OBJ_DIR)/fuzz/rendering/svg.o \
		$(FUZZ_OBJ_DIR)/src/rendering/svg.o $(FUZZ_OBJ_DIR)/src/model.o | $(FUZZ_BUILD_DIR)
	$(CXX) $(CXXFLAGS) $(FUZZ_CXXFLAGS) $^ -o $@

$(FUZZ_BUILD_DIR):
	mkdir -p $@

fuzz: $(FUZZ_BINS)

fuzz-run: $(FUZZ_BINS)
	@failed=""; passed=0; total=0; \
	for target in $(FUZZ_TARGETS); do \
		total=$$((total + 1)); \
		corpus="fuzz/corpora/$$target"; crashes="$(FUZZ_CRASH_DIR)"; \
		mkdir -p "$$corpus" "$$crashes"; \
		echo "Fuzzing $$target"; \
		if "$(FUZZ_BUILD_DIR)/placement_fuzz_$$target" \
			-max_total_time=$(FUZZ_SECONDS) -max_len=$(FUZZ_MAX_LEN) \
			-timeout=$(FUZZ_TIMEOUT) -rss_limit_mb=$(FUZZ_RSS_LIMIT_MB) \
			-print_funcs=0 \
			-artifact_prefix="$$crashes/" \
			"$$corpus" fuzz/corpus; then \
			passed=$$((passed + 1)); \
			echo "[PASS] $$target"; \
		else \
			status=$$?; failed="$$failed $$target"; \
			echo "[FAIL] $$target (exit status $$status)"; \
		fi; \
	done; \
	echo; echo "=== Fuzz run summary ==="; \
	if [ -n "$$failed" ]; then \
		echo "PROBLEMS FOUND: $$passed/$$total targets passed."; \
		echo "Failed targets:$$failed"; \
		exit 1; \
	fi; \
	echo "NO PROBLEMS FOUND: all $$total targets passed."

valgrind:
	+VALGRIND_BUILD_DIR="$(VALGRIND_BUILD_DIR)" \
		VALGRIND_CXXFLAGS="$(VALGRIND_CXXFLAGS)" \
		VALGRIND_FLAGS="$(VALGRIND_FLAGS)" ./test/valgrind_smoke.sh

check-data:
	@missing=""; \
	if [ -z "$(ISPD2005_OUTPUT_DESIGNS)" ]; then \
		missing="data/ispd2005/*/*.dp.aux"; \
	fi; \
	if [ -z "$$missing" ]; then \
		for design in $(ISPD2005FREE_OUTPUT_DESIGNS); do \
			for file in "$$design.aux" "$${design}_allfree.aux" \
				"$${design}_allfree.pl"; do \
				path="data/ispd2005free/$$design/$$file"; \
				if [ ! -f "$$path" ]; then missing="$$path"; break 2; fi; \
			done; \
		done; \
	fi; \
	if [ -z "$$missing" ] && [ -z "$(ISPD2005FREE_OUTPUT_DESIGNS)" ]; then \
		missing="data/ispd2005free/*/*_allfree.aux"; \
	fi; \
	if [ -z "$$missing" ]; then \
		for design in $(ISPD2015_OUTPUT_DESIGNS); do \
			for file in tech.lef cells.lef after_legalized.ntup.fix.def; do \
				path="data/ispd2015/$$design/$$file"; \
				if [ ! -f "$$path" ]; then missing="$$path"; break 2; fi; \
			done; \
		done; \
	fi; \
	if [ -z "$$missing" ] && [ -z "$(ISPD2015_OUTPUT_DESIGNS)" ]; then \
		missing="data/ispd2015/*/after_legalized.ntup.fix.def"; \
	fi; \
	if [ -n "$$missing" ]; then \
		echo "Benchmark data is missing or incomplete: $$missing" >&2; \
		echo "Run ./scripts/prepare_data.sh and retry make outputs." >&2; \
		exit 1; \
	fi

outputs: check-data all $(ISPD2005_OUTPUT_TARGETS) \
	$(ISPD2005_DREAMPLACE_OUTPUT_TARGETS) $(ISPD2005FREE_OUTPUT_TARGETS) \
	$(ISPD2005FREE_DREAMPLACE_OUTPUT_TARGETS) \
	$(ISPD2015_OUTPUT_TARGETS) $(ISPD2015_DREAMPLACE_OUTPUT_TARGETS)

$(ISPD2005_OUTPUT_TARGETS) $(ISPD2005_DREAMPLACE_OUTPUT_TARGETS) \
	$(ISPD2005FREE_OUTPUT_TARGETS) $(ISPD2005FREE_DREAMPLACE_OUTPUT_TARGETS) \
	$(ISPD2015_OUTPUT_TARGETS) \
	$(ISPD2015_DREAMPLACE_OUTPUT_TARGETS): | check-data

define RENDER_VIEWS
	$(RENDER_BIN) "$(1)/placement.placebin" "$(1)/placement.svg"
	$(RENDER_BIN) --output-format utilization-svg "$(1)/placement.placebin" \
		"$(1)/utilization.svg"
	$(RENDER_BIN) --output-format pin-density-svg "$(1)/placement.placebin" \
		"$(1)/pin-density.svg"
	$(RENDER_BIN) --output-format cell-density-svg "$(1)/placement.placebin" \
		"$(1)/cell-density.svg"
endef

$(ISPD2005_OUTPUT_TARGETS): ispd2005-output-%: $(PARSE_BIN) $(RENDER_BIN)
	@mkdir -p "out/ispd2005/$*"
	$(PARSE_BIN) "data/ispd2005/$*/$*.dp.aux" "out/ispd2005/$*/placement.placebin"
	$(call RENDER_VIEWS,out/ispd2005/$*)

$(ISPD2005_DREAMPLACE_OUTPUT_TARGETS): ispd2005-dreamplace-output-%: \
		$(PARSE_BIN) $(RENDER_BIN)
	@mkdir -p "out/ispd2005-dreamplace/$*"
	$(PARSE_BIN) --placement-file "data/ispd2005-dreamplace/$*.gp.pl" \
		"data/ispd2005/$*/$*.dp.aux" "out/ispd2005-dreamplace/$*/placement.placebin"
	$(call RENDER_VIEWS,out/ispd2005-dreamplace/$*)

$(ISPD2005FREE_OUTPUT_TARGETS): ispd2005free-output-%: \
		$(PARSE_BIN) $(RENDER_BIN)
	@mkdir -p "out/ispd2005free/$*"
	$(PARSE_BIN) --placement-file "data/ispd2005free/$*/$*_allfree.pl" \
		"data/ispd2005free/$*/$*.aux" \
		"out/ispd2005free/$*/placement.placebin"
	$(call RENDER_VIEWS,out/ispd2005free/$*)

$(ISPD2005FREE_DREAMPLACE_OUTPUT_TARGETS): ispd2005free-dreamplace-output-%: \
		$(PARSE_BIN) $(RENDER_BIN)
	@mkdir -p "out/ispd2005free-dreamplace/$*"
	$(PARSE_BIN) --placement-file "data/ispd2005free-dreamplace/$*_allfree.gp.pl" \
		"data/ispd2005free/$*/$*.aux" \
		"out/ispd2005free-dreamplace/$*/placement.placebin"
	$(call RENDER_VIEWS,out/ispd2005free-dreamplace/$*)

$(ISPD2015_OUTPUT_TARGETS): ispd2015-output-%: $(PARSE_BIN) $(RENDER_BIN)
	@mkdir -p "out/ispd2015/$*"
	$(PARSE_BIN) --input-format lefdef \
		--lef-file "data/ispd2015/$*/tech.lef" \
		--lef-file "data/ispd2015/$*/cells.lef" \
		"data/ispd2015/$*/after_legalized.ntup.fix.def" \
		"out/ispd2015/$*/placement.placebin"
	$(call RENDER_VIEWS,out/ispd2015/$*)

$(ISPD2015_DREAMPLACE_OUTPUT_TARGETS): ispd2015-dreamplace-output-%: \
		$(PARSE_BIN) $(RENDER_BIN)
	@mkdir -p "out/ispd2015-dreamplace/$*"
	$(PARSE_BIN) --input-format lefdef \
		--lef-file "data/ispd2015/$*/tech.lef" \
		--lef-file "data/ispd2015/$*/cells.lef" \
		"data/ispd2015-dreamplace/$*.gp.def" \
		"out/ispd2015-dreamplace/$*/placement.placebin"
	$(call RENDER_VIEWS,out/ispd2015-dreamplace/$*)

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
	$(RENDER_OBJECT:.o=.d) $(TEST_OBJECTS:.o=.d) $(BENCH_OBJECTS:.o=.d) \
	$(FUZZ_OBJECTS:.o=.d)
