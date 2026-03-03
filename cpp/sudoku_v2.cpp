// sudoku_v2.cpp — Optimized high-performance Sudoku solver
// Improvements over v1:
//   1. Iterative constraint propagation (no recursion → less call overhead)
//   2. Batched initial board setup (single propagation pass)
//   3. Batch work-stealing in benchmark mode (64 puzzles/fetch)
//   4. Compact uint8_t lookup tables (better cache usage)
//   5. __attribute__((hot)) + branch hints on critical paths
//   6. Pre-cached per-cell unit pointers
//
// Usage:
//   ./sudoku_v2 <81-char puzzle> [--verbose]
//   ./sudoku_v2 --benchmark <csv_file> [--num_puzzles N] [--num_workers N]

#include <iostream>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>
#include <chrono>
#include <cstring>
#include <algorithm>
#include <thread>
#include <atomic>
#include <iomanip>
#include <cassert>
#include <cstdint>

// ============================================================
// Bitmask representation
// Digit d (1-9) stored as bit (1 << d), using bits 1..9
// ALL = 0x3FE = 0b0000_0011_1111_1110
// ============================================================
static constexpr uint16_t ALL = 0x3FE;
static constexpr int PROP_STACK_SIZE = 8192;
static constexpr int BATCH_SIZE = 64;

// Compact elimination work item (2 bytes)
struct Elim {
    uint8_t cell;
    uint8_t digit;
};

// Precomputed lookup tables — uint8_t for cache efficiency
static uint8_t peers[81][20];
static uint8_t row_units[9][9];
static uint8_t col_units[9][9];
static uint8_t box_units[9][9];
static uint8_t cell_row[81];
static uint8_t cell_col[81];
static uint8_t cell_box[81];
// Per-cell pointers to its 3 units (row, col, box)
static const uint8_t* cell_unit_ptrs[81][3];

static void init_tables() {
    for (int i = 0; i < 81; i++) {
        cell_row[i] = static_cast<uint8_t>(i / 9);
        cell_col[i] = static_cast<uint8_t>(i % 9);
        cell_box[i] = static_cast<uint8_t>((i / 27) * 3 + (i % 9) / 3);
    }
    for (int r = 0; r < 9; r++)
        for (int c = 0; c < 9; c++)
            row_units[r][c] = static_cast<uint8_t>(r * 9 + c);

    for (int c = 0; c < 9; c++)
        for (int r = 0; r < 9; r++)
            col_units[c][r] = static_cast<uint8_t>(r * 9 + c);

    for (int b = 0; b < 9; b++) {
        int br = (b / 3) * 3, bc = (b % 3) * 3;
        int k = 0;
        for (int r = br; r < br + 3; r++)
            for (int c = bc; c < bc + 3; c++)
                box_units[b][k++] = static_cast<uint8_t>(r * 9 + c);
    }
    for (int s = 0; s < 81; s++) {
        bool seen[81] = {};
        seen[s] = true;
        int n = 0;
        for (int k = 0; k < 9; k++) {
            int p = row_units[cell_row[s]][k];
            if (!seen[p]) { peers[s][n++] = static_cast<uint8_t>(p); seen[p] = true; }
        }
        for (int k = 0; k < 9; k++) {
            int p = col_units[cell_col[s]][k];
            if (!seen[p]) { peers[s][n++] = static_cast<uint8_t>(p); seen[p] = true; }
        }
        for (int k = 0; k < 9; k++) {
            int p = box_units[cell_box[s]][k];
            if (!seen[p]) { peers[s][n++] = static_cast<uint8_t>(p); seen[p] = true; }
        }
        assert(n == 20);
        cell_unit_ptrs[s][0] = row_units[cell_row[s]];
        cell_unit_ptrs[s][1] = col_units[cell_col[s]];
        cell_unit_ptrs[s][2] = box_units[cell_box[s]];
    }
}

// ============================================================
// Board: 81 × uint16_t candidate bitmasks
// Uses iterative propagation instead of recursion
// ============================================================
struct Board {
    uint16_t cand[81];

