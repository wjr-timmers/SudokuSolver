CXX      := g++
CXXFLAGS := -std=c++17 -O3 -march=native -flto -funroll-loops -DNDEBUG
LDFLAGS  := -pthread -flto

# Extra flags for v2: disable unused C++ overhead
V2FLAGS  := $(CXXFLAGS) -fno-exceptions -fno-rtti -fomit-frame-pointer

TARGET   := sudoku
SRC      := sudoku.cpp
TARGET2  := sudoku_v2
SRC2     := sudoku_v2.cpp

PGO_GEN_FLAGS := -fprofile-generate
PGO_USE_FLAGS := -fprofile-use -fprofile-correction
TRAIN_DATA    := sudoku.csv
TRAIN_COUNT   := 50000

.PHONY: all clean debug pgo fast v2 v2pgo

all: pgo

# --- v1 targets ---
pgo:
	$(CXX) $(CXXFLAGS) $(PGO_GEN_FLAGS) -o $(TARGET)_pgo_gen $(SRC) $(LDFLAGS)
	./$(TARGET)_pgo_gen --benchmark $(TRAIN_DATA) --num_puzzles $(TRAIN_COUNT) --num_workers $(SLURM_CPUS_PER_TASK)
	$(CXX) $(CXXFLAGS) $(PGO_USE_FLAGS) -o $(TARGET) $(SRC) $(LDFLAGS)

fast: $(SRC)
	$(CXX) $(CXXFLAGS) -o $(TARGET) $< $(LDFLAGS)

debug: $(SRC)
	$(CXX) -std=c++17 -O0 -g -fsanitize=address,undefined -o sudoku_debug $< -pthread

# --- v2 targets (iterative propagation, optimized) ---
v2: $(SRC2)
	$(CXX) $(V2FLAGS) -o $(TARGET2) $< $(LDFLAGS)

v2pgo: $(SRC2)
	$(CXX) $(V2FLAGS) $(PGO_GEN_FLAGS) -o $(TARGET2)_pgo_gen $(SRC2) $(LDFLAGS)
	./$(TARGET2)_pgo_gen --benchmark $(TRAIN_DATA) --num_puzzles $(TRAIN_COUNT) --num_workers $(SLURM_CPUS_PER_TASK)
	$(CXX) $(V2FLAGS) $(PGO_USE_FLAGS) -o $(TARGET2) $(SRC2) $(LDFLAGS)

v2debug: $(SRC2)
	$(CXX) -std=c++17 -O0 -g -fsanitize=address,undefined -o $(TARGET2)_debug $< -pthread

clean:
	rm -f $(TARGET) $(TARGET)_pgo_gen sudoku_debug \
	      $(TARGET2) $(TARGET2)_pgo_gen $(TARGET2)_debug \
	      *.gcda *.gcno