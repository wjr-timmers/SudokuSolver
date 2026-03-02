#!/bin/bash
#SBATCH --job-name=sudoku_cpp_benchmark
#SBATCH --output=logs/bench_%A_%a.out
#SBATCH --error=logs/bench_%A_%a.err
#SBATCH --ntasks=1
#SBATCH --cpus-per-task=62

module load 2025
module load GCCcore/14.2.0

# Copy files to fast local storage
cp -r $SLURM_SUBMIT_DIR/* $TMPDIR/
cp $SLURM_SUBMIT_DIR/../sudoku.csv $TMPDIR/
cd $TMPDIR

# Build optimized binary
make clean
make

NUM_PUZZLES=9000000
NUM_WORKERS=$SLURM_CPUS_PER_TASK

echo "Starting C++ benchmark: $NUM_PUZZLES puzzles using $NUM_WORKERS threads."
./sudoku --benchmark sudoku.csv --num_puzzles $NUM_PUZZLES --num_workers $NUM_WORKERS