    // Core iterative propagation engine.
    // Processes a stack of pending eliminations.
    __attribute__((hot))
    bool propagate(Elim* stack, int top) {
        while (__builtin_expect(top > 0, 1)) {
            const Elim e = stack[--top];
            const uint16_t bit = 1u << e.digit;

            if (__builtin_expect(!(cand[e.cell] & bit), 0))
                continue;   // already absent — skip

            cand[e.cell] &= ~bit;
            const uint16_t c = cand[e.cell];

            if (__builtin_expect(c == 0, 0))
                return false;   // contradiction

            // --- Naked single: exactly one candidate left → tell all peers ---
            if (__builtin_popcount(c) == 1) {
                const uint8_t v = static_cast<uint8_t>(__builtin_ctz(c));
                const uint8_t* p = peers[e.cell];
                for (int i = 0; i < 20; i++)
                    stack[top++] = {p[i], v};
            }

            // --- Hidden single: check each of this cell's 3 units ---
            for (int u = 0; u < 3; u++) {
                const uint8_t* unit = cell_unit_ptrs[e.cell][u];
                int place = -1, cnt = 0;
                for (int k = 0; k < 9; k++) {
                    if (cand[unit[k]] & bit) {
                        place = k;
                        if (++cnt > 1) break;   // >1 place → nothing to do
                    }
                }
                if (__builtin_expect(cnt == 0, 0))
                    return false;   // digit has no place → contradiction

                if (cnt == 1 && __builtin_popcount(cand[unit[place]]) > 1) {
                    // Assign: eliminate every other candidate from this cell
                    const uint8_t cell = unit[place];
                    uint16_t others = cand[cell] & ~bit;
                    while (others) {
                        const int v = __builtin_ctz(others);
                        others &= others - 1;
                        stack[top++] = {cell, static_cast<uint8_t>(v)};
                    }
                }
            }
        }
        return true;
    }

    // Place digit d in cell s (eliminates every other candidate).
    __attribute__((hot))
    bool assign(int s, int d) {
        Elim stack[PROP_STACK_SIZE];
        int top = 0;
        uint16_t others = cand[s] & ~(1u << d);
        while (others) {
            const int v = __builtin_ctz(others);
            others &= others - 1;
            stack[top++] = {static_cast<uint8_t>(s), static_cast<uint8_t>(v)};
        }
        return propagate(stack, top);
    }
};

// ============================================================
// Batched board initialization — single propagation pass
// ============================================================
__attribute__((hot))
static bool init_board(Board& b, const char* puzzle) {
    for (int i = 0; i < 81; i++) b.cand[i] = ALL;

    Elim stack[PROP_STACK_SIZE];
    int top = 0;

    for (int i = 0; i < 81; i++) {
        const int d = puzzle[i] - '0';
        if (d >= 1 && d <= 9) {
            uint16_t others = b.cand[i] & ~(1u << d);
            while (others) {
                const int v = __builtin_ctz(others);
                others &= others - 1;
                stack[top++] = {static_cast<uint8_t>(i), static_cast<uint8_t>(v)};
            }
        }
    }
    return b.propagate(stack, top);
}

// ============================================================
// Solve with backtracking + MRV heuristic
// ============================================================
__attribute__((hot))
static bool solve(Board& b, int& guesses) {
    // Find unsolved cell with fewest candidates
    int best = -1, best_cnt = 10;
    for (int i = 0; i < 81; i++) {
        const int c = __builtin_popcount(b.cand[i]);
        if (c < 2) {
            if (__builtin_expect(c == 0, 0)) return false;
            continue;
        }
        if (c < best_cnt) {
            best_cnt = c;
            best = i;
            if (c == 2) break;   // can't improve on 2
        }
    }
    if (best == -1) return true;   // all cells solved

    uint16_t choices = b.cand[best];
    while (choices) {
        const int d = __builtin_ctz(choices);
        choices &= choices - 1;
        ++guesses;
        Board copy = b;   // cheap stack copy (162 bytes)
        if (copy.assign(best, d) && solve(copy, guesses)) {
            b = copy;
            return true;
        }
    }
    return false;
}

// ============================================================
// Output helpers
// ============================================================
static std::string board_to_string(const Board& b) {
    std::string s(81, '0');
    for (int i = 0; i < 81; i++) {
        if (__builtin_popcount(b.cand[i]) == 1)
            s[i] = static_cast<char>('0' + __builtin_ctz(b.cand[i]));
    }
    return s;
}

static void print_grid(const Board& b) {
    for (int r = 0; r < 9; r++) {
        std::cout << '[';
        for (int c = 0; c < 9; c++) {
            if (c) std::cout << ", ";
            const uint16_t v = b.cand[r * 9 + c];
            std::cout << ((__builtin_popcount(v) == 1) ? __builtin_ctz(v) : 0);
        }
        std::cout << "]\n";
    }
}

static void print_grid_from_string(const char* s) {
    for (int r = 0; r < 9; r++) {
        std::cout << '[';
        for (int c = 0; c < 9; c++) {
            if (c) std::cout << ", ";
            std::cout << static_cast<int>(s[r * 9 + c] - '0');
        }
        std::cout << "]\n";
    }
}

