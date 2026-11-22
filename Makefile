CXX      ?= g++
CXXFLAGS ?= -std=c++17 -O2 -Wall -Wextra -Wpedantic -Wshadow
INC      := -Iinclude
BUILD    := build

LIB_SRC := src/models/graph.cpp src/algorithms/toposort.cpp src/algorithms/critical_path.cpp \
           src/algorithms/reach.cpp src/services/parser.cpp src/services/scheduler.cpp
LIB_OBJ := $(LIB_SRC:%.cpp=$(BUILD)/%.o)

.PHONY: all test check demo sanitize clean
all: $(BUILD)/kiln $(BUILD)/kiln_tests

$(BUILD)/%.o: %.cpp
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) $(INC) -MMD -MP -c $< -o $@

$(BUILD)/kiln: $(LIB_OBJ) $(BUILD)/src/main.o
	$(CXX) $(CXXFLAGS) $^ -o $@

$(BUILD)/kiln_tests: $(LIB_OBJ) $(BUILD)/tests/test_kiln.o
	$(CXX) $(CXXFLAGS) $^ -o $@

test: $(BUILD)/kiln_tests
	./$(BUILD)/kiln_tests

# cross-checks the C++ output against an independent Python implementation
check: $(BUILD)/kiln
	python3 tools/verify.py examples/webapp.kiln
	python3 tools/gen_graph.py --nodes 300 --seed 7 > $(BUILD)/g1.kiln && python3 tools/verify.py $(BUILD)/g1.kiln
	python3 tools/gen_graph.py --nodes 800 --layers 25 --seed 8 > $(BUILD)/g2.kiln && python3 tools/verify.py $(BUILD)/g2.kiln
	python3 tools/gen_graph.py --nodes 200 --layers 10 --seed 5 --flat-cost > $(BUILD)/g4.kiln && python3 tools/verify.py $(BUILD)/g4.kiln
	python3 tools/gen_graph.py --nodes 100 --seed 9 --cycle > $(BUILD)/g3.kiln && python3 tools/verify.py $(BUILD)/g3.kiln --expect-cycle

demo: $(BUILD)/kiln
	./$(BUILD)/kiln plan examples/webapp.kiln
	./$(BUILD)/kiln critical examples/webapp.kiln
	./$(BUILD)/kiln simulate examples/webapp.kiln --workers 4
	./$(BUILD)/kiln sweep examples/webapp.kiln
	./$(BUILD)/kiln impact examples/webapp.kiln --changed json.h
	./$(BUILD)/kiln hotspots examples/webapp.kiln --top 5

sanitize:
	$(MAKE) BUILD=build/asan CXXFLAGS="-std=c++17 -O1 -g -fsanitize=address,undefined -fno-omit-frame-pointer -Wall -Wextra" test

clean:
	rm -rf build

-include $(LIB_OBJ:.o=.d)
