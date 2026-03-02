CXX      := g++
CXXFLAGS := -std=c++17 -O3 -march=native -flto -DNDEBUG
LDFLAGS  := -pthread -flto
TARGET   := sudoku
SRC      := sudoku.cpp

.PHONY: all clean debug

all: $(TARGET)

$(TARGET): $(SRC)
	$(CXX) $(CXXFLAGS) -o $@ $< $(LDFLAGS)

debug: $(SRC)
	$(CXX) -std=c++17 -O0 -g -fsanitize=address,undefined -o sudoku_debug $< -pthread

clean:
	rm -f $(TARGET) sudoku_debug

# Usage (from cpp/ directory):
#   make
#   ./sudoku <81-char-puzzle>
#   ./sudoku --benchmark ../sudoku.csv --num_puzzles 1000
