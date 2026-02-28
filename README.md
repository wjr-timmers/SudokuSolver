# Sudoku-solver
Code to solve a sudoku for fun.

![Image](https://media.istockphoto.com/id/172406544/nl/foto/sudoku-puzzle.jpg?s=2048x2048&w=is&k=20&c=oLlnSR9R04iS1OXtooYO73bE_sj7pkrpANzD9APwz6U=)

## Usage

### Solve a single puzzle
Run the solver directly with a puzzle from `templates.py`:
```bash
python sudoku.py
```
Edit the `test_grid` variable in the `__main__` block to change which puzzle to solve.

### Run benchmark
Benchmark against puzzles from `sudoku.csv` (dataset from [Kaggle](https://www.kaggle.com/datasets/rohanrao/sudoku)):
```bash
# Solve 1000 puzzles with auto-detected workers
python benchmark.py --num_puzzles 1000

# Customize chunk size and worker count
python benchmark.py --num_puzzles 10000 --chunk_size 100 --num_workers 8
```

**Arguments:**
- `--num_puzzles` - Number of puzzles to solve (default: 1000, max: 9 million)
- `--chunk_size` - Puzzles per parallel chunk (default: 50)
- `--num_workers` - Number of parallel workers (default: auto-detect CPU cores)


