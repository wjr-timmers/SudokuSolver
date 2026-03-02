import time
import sudoku as s
import csv
from multiprocessing import Pool, cpu_count
import argparse

## DATA SET: https://www.kaggle.com/datasets/rohanrao/sudoku?resource=download

def read_string(puzzle_string):
    current_grid = []
    for i in range(0, 81, 9):
        grid_row = [int(char) for char in puzzle_string[i:i+9]]
        current_grid.append(grid_row)
    return current_grid


def read_grids(filename, num_puzzles):
    """Generator to stream puzzles from CSV instead of loading all at once."""
    count = 0
    with open(filename, 'r') as f:
        reader = csv.reader(f)
        next(reader)
        
        for row in reader:
            if count >= num_puzzles:
                break
            puzzle = read_string(row[0])
            solution = read_string(row[1])
            yield (puzzle, solution)
            count += 1


def solve_chunk(chunk):
    success_count = 0
    times = []
    total_guesses = 0
    for grid_data, solution_data in chunk:
        start = time.time()
        count_zeros = sum(row.count(0) for row in grid_data)
        solution, success, guesses = s.solve_sudoku(grid_data, 0, count_zeros)
        end = time.time()
        
        if success:
            assert solution == solution_data
            success_count += 1
            times.append(round(end - start, 5))
            total_guesses += guesses
        # else:
        #     print(solution)

    return success_count, times, total_guesses


def make_chunks(iterable, chunk_size):
    """Create chunks from an iterable without loading everything into memory."""
    chunk = []
    for item in iterable:
        chunk.append(item)
        if len(chunk) == chunk_size:
            yield chunk
            chunk = []
    if chunk:  # yield remaining items
        yield chunk


def solve_many_sudokus_parallel(grids, num_puzzles, chunk_size, num_workers):
    if num_workers is None:
        num_workers = cpu_count()
    
    chunks = make_chunks(grids, chunk_size)
    print(f"Using {num_workers} workers with chunk_size={chunk_size}")
    
    total_success = 0
    all_times = []
    total_guesses = 0
    chunk_count = 0
    
    with Pool(processes=num_workers) as pool:
        # Process results as they arrive instead of loading all into memory
        for success_count, times, guesses in pool.imap_unordered(solve_chunk, chunks):
            total_success += success_count
            all_times.extend(times)
            total_guesses += guesses
            chunk_count += 1
            if chunk_count % 10 == 0:
                print(f"Processed {chunk_count * chunk_size} puzzles ({total_success} successful)")
    
    return total_success, all_times, total_guesses


def main():
    parser = argparse.ArgumentParser(description="Sudoku Solver Benchmark")
    parser.add_argument('--num_puzzles', type=int, default=1000, help="Number of puzzles to solve (max is 9 million)")
    parser.add_argument('--chunk_size', type=int, default=50, help="Number of puzzles per chunk (lower = less memory per worker)")
    parser.add_argument('--num_workers', type=int, default=None, help="Number of workers (default: auto-detect)")
    args = parser.parse_args()

    num_puzzles = args.num_puzzles
    chunk_size = args.chunk_size
    num_workers = args.num_workers

    grids = read_grids('sudoku.csv', num_puzzles)  # Generator, doesn't load all at once
    start = time.time()
    success_count, times, total_guesses = solve_many_sudokus_parallel(grids, num_puzzles, chunk_size, num_workers)
    total_time = time.time() - start

    mins, secs = divmod(total_time, 60)
    if success_count > 0:
        print(f"Solved {success_count} puzzles ({round((success_count/num_puzzles)*100,2)}%) in {int(mins)}m {secs:.2f}s total")
        print(f"Average {sum(times)/success_count:.5f} seconds per puzzle")
        print(f"Total guesses: {total_guesses} (avg {total_guesses/success_count:.2f} per puzzle)")
    else:
        print(f"No puzzles solved in {int(mins)}m {secs:.2f}s total")


if __name__ == '__main__':
    main()