// ============================================================
// Verbose solve — mirrors Python's verbose output
// ============================================================
static bool solve_verbose(Board& b, int& guesses, int depth = 0) {
    int best = -1, best_cnt = 10;
    for (int i = 0; i < 81; i++) {
        const int c = __builtin_popcount(b.cand[i]);
        if (c < 2) {
            if (c == 0) return false;
            continue;
        }
        if (c < best_cnt) { best_cnt = c; best = i; if (c == 2) break; }
    }
    if (best == -1) {
        std::cout << "We're done!\n";
        return true;
    }

    int r = best / 9, co = best % 9;
    uint16_t choices = b.cand[best];
    while (choices) {
        const int d = __builtin_ctz(choices);
        choices &= choices - 1;
        ++guesses;
        std::cout << "*****DIFFICULT******* - Guessing cell (" << r << "," << co
                  << ") = " << d << " (options were ";
        uint16_t tmp = b.cand[best];
        bool first = true;
        std::cout << "[";
        while (tmp) {
            int v = __builtin_ctz(tmp); tmp &= tmp - 1;
            if (!first) std::cout << ", ";
            std::cout << v; first = false;
        }
        std::cout << "])\n";

        Board copy = b;
        if (copy.assign(best, d) && solve_verbose(copy, guesses, depth + 1)) {
            b = copy;
            return true;
        }
        std::cout << "Guess " << d << " at (" << r << "," << co << ") failed, backtracking...\n";
    }
    return false;
}

// ============================================================
// Benchmark mode — multi-threaded CSV solving
// ============================================================
struct PuzzleData {
    char puzzle[82];
    char solution[82];
};

static std::vector<PuzzleData> read_csv(const std::string& filename, int max_puzzles) {
    std::vector<PuzzleData> puzzles;
    puzzles.reserve(std::min(max_puzzles, 1000000));
    std::ifstream file(filename);
    if (!file.is_open()) {
        std::cerr << "Error: cannot open " << filename << "\n";
        return puzzles;
    }
    std::string line;
    std::getline(file, line);   // skip header
    int count = 0;
    while (count < max_puzzles && std::getline(file, line)) {
        const size_t comma = line.find(',');
        if (comma == std::string::npos || comma < 81) continue;
        PuzzleData pd;
        std::memcpy(pd.puzzle, line.c_str(), 81);
        pd.puzzle[81] = '\0';
        std::memcpy(pd.solution, line.c_str() + comma + 1, 81);
        pd.solution[81] = '\0';
        puzzles.push_back(pd);
        ++count;
    }
    return puzzles;
}

struct ThreadResult {
    int solved        = 0;
    int failed        = 0;
    int total_guesses = 0;
    double total_time = 0.0;
};

// Batch work-stealing: each thread grabs BATCH_SIZE puzzles at a time
__attribute__((hot))
static void benchmark_worker(const std::vector<PuzzleData>& puzzles,
                              std::atomic<int>& next_idx,
                              ThreadResult& result) {
    const int n = static_cast<int>(puzzles.size());
    while (true) {
        const int start = next_idx.fetch_add(BATCH_SIZE, std::memory_order_relaxed);
        if (start >= n) break;
        const int end = std::min(start + BATCH_SIZE, n);

        for (int idx = start; idx < end; idx++) {
            auto t0 = std::chrono::high_resolution_clock::now();
            Board b;
            int guesses = 0;
            bool ok = init_board(b, puzzles[idx].puzzle);
            if (ok) ok = solve(b, guesses);
            auto t1 = std::chrono::high_resolution_clock::now();
            const double elapsed = std::chrono::duration<double>(t1 - t0).count();

            if (ok) {
                bool match = true;
                for (int i = 0; i < 81 && match; i++) {
                    const int expected = puzzles[idx].solution[i] - '0';
                    const int got = __builtin_ctz(b.cand[i]);
                    if (got != expected) match = false;
                }
                if (match) {
                    result.solved++;
                    result.total_time += elapsed;
                    result.total_guesses += guesses;
                } else {
                    result.failed++;
                }
            } else {
                result.failed++;
            }
        }
    }
}

