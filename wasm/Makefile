EMCC     := emcc
CXXFLAGS := -O3 -std=c++17 -flto
SOURCE   := solver_wasm.cpp
OUTPUT   := ../solver.js

.PHONY: all clean

all: $(OUTPUT)

$(OUTPUT): $(SOURCE)
	$(EMCC) $(CXXFLAGS) -o $(OUTPUT) $(SOURCE) \
		-s EXPORTED_FUNCTIONS=_solve_puzzle \
		-s EXPORTED_RUNTIME_METHODS=ccall \
		-s MODULARIZE=1 \
		-s EXPORT_NAME=createSolverModule \
		-s ALLOW_MEMORY_GROWTH=0 \
		-s INITIAL_MEMORY=16777216 \
		-s ENVIRONMENT=web \
		--no-entry

clean:
	rm -f $(OUTPUT) ../solver.wasm
