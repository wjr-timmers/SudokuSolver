// sudoku.cpp — High-performance Sudoku solver
// C++ port of sudoku.py with bitmask-based constraint propagation
//
// Usage:
//   ./sudoku <81-char puzzle> [--verbose]
//   ./sudoku --benchmark <csv_file> [--num_puzzles N] [--num_workers N]

#include <iostream>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>
#include <chrono>
#include <cstring>
#include <algorithm>
#include <thread>
#include <mutex>
#include <atomic>
#include <iomanip>
#include <cassert>
#include <cstdint>

// ============================================================
// Bitmask representation
// Digit d (1-9) is stored as bit (1 << d), using bits 1..9
// ALL = 0x3FE = 0b0000_0011_1111_1110
// ============================================================
static constexpr uint16_t ALL = 0x3FE;

// Precomputed lookup tables (filled once at startup)
static int peers[81][20];       // 20 peers per cell (same row/col/box, excluding self)
static int row_units[9][9];     // cells in each row
static int col_units[9][9];     // cells in each column
static int box_units[9][9];     // cells in each 3×3 box
static int cell_row[81];
static int cell_col[81];
static int cell_box[81];

static void init_tables() {
    // Row/col/box membership
    for (int i = 0; i < 81; i++) {
        cell_row[i] = i / 9;
        cell_col[i] = i % 9;
        cell_box[i] = (i / 27) * 3 + (i % 9) / 3;
    }
    // Unit cell lists
    for (int r = 0; r < 9; r++)
        for (int c = 0; c < 9; c++)
            row_units[r][c] = r * 9 + c;

    for (int c = 0; c < 9; c++)
        for (int r = 0; r < 9; r++)
            col_units[c][r] = r * 9 + c;

    for (int b = 0; b < 9; b++) {
        int br = (b / 3) * 3, bc = (b % 3) * 3;
        int k = 0;
        for (int r = br; r < br + 3; r++)
            for (int c = bc; c < bc + 3; c++)
                box_units[b][k++] = r * 9 + c;
    }
    // Peer lists (20 peers per cell)
    for (int s = 0; s < 81; s++) {
        bool seen[81] = {};
        seen[s] = true;
        int n = 0;
        for (int k = 0; k < 9; k++) {
            int p = row_units[cell_row[s]][k];
            if (!seen[p]) { peers[s][n++] = p; seen[p] = true; }
        }
        for (int k = 0; k < 9; k++) {
            int p = col_units[cell_col[s]][k];
            if (!seen[p]) { peers[s][n++] = p; seen[p] = true; }
        }
        for (int k = 0; k < 9; k++) {
            int p = box_units[cell_box[s]][k];
            if (!seen[p]) { peers[s][n++] = p; seen[p] = true; }
        }
        assert(n == 20);
    }
}

// ============================================================
// Board: 81 × uint16_t candidate bitmasks  (162 bytes total)
// Solved cell ⇒ exactly one bit set in cand[]
// ============================================================
struct Board {
    uint16_t cand[81];

    // Remove digit d from candidates of cell s.
    // Propagates naked singles and hidden singles.
    // Returns false on contradiction.
    bool eliminate(int s, int d) {
        const uint16_t bit = 1u << d;
        if (!(cand[s] & bit)) return true;   // already absent
        cand[s] &= ~bit;
        const uint16_t c = cand[s];
        if (c == 0) return false;             // no candidates left

        // --- Naked single: one candidate remains → propagate to peers ---
        if (__builtin_popcount(c) == 1) {
            const int v = __builtin_ctz(c);
            for (int i = 0; i < 20; i++)
                if (!eliminate(peers[s][i], v)) return false;
        }

        // --- Hidden single: if d now has exactly one place in a unit, assign it ---
        auto check_unit = [&](const int* unit) -> bool {
            int place = -1, cnt = 0;
            for (int k = 0; k < 9; k++) {
                if (cand[unit[k]] & bit) {
                    place = k;
                    if (++cnt > 1) return true;   // >1 place → nothing to do
                }
            }
            if (cnt == 0) return false;            // digit has no place → contradiction
            if (cnt == 1 && __builtin_popcount(cand[unit[place]]) > 1)
                return assign(unit[place], d);
            return true;
        };

        return check_unit(row_units[cell_row[s]])
            && check_unit(col_units[cell_col[s]])
            && check_unit(box_units[cell_box[s]]);
    }

    // Place digit d in cell s by eliminating every other candidate.
    bool assign(int s, int d) {
        uint16_t others = cand[s] & ~(1u << d);
        while (others) {
            const int v = __builtin_ctz(others);
            others &= others - 1;               // clear lowest set bit
            if (!eliminate(s, v)) return false;
        }
        return true;
    }
};

// ============================================================
// Initialise board from 81-char string ('0' or '.' = empty)
// ============================================================
static bool init_board(Board& b, const char* puzzle) {
    for (int i = 0; i < 81; i++) b.cand[i] = ALL;
    for (int i = 0; i < 81; i++) {
        const int d = puzzle[i] - '0';
        if (d >= 1 && d <= 9) {
            if (!b.assign(i, d)) return false;
        }
    }
    return true;
}

// ============================================================
// Solve with back-tracking + constraint propagation
// Uses MRV (Minimum Remaining Values) heuristic
// ============================================================
static bool solve(Board& b, int& guesses) {
    // Find unsolved cell with fewest candidates
    int best = -1, best_cnt = 10;
    for (int i = 0; i < 81; i++) {
        const int c = __builtin_popcount(b.cand[i]);
        if (c < 2) {
            if (c == 0) return false;            // contradiction
            continue;
        }
        if (c < best_cnt) {
            best_cnt = c;
            best = i;
            if (c == 2) break;                   // can't improve on 2
        }
    }
    if (best == -1) return true;                  // all cells solved

    uint16_t choices = b.cand[best];
    while (choices) {
        const int d = __builtin_ctz(choices);
        choices &= choices - 1;
        ++guesses;
        Board copy = b;                           // cheap stack copy (162 bytes)
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

    int r = best / 9, c = best % 9;
    uint16_t choices = b.cand[best];
    while (choices) {
        const int d = __builtin_ctz(choices);
        choices &= choices - 1;
        ++guesses;
        std::cout << "*****DIFFICULT******* - Guessing cell (" << r << "," << c
                  << ") = " << d << " (options were ";
        // print options
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
        std::cout << "Guess " << d << " at (" << r << "," << c << ") failed, backtracking...\n";
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
    std::getline(file, line);                     // skip header
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

static void benchmark_worker(const std::vector<PuzzleData>& puzzles,
                              std::atomic<int>& next_idx,
                              ThreadResult& result) {
    const int n = static_cast<int>(puzzles.size());
    while (true) {
        const int idx = next_idx.fetch_add(1, std::memory_order_relaxed);
        if (idx >= n) break;

        auto t0 = std::chrono::high_resolution_clock::now();
        Board b;
        int guesses = 0;
        bool ok = init_board(b, puzzles[idx].puzzle);
        if (ok) ok = solve(b, guesses);
        auto t1 = std::chrono::high_resolution_clock::now();
        const double elapsed = std::chrono::duration<double>(t1 - t0).count();

        if (ok) {
            // Verify against expected solution
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
        std::cout << std::fixed << std::setprecision(2)
                  << "Solved " << total_solved << " puzzles (" << pct << "%) in "
                  << mins << "m " << secs << "s total\n"
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
              << "  sudoku <81-char puzzle> [--verbose]\n"
              << "  sudoku --benchmark <csv_file> [--num_puzzles N] [--num_workers N]\n";
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

    // Count empty cells (for display parity with Python)
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