static void run_benchmark(const std::string& csv_file, int num_puzzles, int num_workers) {
    std::cout << "Reading puzzles from " << csv_file << "...\n";
    auto t0 = std::chrono::high_resolution_clock::now();
    auto puzzles = read_csv(csv_file, num_puzzles);
    auto t1 = std::chrono::high_resolution_clock::now();
    std::cout << "Read " << puzzles.size() << " puzzles in "
              << std::fixed << std::setprecision(2)
              << std::chrono::duration<double>(t1 - t0).count() << "s\n";

    if (puzzles.empty()) return;

    if (num_workers <= 0)
        num_workers = std::max(1u, std::thread::hardware_concurrency());
    std::cout << "Using " << num_workers << " workers\n";

    std::atomic<int> next_idx(0);
    std::vector<ThreadResult> results(num_workers);
    std::vector<std::thread> threads;

    auto start = std::chrono::high_resolution_clock::now();
    for (int i = 0; i < num_workers; i++)
        threads.emplace_back(benchmark_worker, std::cref(puzzles),
                             std::ref(next_idx), std::ref(results[i]));
    for (auto& t : threads) t.join();
    auto end = std::chrono::high_resolution_clock::now();
    const double wall = std::chrono::duration<double>(end - start).count();

    // Aggregate results
    int total_solved = 0, total_failed = 0, total_guesses = 0;
    double total_solve_time = 0.0;
    for (const auto& r : results) {
        total_solved    += r.solved;
        total_failed    += r.failed;
        total_guesses   += r.total_guesses;
        total_solve_time += r.total_time;
    }

    const int mins = static_cast<int>(wall / 60);
    const double secs = wall - mins * 60;

    if (total_solved > 0) {
        const double pct = 100.0 * total_solved / static_cast<int>(puzzles.size());
        const double puzzles_per_sec = total_solved / wall;
        std::cout << std::fixed << std::setprecision(2)
                  << "Solved " << total_solved << " puzzles (" << pct << "%) in "
                  << mins << "m " << secs << "s total\n"
                  << std::setprecision(0)
                  << "Throughput: " << puzzles_per_sec << " puzzles/sec\n"
                  << std::setprecision(5)
                  << "Average " << (total_solve_time / total_solved) << " seconds per puzzle\n"
                  << "Total guesses: " << total_guesses
                  << std::setprecision(2)
                  << " (avg " << static_cast<double>(total_guesses) / total_solved
                  << " per puzzle)\n";
    } else {
        std::cout << "No puzzles solved in " << mins << "m "
                  << std::fixed << std::setprecision(2) << secs << "s total\n";
    }
}

// ============================================================
// Main
// ============================================================
static void print_usage() {
    std::cerr << "Usage:\n"
              << "  sudoku_v2 <81-char puzzle> [--verbose]\n"
              << "  sudoku_v2 --benchmark <csv_file> [--num_puzzles N] [--num_workers N]\n";
}

int main(int argc, char* argv[]) {
    init_tables();

    if (argc < 2) { print_usage(); return 1; }

    const std::string arg1 = argv[1];

    // ---- Benchmark mode ----
    if (arg1 == "--benchmark") {
        if (argc < 3) { print_usage(); return 1; }
        const std::string csv_file = argv[2];
        int num_puzzles = 1000, num_workers = 0;
        for (int i = 3; i < argc; i++) {
            const std::string a = argv[i];
            if (a == "--num_puzzles" && i + 1 < argc) num_puzzles = std::stoi(argv[++i]);
            else if (a == "--num_workers" && i + 1 < argc) num_workers = std::stoi(argv[++i]);
        }
        run_benchmark(csv_file, num_puzzles, num_workers);
        return 0;
    }

    // ---- Single puzzle mode ----
    if (arg1.size() != 81) { print_usage(); return 1; }

    bool verbose = false;
    for (int i = 2; i < argc; i++)
        if (std::string(argv[i]) == "--verbose") verbose = true;

    int total_to_fill = 0;
    for (char ch : arg1) if (ch == '0' || ch == '.') total_to_fill++;

    std::cout << "Initial grid:\n";
    print_grid_from_string(arg1.c_str());
    std::cout << "---------\n";

    auto t0 = std::chrono::high_resolution_clock::now();
    Board b;
    int guesses = 0;
    bool ok = init_board(b, arg1.c_str());
    if (ok) {
        if (verbose)
            ok = solve_verbose(b, guesses);
        else
            ok = solve(b, guesses);
    }
    auto t1 = std::chrono::high_resolution_clock::now();
    const double elapsed = std::chrono::duration<double>(t1 - t0).count();

    std::cout << "---------\n";
    std::cout << (ok ? "Final solution:\n" : "How far we got:\n");
    if (guesses > 2) std::cout << "Total guesses: " << guesses << "\n";
    print_grid(b);
    std::cout << "---------\n";
    if (ok) {
        std::cout << std::fixed << std::setprecision(5)
                  << "Solve took " << elapsed << " seconds\n"
                  << "Total guesses: " << guesses << "\n";
    }

    return 0;
}
