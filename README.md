# Sudoku-solver
Code to solve a sudoku for fun.

![Image](https://media.istockphoto.com/id/172406544/nl/foto/sudoku-puzzle.jpg?s=2048x2048&w=is&k=20&c=oLlnSR9R04iS1OXtooYO73bE_sj7pkrpANzD9APwz6U=)

## Project structure
```
ProjectSudoku/
├── cpp/              # C++ solver (CLI, high-performance)
│   ├── sudoku.cpp
│   ├── Makefile
│   └── benchmark.sh
├── wasm/             # WebAssembly build (for browser)
│   ├── solver_wasm.cpp
│   └── Makefile
├── python/           # Python solver (original)
│   ├── sudoku.py
│   ├── templates.py
│   ├── benchmark.py
│   └── benchmark.sh
├── app.js            # Web frontend
├── index.html
├── style.css
├── solver.js         # Emscripten glue (built from wasm/)
├── solver.wasm       # Compiled solver (built from wasm/)
├── sudoku.csv        # 9M puzzle dataset (not in git)
└── README.md
```

## Python solver

### Solve a single puzzle
Run the solver directly with a puzzle from `templates.py`:
```bash
cd python
python sudoku.py
```
Edit the `test_grid` variable in the `__main__` block to change which puzzle to solve.

### Run benchmark
Benchmark against puzzles from `sudoku.csv` (dataset from [Kaggle](https://www.kaggle.com/datasets/rohanrao/sudoku)):
```bash
cd python

# Solve 1000 puzzles with auto-detected workers
python benchmark.py --num_puzzles 1000

# Customize chunk size and worker count
python benchmark.py --num_puzzles 10000 --chunk_size 100 --num_workers 8
```

**Arguments:**
- `--num_puzzles` - Number of puzzles to solve (default: 1000, max: 9 million)
- `--chunk_size` - Puzzles per parallel chunk (default: 50)
- `--num_workers` - Number of parallel workers (default: auto-detect CPU cores)

### HPC (Slurm)
```bash
cd python
sbatch benchmark.sh
```

## C++ solver (high-performance)

The C++ solver uses bitmask-based constraint propagation and is significantly faster than the Python version.

### Build
```bash
cd cpp
make          # optimized build (-O3, -march=native, -flto)
make debug    # debug build with sanitizers
make clean    # remove binaries
```

### Solve a single puzzle
Pass an 81-character string where `0` represents empty cells:
```bash
./sudoku 070000938000005764350700291005400017407500023060270845500973486849651372736842159

# With verbose output
./sudoku 070000938000005764350700291005400017407500023060270845500973486849651372736842159 --verbose
```

### Run benchmark
Benchmark against the CSV dataset:
```bash
# Solve 1000 puzzles (single-threaded)
./sudoku --benchmark ../sudoku.csv --num_puzzles 1000

# Multi-threaded with 8 workers
./sudoku --benchmark ../sudoku.csv --num_puzzles 10000 --num_workers 8
```

**Arguments:**
- `--benchmark <csv_file>` - Path to puzzle CSV file
- `--num_puzzles N` - Number of puzzles to solve (default: 1000)
- `--num_workers N` - Number of threads (default: 1)

### HPC (Slurm)
```bash
cd cpp
sbatch benchmark.sh
```

## Web app (WebAssembly)

The web app runs the C++ solver compiled to WebAssembly — no server needed, works entirely in the browser via GitHub Pages.

### Prerequisites
Install the [Emscripten SDK](https://emscripten.org/docs/getting_started/downloads.html):
```bash
git clone https://github.com/emscripten-core/emsdk.git
cd emsdk
./emsdk install latest
./emsdk activate latest
source ./emsdk_env.sh
```

### Build
```bash
cd wasm
make          # produces solver.js + solver.wasm in project root
make clean    # remove built files
```

### Run locally
Serve the project root with any HTTP server (Wasm requires it):
```bash
python3 -m http.server 8000
# Open http://localhost:8000
```

### Deploy to GitHub Pages
After building, commit `solver.js` and `solver.wasm` to the repo. GitHub Pages will serve them alongside `index.html`, `app.js`, and `style.css`.


