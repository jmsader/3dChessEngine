# Fallback build for machines without CMake (GCC or Clang).
#   make            build everything into build-make/
#   make test       build and run the unit tests and a self-play smoke test
#   make clean
CXX      ?= g++
CXXFLAGS ?= -std=c++17 -O2 -Wall -Wextra -Wpedantic -Wshadow -Wconversion -Wno-sign-conversion
CPPFLAGS += -Iinclude

BUILD := build-make
CORE_SRC := $(wildcard src/*.cpp)
CORE_OBJ := $(patsubst src/%.cpp,$(BUILD)/%.o,$(CORE_SRC))
HEADERS  := $(wildcard include/raumschach/*.h)
LIB      := $(BUILD)/libraumschach_core.a
BINS     := $(BUILD)/perft $(BUILD)/selfplay $(BUILD)/unit_tests

all: $(BINS)

$(BUILD):
	mkdir -p $(BUILD)

$(BUILD)/%.o: src/%.cpp $(HEADERS) | $(BUILD)
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) -c $< -o $@

$(LIB): $(CORE_OBJ)
	ar rcs $@ $^

$(BUILD)/perft: tools/perft.cpp $(LIB) $(HEADERS)
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) $< $(LIB) -o $@

$(BUILD)/selfplay: tools/selfplay.cpp $(LIB) $(HEADERS)
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) $< $(LIB) -o $@

$(BUILD)/unit_tests: tests/unit_tests.cpp $(LIB) $(HEADERS)
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) $< $(LIB) -o $@

test: all
	./$(BUILD)/unit_tests
	./$(BUILD)/selfplay --games 20 --seed 1 --check-all

clean:
	rm -rf $(BUILD)

.PHONY: all test clean
