#!/bin/bash
#SBATCH --job-name=sudoku_v2_benchmark
#SBATCH --output=logs/bench_v2_%A_%a.out
#SBATCH --error=logs/bench_v2_%A_%a.err
#SBATCH --ntasks=1
#SBATCH --cpus-per-task=64
#SBATCH --partition=defq

module load 2025
module load GCCcore/14.2.0

# Copy files to fast local storage
cp -r $SLURM_SUBMIT_DIR/* $TMPDIR/
cp $SLURM_SUBMIT_DIR/../sudoku.csv $TMPDIR/
cd $TMPDIR

# Build optimized v2 binary (no PGO — iterative propagation + compiler opts)
make clean
make v2

echo $hostname

NUM_PUZZLES=9000000
NUM_WORKERS=$SLURM_CPUS_PER_TASK

echo "Starting C++ v2 benchmark: $NUM_PUZZLES puzzles using $NUM_WORKERS threads."
echo "Optimizations: iterative propagation, batched init, batch work-stealing, -fno-exceptions -fno-rtti"
./sudoku_v2 --benchmark sudoku.csv --num_puzzles $NUM_PUZZLES --num_workers $NUM_WORKERS
