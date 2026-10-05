# shot — build and test.
#
#   make          build build/shot
#   make test     build, then run the unit and end-to-end tests
#   make clean    remove build artefacts
#
# Requires hyprtoolkit and hyprutils (pkg-config), plus grim/slurp at runtime.

CXX        ?= g++
CXXFLAGS   ?= -O2 -Wall -Wextra
CPPFLAGS   ?=
LDFLAGS    ?=

# hyprtoolkit requires C++23. This is appended rather than folded into the
# CXXFLAGS default above, because a CXXFLAGS from the environment (makepkg
# supplies one) would otherwise silently replace it and the build would fail on
# std::expected.
CXXFLAGS   += -std=c++23

PKGS       := hyprtoolkit hyprutils
PKG_CFLAGS := $(shell pkg-config --cflags $(PKGS))
PKG_LIBS   := $(shell pkg-config --libs $(PKGS))

BUILD_DIR  := build
OBJ_DIR    := $(BUILD_DIR)/obj
TEST_DIR   := $(BUILD_DIR)/tests
BIN        := $(BUILD_DIR)/shot

SOURCES    := $(sort $(wildcard src/*.cpp))
OBJECTS    := $(patsubst src/%.cpp,$(OBJ_DIR)/%.o,$(SOURCES))
DEPS       := $(OBJECTS:.o=.d)

.PHONY: all test clean

all: $(BIN)

$(BIN): $(OBJECTS)
	$(CXX) $(CXXFLAGS) $(LDFLAGS) -o $@ $^ $(PKG_LIBS)

$(OBJ_DIR)/%.o: src/%.cpp
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) $(CPPFLAGS) $(PKG_CFLAGS) -MMD -MP -c $< -o $@

# ---------------------------------------------------------------------------
# Tests
#
# The unit tests cover the pure logic (JSON parsing, argument parsing and the
# capture bar's selection mapping) and deliberately link no GUI libraries, so
# they run anywhere.
# ---------------------------------------------------------------------------

TEST_BINS := $(TEST_DIR)/json_test $(TEST_DIR)/args_test $(TEST_DIR)/gui_state_test

$(TEST_DIR)/json_test: tests/json_test.cpp src/json.cpp
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) $(CPPFLAGS) -Isrc -o $@ $^

$(TEST_DIR)/args_test: tests/args_test.cpp src/args.cpp
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) $(CPPFLAGS) -Isrc -o $@ $^

$(TEST_DIR)/gui_state_test: tests/gui_state_test.cpp src/gui_state.cpp src/args.cpp
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) $(CPPFLAGS) -Isrc -o $@ $^

test: $(BIN) $(TEST_BINS)
	@for t in $(TEST_BINS); do ./$$t || exit 1; done
	@SHOT_NO_NOTIFY=1 tests/cli_test.sh $(BIN)

clean:
	rm -rf $(BUILD_DIR)

-include $(DEPS)
