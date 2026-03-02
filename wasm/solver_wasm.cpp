// solver_wasm.cpp — Sudoku solver compiled to WebAssembly via Emscripten
// Core solver logic from cpp/sudoku.cpp, with JS-callable exports
//
// Build: cd wasm && make
// Output: ../solver.js + ../solver.wasm

#include <emscripten.h>
#include <cstdint>
#include <cstring>
#include <cstdio>

// ============================================================
// Bitmask representation
// Digit d (1-9) is stored as bit (1 << d), using bits 1..9
// ALL = 0x3FE = 0b0000_0011_1111_1110
// ============================================================
static constexpr uint16_t ALL = 0x3FE;

// Precomputed lookup tables (filled once at startup)
static int peers[81][20];
static int row_units[9][9];
static int col_units[9][9];
static int box_units[9][9];
static int cell_row[81];
static int cell_col[81];
static int cell_box[81];

static void init_tables() {
    for (int i = 0; i < 81; i++) {
        cell_row[i] = i / 9;
        cell_col[i] = i % 9;
        cell_box[i] = (i / 27) * 3 + (i % 9) / 3;
    }
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
    }
}

// ============================================================
// Board: 81 × uint16_t candidate bitmasks
// ============================================================
struct Board {
    uint16_t cand[81];

    bool eliminate(int s, int d) {
        const uint16_t bit = 1u << d;
        if (!(cand[s] & bit)) return true;
        cand[s] &= ~bit;
        const uint16_t c = cand[s];
        if (c == 0) return false;

        // Naked single
        if (__builtin_popcount(c) == 1) {
            const int v = __builtin_ctz(c);
            for (int i = 0; i < 20; i++)
                if (!eliminate(peers[s][i], v)) return false;
        }

        // Hidden single
        auto check_unit = [&](const int* unit) -> bool {
            int place = -1, cnt = 0;
            for (int k = 0; k < 9; k++) {
                if (cand[unit[k]] & bit) {
                    place = k;
                    if (++cnt > 1) return true;
                }
            }
            if (cnt == 0) return false;
            if (cnt == 1 && __builtin_popcount(cand[unit[place]]) > 1)
                return assign(unit[place], d);
            return true;
        };

        return check_unit(row_units[cell_row[s]])
            && check_unit(col_units[cell_col[s]])
            && check_unit(box_units[cell_box[s]]);
    }

    bool assign(int s, int d) {
        uint16_t others = cand[s] & ~(1u << d);
        while (others) {
            const int v = __builtin_ctz(others);
            others &= others - 1;
            if (!eliminate(s, v)) return false;
        }
        return true;
    }
};

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

// MRV backtracking search
static bool solve(Board& b, int& guesses) {
    int best = -1, best_cnt = 10;
    for (int i = 0; i < 81; i++) {
        const int c = __builtin_popcount(b.cand[i]);
        if (c < 2) {
            if (c == 0) return false;
            continue;
        }
        if (c < best_cnt) {
            best_cnt = c;
            best = i;
            if (c == 2) break;
        }
    }
    if (best == -1) return true;

    uint16_t choices = b.cand[best];
    while (choices) {
        const int d = __builtin_ctz(choices);
        choices &= choices - 1;
        ++guesses;
        Board copy = b;
        if (copy.assign(best, d) && solve(copy, guesses)) {
            b = copy;
            return true;
        }
    }
    return false;
}

// ============================================================
// Exported function for JavaScript
// ============================================================
static bool tables_initialized = false;
static char result_buffer[512];

extern "C" {

EMSCRIPTEN_KEEPALIVE
const char* solve_puzzle(const char* puzzle) {
    if (!tables_initialized) {
        init_tables();
        tables_initialized = true;
    }

    int empty_cells = 0;
    for (int i = 0; i < 81; i++)
        if (puzzle[i] == '0' || puzzle[i] == '.') empty_cells++;

    double start = emscripten_get_now();

    Board b;
    int guesses = 0;
    bool ok = init_board(b, puzzle);
    if (ok) ok = solve(b, guesses);

    double elapsed = (emscripten_get_now() - start) / 1000.0;

    // Build solution string
    char solution[82];
    for (int i = 0; i < 81; i++) {
        if (__builtin_popcount(b.cand[i]) == 1)
            solution[i] = '0' + __builtin_ctz(b.cand[i]);
        else
            solution[i] = '0';
    }
    solution[81] = '\0';

    snprintf(result_buffer, sizeof(result_buffer),
        "{\"solution\":\"%s\",\"success\":%s,\"time\":%.5f,\"guesses\":%d,\"empty_cells\":%d}",
        solution, ok ? "true" : "false", elapsed, guesses, empty_cells);

    return result_buffer;
}

} // extern "C"
