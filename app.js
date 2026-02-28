// Sudoku Solver App - Using Pyodide to run Python in the browser

let pyodide = null;
let templates = {};
let logEntries = [];
let logCounter = 0;

// Initialize the app
async function init() {
    createGrid();
    loadLogFromStorage();
    renderLog();
    
    try {
        updateStatus('Loading Python environment...', 'loading');
        pyodide = await loadPyodide();
        
        updateStatus('Loading solver code...', 'loading');
        await loadPythonCode();
        
        updateStatus('Ready to solve!', 'ready');
        document.getElementById('solve-btn').disabled = false;
    } catch (error) {
        updateStatus('Error loading Python: ' + error.message, 'error');
        console.error(error);
    }
    
    setupEventListeners();
}

function updateStatus(message, state) {
    const statusBar = document.getElementById('status-bar');
    const statusText = document.getElementById('status-text');
    const loader = document.getElementById('loader');
    
    statusText.textContent = message;
    statusBar.className = 'status-bar ' + state;
    
    if (state === 'loading') {
        loader.classList.remove('hidden');
    } else {
        loader.classList.add('hidden');
    }
}

function createGrid() {
    const grid = document.getElementById('sudoku-grid');
    grid.innerHTML = '';
    
    for (let i = 0; i < 81; i++) {
        const input = document.createElement('input');
        input.type = 'text';
        input.maxLength = 1;
        input.dataset.index = i;
        input.addEventListener('input', handleCellInput);
        input.addEventListener('keydown', handleKeyNavigation);
        grid.appendChild(input);
    }
}

function handleCellInput(e) {
    const value = e.target.value;
    if (value && !/^[1-9]$/.test(value)) {
        e.target.value = '';
    }
    e.target.classList.remove('solved', 'invalid');
}

function handleKeyNavigation(e) {
    const index = parseInt(e.target.dataset.index);
    const inputs = document.querySelectorAll('.sudoku-grid input');
    let newIndex = index;
    
    switch(e.key) {
        case 'ArrowUp': newIndex = index - 9; break;
        case 'ArrowDown': newIndex = index + 9; break;
        case 'ArrowLeft': newIndex = index - 1; break;
        case 'ArrowRight': newIndex = index + 1; break;
        default: return;
    }
    
    if (newIndex >= 0 && newIndex < 81) {
        inputs[newIndex].focus();
        e.preventDefault();
    }
}

async function loadPythonCode() {
    // Load the sudoku solver code
    const solverCode = `
import time
import itertools
from collections import defaultdict
import copy

VALUES = [1,2,3,4,5,6,7,8,9]
_BLOCK_COORDINATES_CACHE = None

def get_all_block_coordinates():
    global _BLOCK_COORDINATES_CACHE
    if _BLOCK_COORDINATES_CACHE is not None:
        return _BLOCK_COORDINATES_CACHE
    
    blocks = []
    for block_row in range(3):
        for block_col in range(3):
            row_start = block_row * 3
            col_start = block_col * 3
            coords = [(i, j) for i in range(row_start, row_start + 3) 
                             for j in range(col_start, col_start + 3)]
            blocks.append(coords)
    
    _BLOCK_COORDINATES_CACHE = blocks
    return blocks

def get_block_coordinates(row_idx, col_idx):
    block_row = row_idx // 3
    block_col = col_idx // 3
    block_idx = block_row * 3 + block_col
    all_blocks = get_all_block_coordinates()
    return all_blocks[block_idx]

def check_block(grid, temp_options, cell_coordinate, verbose=False):
    row_idx, col_idx = cell_coordinate
    coordinates = get_block_coordinates(row_idx, col_idx).copy()
    coordinates.remove((row_idx, col_idx))
    for i, j in coordinates:
        value = grid[i][j]
        if value in temp_options:
            temp_options.remove(value)
    return temp_options

def check_hidden(list_of_possibilities_in_block, option_grid, flag="None", verbose=False, hidden_quant=2):
    occurences = {}
    for v in VALUES:
        occurences[v] = {'count': 0, 'coordinates': []}
    
    for coordinate, values in list_of_possibilities_in_block:
        if values == [0]:
            continue
        else:
            for value in values:
                occurences[value]['count'] += 1
                occurences[value]['coordinates'].append(coordinate)
    
    coord_groups = defaultdict(list)
    for num, data in occurences.items():
        if data['count'] == hidden_quant:
            coord_key = tuple(data['coordinates'])
            coord_groups[coord_key].append(num)
    
    for coords, numbers_hidden in coord_groups.items():
        if len(numbers_hidden) == hidden_quant:
            coords = list(coords)
            for coordinate, values in list_of_possibilities_in_block:
                if coordinate in coords:
                    for v in values[:]:
                        if v in numbers_hidden:
                            continue
                        else:
                            values.remove(v)
                            if v in option_grid[coordinate[0]][coordinate[1]]:
                                option_grid[coordinate[0]][coordinate[1]].remove(v)
    
    return list_of_possibilities_in_block, option_grid

def check_naked(list_of_possibilities_in_block, option_grid, flag="None", verbose=False, naked_quant=2):
    naked_options = []
    
    if naked_quant == 1:
        naked_options = [x for x in list_of_possibilities_in_block if len(x[1]) == 1]
    else:
        naked_quant_options = [x for x in list_of_possibilities_in_block if (len(x[1]) == naked_quant)]
        for combo in itertools.combinations(naked_quant_options, naked_quant):
            indices = [c[0] for c in combo]
            values = [c[1] for c in combo]
            union = set().union(*values)
            if len(union) == naked_quant:
                for i in range(naked_quant):
                    naked_options.append((indices[i], values[i]))
    
    if len(naked_options) != 0:
        naked_quant_coordinates = set(coord for coord, vals in naked_options)
        for coordinate, values in list_of_possibilities_in_block:
            if coordinate in naked_quant_coordinates:
                continue
            for naked_coord, naked_values in naked_options:
                for naked_value in naked_values:
                    if naked_value in values[:]:
                        values.remove(naked_value)
                        if naked_value in option_grid[coordinate[0]][coordinate[1]]:
                            option_grid[coordinate[0]][coordinate[1]].remove(naked_value)
    
    return list_of_possibilities_in_block, option_grid

def get_block_options(cell_coordinate, option_grid, verbose=False):
    row_idx, col_idx = cell_coordinate
    coordinates_block = get_block_coordinates(row_idx, col_idx)
    coordinates_col = [(i, col_idx) for i in range(0, 9)]
    coordinates_row = [(row_idx, j) for j in range(0, 9)]
    
    list_of_possibilities_block_total = []
    list_of_possibitlies_row_total = []
    list_of_possibitlies_col_total = []
    
    for i, j in coordinates_block:
        list_of_possibilities_block_total.append(((i, j), option_grid[i][j]))
    for i, j in coordinates_row:
        list_of_possibitlies_row_total.append(((i, j), option_grid[i][j]))
    for i, j in coordinates_col:
        list_of_possibitlies_col_total.append(((i, j), option_grid[i][j]))
    
    return list_of_possibilities_block_total, list_of_possibitlies_row_total, list_of_possibitlies_col_total

def check_if_fill_in(list_of_possibilities, cell_coordinate, option_grid, verbose=False):
    list_of_possibilities = [item for item in list_of_possibilities if item[0] != cell_coordinate]
    row_idx, col_idx = cell_coordinate
    current_possibilities = option_grid[row_idx][col_idx]
    
    if not current_possibilities or current_possibilities == [0]:
        return None, option_grid
    
    neighbor_values = set()
    for coordinate, possibilities in list_of_possibilities:
        if possibilities != [0] and len(possibilities) > 0:
            neighbor_values.update(possibilities)
    
    unique_value = None
    unique_count = 0
    
    for num in current_possibilities:
        if num not in neighbor_values:
            unique_count += 1
            if unique_count > 1:
                return None, option_grid
            unique_value = num
    
    if unique_count == 1:
        return unique_value, option_grid
    else:
        return None, option_grid

def check_block_options(option_grid, cell_coordinate, verbose=False):
    list_of_possibilities_block_total, list_of_possibitlies_row_total, list_of_possibitlies_col_total = get_block_options(cell_coordinate, option_grid, verbose=verbose)
    
    for i in range(1, 5):
        list_of_possibilities_block_total, option_grid = check_naked(list_of_possibilities_block_total, option_grid, flag="block", verbose=verbose, naked_quant=i)
        value_to_fill_in, option_grid = check_if_fill_in(list_of_possibilities_block_total, cell_coordinate, option_grid, verbose=False)
        if value_to_fill_in is not None:
            return value_to_fill_in, option_grid
        
        list_of_possibitlies_row_total, option_grid = check_naked(list_of_possibitlies_row_total, option_grid, flag="row", verbose=verbose, naked_quant=i)
        value_to_fill_in, option_grid = check_if_fill_in(list_of_possibitlies_row_total, cell_coordinate, option_grid, verbose=False)
        if value_to_fill_in is not None:
            return value_to_fill_in, option_grid
        
        list_of_possibitlies_col_total, option_grid = check_naked(list_of_possibitlies_col_total, option_grid, flag="col", verbose=verbose, naked_quant=i)
        value_to_fill_in, option_grid = check_if_fill_in(list_of_possibitlies_col_total, cell_coordinate, option_grid, verbose=False)
        if value_to_fill_in is not None:
            return value_to_fill_in, option_grid
        
        list_of_possibilities_block_total, option_grid = check_hidden(list_of_possibilities_block_total, option_grid, flag="block", verbose=verbose, hidden_quant=i)
        value_to_fill_in, option_grid = check_if_fill_in(list_of_possibilities_block_total, cell_coordinate, option_grid, verbose=False)
        if value_to_fill_in is not None:
            return value_to_fill_in, option_grid
        
        list_of_possibitlies_row_total, option_grid = check_hidden(list_of_possibitlies_row_total, option_grid, flag="row", verbose=verbose, hidden_quant=i)
        value_to_fill_in, option_grid = check_if_fill_in(list_of_possibitlies_row_total, cell_coordinate, option_grid, verbose=False)
        if value_to_fill_in is not None:
            return value_to_fill_in, option_grid
        
        list_of_possibitlies_col_total, option_grid = check_hidden(list_of_possibitlies_col_total, option_grid, flag="col", verbose=verbose, hidden_quant=i)
        value_to_fill_in, option_grid = check_if_fill_in(list_of_possibitlies_col_total, cell_coordinate, option_grid, verbose=False)
        if value_to_fill_in is not None:
            return value_to_fill_in, option_grid
    
    return None, option_grid

def get_grid_options(grid, verbose=False):
    options_grid = [[[0] for i in range(9)] for j in range(9)]
    
    for row_idx in range(len(grid)):
        for col_idx in range(len(grid[0])):
            cell_value = grid[row_idx][col_idx]
            possibilities = []
            
            if cell_value != 0:
                options_grid[row_idx][col_idx] = [0]
            else:
                for v in VALUES:
                    row = grid[row_idx]
                    col = [row[col_idx] for row in grid]
                    if (v not in row) and (v not in col):
                        possibilities.append(v)
                        possibilities = check_block(grid, possibilities, (row_idx, col_idx), verbose=verbose)
                options_grid[row_idx][col_idx] = possibilities
    
    return options_grid

def propagate_constraint(options_grid, row_idx, col_idx, fill_in_value):
    for j in range(9):
        if fill_in_value in options_grid[row_idx][j]:
            options_grid[row_idx][j].remove(fill_in_value)
    
    for i in range(9):
        if fill_in_value in options_grid[i][col_idx]:
            options_grid[i][col_idx].remove(fill_in_value)
    
    block_row_start = (row_idx // 3) * 3
    block_col_start = (col_idx // 3) * 3
    for i in range(block_row_start, block_row_start + 3):
        for j in range(block_col_start, block_col_start + 3):
            if fill_in_value in options_grid[i][j]:
                options_grid[i][j].remove(fill_in_value)
    
    return options_grid

def check_for_contradiction(options_grid):
    for row_idx in range(9):
        for col_idx in range(9):
            cell = options_grid[row_idx][col_idx]
            if cell != [0] and len(cell) == 0:
                return True
    return False

def solve_sudoku(grid, counter, total_to_fillin, verbose=False, recalculated_grid=None, guessed=False, guess_count=0):
    if counter == total_to_fillin:
        return grid, True, guess_count
    
    if recalculated_grid is None:
        options_grid = get_grid_options(grid, verbose=verbose)
    else:
        options_grid = recalculated_grid
    
    if guessed and check_for_contradiction(options_grid):
        return grid, False, guess_count
    
    fill_in_value = None
    for row_idx in range(len(grid)):
        for col_idx in range(len(grid[0])):
            cell_value = options_grid[row_idx][col_idx]
            
            if cell_value == [0]:
                pass
            elif len(cell_value) == 1:
                fill_in_value = cell_value[0]
            else:
                fill_in_value, options_grid = check_block_options(options_grid, (row_idx, col_idx), verbose=verbose)
            
            if fill_in_value is not None:
                grid[row_idx][col_idx] = fill_in_value
                options_grid[row_idx][col_idx] = [0]
                options_grid = propagate_constraint(options_grid, row_idx, col_idx, fill_in_value)
                counter += 1
                return solve_sudoku(grid, counter, total_to_fillin, verbose=verbose, recalculated_grid=options_grid, guessed=False, guess_count=guess_count)
    
    best_cell = None
    min_options = 10
    for row_idx in range(9):
        for col_idx in range(9):
            cell_value = options_grid[row_idx][col_idx]
            if cell_value != [0] and len(cell_value) >= 2 and len(cell_value) < min_options:
                min_options = len(cell_value)
                best_cell = (row_idx, col_idx, cell_value)
    
    if best_cell is None:
        return grid, False, guess_count
    
    row_idx, col_idx, options = best_cell
    
    for guess in options:
        grid_backup = copy.deepcopy(grid)
        options_grid_backup = copy.deepcopy(options_grid)
        
        grid[row_idx][col_idx] = guess
        options_grid[row_idx][col_idx] = [0]
        options_grid = propagate_constraint(options_grid, row_idx, col_idx, guess)
        guess_count += 1
        
        result_grid, success, guess_count = solve_sudoku(grid, counter + 1, total_to_fillin, verbose=verbose, recalculated_grid=options_grid, guessed=True, guess_count=guess_count)
        
        if success:
            return result_grid, True, guess_count
        else:
            grid = grid_backup
            options_grid = options_grid_backup
    
    return grid, False, guess_count

def solve_from_flat(flat_grid):
    """Solve sudoku from flat list of 81 numbers"""
    grid = [flat_grid[i*9:(i+1)*9] for i in range(9)]
    grid = [list(row) for row in grid]
    
    count_zeros = sum(row.count(0) for row in grid)
    
    start = time.time()
    solution, success, guesses = solve_sudoku(grid, 0, count_zeros, verbose=False)
    end = time.time()
    
    flat_solution = [cell for row in solution for cell in row]
    
    return {
        'solution': flat_solution,
        'success': success,
        'time': round(end - start, 5),
        'guesses': guesses,
        'empty_cells': count_zeros
    }
`;

    await pyodide.runPythonAsync(solverCode);
    
    // Load templates
    await loadTemplates();
}

async function loadTemplates() {
    const templateCode = `
templates = {
    "test_grid1": [[0, 0, 9, 8, 0, 1, 5, 0, 6], [0, 0, 0, 3, 6, 0, 8, 0, 9], [0, 0, 0, 0, 0, 0, 1, 0, 2], [0, 0, 0, 0, 0, 0, 0, 1, 5], [3, 5, 0, 0, 0, 0, 0, 9, 4], [6, 7, 0, 0, 0, 0, 0, 0, 8], [8, 0, 3, 0, 0, 0, 0, 0, 7], [0, 0, 0, 0, 3, 8, 0, 0, 1], [4, 0, 5, 6, 0, 7, 9, 8, 3]],
    "test_grid2": [[7, 0, 9, 8, 0, 1, 5, 0, 6], [1, 0, 0, 3, 6, 0, 8, 0, 9], [5, 0, 0, 0, 0, 0, 1, 0, 2], [2, 0, 0, 0, 0, 0, 0, 1, 5], [3, 5, 0, 0, 0, 0, 0, 9, 4], [6, 7, 0, 0, 0, 0, 0, 0, 8], [8, 0, 3, 0, 0, 0, 0, 0, 7], [9, 0, 0, 0, 3, 8, 0, 0, 1], [4, 0, 5, 6, 0, 7, 9, 8, 3]],
    "test_grid3": [[7, 3, 9, 8, 2, 1, 5, 4, 6], [1, 4, 0, 3, 6, 0, 8, 7, 9], [5, 8, 6, 0, 0, 0, 1, 0, 2], [2, 0, 0, 7, 4, 3, 0, 1, 5], [3, 5, 1, 2, 8, 6, 7, 9, 4], [6, 7, 0, 1, 5, 9, 3, 2, 8], [8, 0, 3, 5, 9, 2, 4, 6, 7], [9, 0, 0, 0, 3, 8, 0, 0, 1], [4, 0, 5, 6, 0, 7, 9, 8, 3]],
    "test_grid4": [[7, 3, 9, 8, 2, 1, 5, 4, 6], [1, 4, 0, 3, 0, 0, 8, 7, 9], [0, 8, 6, 0, 0, 0, 1, 0, 2], [2, 0, 0, 7, 4, 3, 0, 1, 5], [0, 5, 1, 2, 8, 6, 7, 0, 4], [6, 7, 0, 1, 5, 9, 3, 2, 8], [0, 0, 3, 5, 0, 2, 4, 6, 7], [9, 0, 0, 0, 3, 8, 0, 0, 1], [4, 0, 5, 6, 0, 7, 9, 8, 3]],
    "test_grid5": [[6, 3, 9, 0, 0, 8, 0, 0, 4], [0, 8, 7, 0, 0, 4, 0, 0, 9], [0, 2, 4, 0, 0, 0, 3, 8, 0], [0, 0, 5, 0, 8, 6, 7, 2, 1], [0, 0, 8, 1, 0, 2, 4, 0, 0], [2, 1, 6, 4, 5, 7, 8, 9, 3], [0, 5, 3, 8, 0, 0, 6, 0, 0], [8, 6, 1, 7, 0, 0, 9, 0, 0], [9, 0, 2, 6, 0, 0, 1, 0, 8]],
    "test_grid6": [[0, 0, 0, 0, 0, 0, 0, 1, 8], [0, 0, 0, 7, 0, 4, 5, 0, 0], [0, 5, 6, 0, 0, 0, 0, 0, 4], [0, 8, 4, 0, 0, 0, 0, 0, 3], [0, 2, 0, 9, 0, 5, 0, 4, 0], [1, 0, 0, 0, 0, 0, 8, 7, 0], [6, 0, 0, 0, 0, 0, 2, 3, 0], [0, 0, 5, 6, 0, 7, 0, 0, 0], [2, 1, 0, 0, 0, 0, 0, 0, 0]],
    "test_grid7": [[0, 0, 0, 4, 0, 0, 3, 6, 5], [0, 0, 4, 0, 5, 6, 7, 8, 1], [6, 7, 5, 8, 1, 3, 9, 4, 2], [0, 0, 6, 0, 0, 5, 4, 9, 0], [5, 4, 0, 6, 9, 0, 2, 1, 0], [0, 0, 9, 0, 4, 2, 8, 5, 6], [0, 6, 2, 5, 0, 4, 1, 7, 9], [0, 0, 0, 0, 6, 0, 5, 3, 4], [4, 5, 0, 0, 0, 0, 6, 2, 8]],
    "test_grid8": [[0, 7, 0, 0, 0, 9, 5, 3, 8], [0, 0, 0, 0, 0, 5, 7, 6, 4], [3, 5, 0, 7, 0, 0, 2, 9, 1], [0, 0, 5, 4, 0, 0, 0, 1, 7], [4, 0, 7, 5, 0, 0, 0, 2, 3], [0, 6, 0, 0, 2, 7, 8, 4, 5], [5, 0, 0, 9, 7, 3, 4, 8, 6], [8, 4, 9, 6, 5, 1, 3, 7, 2], [7, 3, 6, 8, 4, 2, 1, 5, 9]],
    "test_grid9": [[0, 5, 8, 6, 0, 0, 0, 4, 1], [0, 0, 3, 0, 4, 5, 6, 0, 0], [9, 6, 0, 8, 0, 0, 3, 0, 0], [6, 0, 5, 4, 0, 0, 1, 0, 0], [0, 0, 7, 0, 0, 8, 2, 0, 4], [4, 0, 2, 0, 0, 0, 0, 0, 3], [0, 2, 9, 5, 0, 0, 4, 3, 6], [0, 0, 6, 0, 2, 4, 5, 1, 0], [5, 0, 0, 0, 0, 0, 8, 0, 2]],
    "test_grid10": [[3, 7, 2, 1, 9, 8, 4, 6, 5], [5, 6, 8, 7, 2, 4, 1, 9, 3], [4, 1, 9, 3, 6, 5, 0, 0, 0], [0, 5, 3, 2, 4, 6, 0, 0, 0], [6, 9, 0, 5, 8, 0, 3, 2, 4], [2, 8, 4, 9, 0, 3, 5, 0, 6], [8, 3, 6, 4, 0, 0, 0, 5, 0], [0, 2, 5, 6, 3, 0, 7, 4, 8], [0, 4, 0, 8, 5, 2, 6, 3, 0]],
    "test_grid11": [[8, 0, 7, 1, 9, 5, 3, 0, 6], [1, 6, 5, 8, 0, 0, 9, 7, 0], [3, 9, 0, 7, 0, 6, 5, 8, 1], [0, 3, 1, 9, 6, 7, 8, 0, 5], [9, 5, 0, 3, 8, 1, 7, 6, 0], [7, 8, 6, 0, 5, 0, 1, 9, 3], [0, 7, 3, 0, 1, 8, 6, 5, 9], [5, 0, 9, 6, 7, 0, 4, 0, 8], [6, 0, 8, 5, 0, 9, 2, 0, 7]],
    "test_grid12": [[6, 0, 2, 0, 9, 0, 1, 0, 3], [5, 0, 9, 0, 0, 8, 4, 0, 0], [0, 0, 0, 2, 0, 0, 0, 9, 0], [0, 0, 0, 0, 0, 6, 2, 7, 5], [7, 6, 0, 8, 2, 0, 9, 3, 1], [0, 2, 0, 0, 7, 0, 6, 4, 8], [4, 5, 6, 3, 8, 2, 7, 1, 9], [0, 9, 8, 4, 0, 7, 0, 0, 0], [2, 0, 7, 0, 5, 9, 0, 0, 4]],
    "test_grid13": [[8, 9, 3, 6, 5, 4, 1, 2, 7], [4, 7, 5, 8, 2, 1, 3, 6, 9], [6, 1, 2, 9, 7, 3, 8, 4, 5], [2, 3, 0, 0, 1, 9, 5, 8, 0], [0, 0, 1, 0, 8, 5, 0, 0, 0], [5, 0, 0, 3, 6, 2, 0, 7, 1], [3, 5, 4, 2, 9, 6, 7, 1, 8], [0, 0, 0, 0, 4, 7, 0, 0, 0], [0, 2, 0, 0, 3, 8, 0, 0, 0]],
    "test_grid14": [[1, 2, 4, 9, 8, 5, 6, 7, 3], [7, 5, 3, 6, 1, 2, 8, 0, 0], [8, 9, 6, 3, 4, 7, 5, 2, 1], [0, 8, 0, 5, 0, 0, 0, 1, 0], [9, 0, 5, 1, 2, 0, 4, 8, 7], [0, 1, 0, 4, 0, 8, 0, 0, 5], [5, 0, 1, 8, 3, 9, 7, 0, 2], [0, 0, 8, 0, 5, 1, 9, 3, 0], [0, 0, 9, 0, 6, 4, 1, 5, 8]],
    "test_grid15": [[3, 7, 6, 2, 5, 0, 0, 1, 9], [0, 2, 4, 7, 1, 9, 6, 0, 3], [0, 9, 1, 0, 6, 3, 2, 0, 7], [4, 3, 0, 1, 9, 0, 7, 6, 2], [9, 0, 0, 0, 2, 7, 0, 3, 0], [2, 0, 7, 0, 3, 0, 9, 0, 0], [7, 5, 9, 3, 8, 6, 1, 2, 4], [6, 4, 2, 5, 7, 1, 3, 9, 8], [1, 8, 3, 9, 4, 2, 5, 7, 6]],
    "test_grid16":[[8, 0, 0, 4, 0, 0, 5, 0, 0], [4, 0, 0, 8, 1, 0, 3, 0, 2], [9, 0, 0, 7, 0, 0, 4, 0, 0], [3, 2, 9, 6, 5, 8, 1, 4, 7], [5, 1, 0, 2, 0, 4, 0, 6, 3], [0, 4, 0, 3, 0, 1, 2, 5, 0], [0, 0, 3, 0, 4, 7, 0, 2, 5], [2, 0, 4, 5, 6, 3, 7, 0, 1], [0, 0, 5, 0, 8, 2, 6, 3, 4]],
    "test_grid17":[[0, 4, 2, 7, 3, 0, 9, 6, 1], [0, 0, 0, 6, 9, 0, 2, 8, 4], [0, 0, 0, 0, 4, 0, 5, 7, 3], [0, 0, 6, 5, 2, 3, 8, 4, 9], [2, 8, 3, 4, 7, 9, 1, 5, 6], [9, 5, 4, 1, 8, 6, 7, 3, 2], [0, 0, 0, 0, 5, 4, 3, 1, 0], [0, 0, 5, 0, 6, 0, 4, 2, 0], [4, 0, 8, 0, 1, 0, 6, 9, 5]],
    "test_grid18":[[3, 1, 0, 9, 0, 2, 5, 0, 7], [0, 0, 4, 7, 8, 5, 1, 3, 6], [0, 7, 5, 3, 0, 1, 0, 0, 0], [0, 5, 0, 1, 7, 4, 3, 0, 2], [0, 3, 7, 2, 9, 8, 0, 5, 0], [0, 0, 0, 6, 5, 3, 0, 7, 0], [7, 6, 2, 0, 3, 9, 0, 1, 0], [0, 8, 3, 0, 1, 7, 0, 0, 0], [5, 0, 1, 0, 2, 6, 7, 0, 3]],
    "test_grid19":[[2, 1, 0, 0, 6, 0, 0, 7, 8], [0, 0, 7, 8, 2, 1, 0, 0, 4], [4, 0, 0, 5, 3, 7, 0, 1, 2], [0, 2, 0, 0, 5, 8, 4, 6, 0], [0, 0, 0, 2, 0, 6, 0, 0, 0], [7, 5, 6, 3, 0, 0, 2, 8, 1], [3, 9, 1, 6, 8, 2, 7, 4, 5], [0, 0, 0, 0, 0, 3, 1, 2, 6], [6, 7, 2, 0, 0, 5, 8, 3, 9]],
    "test_grid20":[[1, 8, 0, 5, 3, 0, 0, 0, 0], [9, 3, 0, 1, 7, 0, 0, 0, 8], [7, 0, 2, 8, 9, 6, 0, 1, 0], [0, 0, 7, 4, 0, 0, 0, 0, 9], [4, 9, 0, 3, 0, 0, 7, 0, 0], [8, 6, 3, 9, 2, 7, 1, 4, 5], [0, 0, 9, 6, 8, 1, 4, 0, 0], [3, 0, 0, 2, 4, 9, 0, 5, 0], [6, 0, 0, 7, 5, 3, 0, 0, 0]],
    "test_grid21":[[8, 6, 2, 3, 4, 1, 0, 0, 5], [9, 7, 3, 5, 2, 6, 0, 0, 0], [5, 1, 4, 8, 9, 7, 6, 2, 3], [4, 2, 1, 9, 0, 0, 5, 6, 7], [6, 3, 8, 2, 7, 5, 4, 1, 9], [7, 5, 9, 6, 1, 4, 2, 3, 8], [3, 9, 6, 1, 5, 0, 0, 0, 0], [0, 8, 7, 4, 0, 0, 0, 5, 0], [0, 4, 5, 7, 0, 0, 3, 0, 0]],
    "test_grid22":[[7, 1, 4, 3, 9, 8, 6, 5, 2], [3, 2, 5, 7, 4, 6, 1, 8, 9], [8, 9, 6, 2, 5, 1, 4, 7, 3], [0, 7, 0, 4, 0, 2, 3, 9, 0], [0, 0, 2, 9, 3, 5, 7, 0, 0], [9, 0, 3, 8, 0, 7, 2, 0, 0], [0, 0, 0, 6, 7, 3, 9, 2, 4], [2, 6, 9, 1, 8, 4, 5, 3, 7], [4, 3, 7, 5, 2, 9, 8, 6, 1]],
    "test_grid23":[[9, 0, 0, 1, 0, 8, 7, 4, 5], [0, 4, 0, 9, 7, 0, 2, 8, 6], [8, 7, 0, 0, 0, 4, 9, 1, 3], [0, 0, 4, 8, 1, 2, 3, 0, 7], [7, 0, 0, 0, 4, 0, 1, 0, 8], [0, 0, 8, 7, 0, 9, 5, 2, 4], [4, 8, 7, 2, 5, 1, 6, 3, 9], [0, 6, 3, 4, 9, 7, 8, 5, 0], [0, 0, 0, 0, 8, 0, 4, 7, 0]],
    "test_grid24":[[8, 6, 2, 3, 0, 9, 0, 0, 1], [5, 1, 3, 8, 2, 0, 0, 9, 0], [7, 9, 4, 1, 5, 6, 2, 8, 3], [1, 5, 8, 0, 0, 0, 0, 0, 9], [3, 7, 9, 5, 1, 8, 0, 0, 2], [2, 4, 6, 7, 9, 3, 8, 1, 5], [6, 2, 5, 4, 0, 1, 9, 0, 8], [9, 3, 7, 0, 8, 5, 1, 0, 4], [4, 8, 1, 9, 0, 0, 0, 0, 0]],
    "test_grid25":[[0, 4, 2, 3, 1, 0, 9, 7, 6], [9, 3, 0, 0, 4, 0, 2, 8, 1], [0, 6, 0, 0, 0, 9, 4, 5, 3], [0, 0, 3, 0, 0, 0, 5, 2, 0], [7, 0, 0, 0, 0, 0, 0, 6, 0], [0, 0, 6, 0, 9, 0, 0, 1, 0], [0, 0, 9, 4, 6, 7, 1, 3, 8], [6, 1, 4, 0, 3, 0, 7, 9, 5], [3, 7, 8, 9, 5, 1, 6, 4, 2]],
    "test_grid26":[[8, 9, 3, 6, 5, 4, 1, 2, 7], [4, 7, 5, 8, 2, 1, 3, 6, 9], [6, 1, 2, 9, 7, 3, 8, 4, 5], [2, 3, 0, 0, 1, 9, 5, 8, 0], [0, 0, 1, 0, 8, 5, 0, 0, 0], [5, 0, 0, 3, 6, 2, 0, 7, 1], [3, 5, 4, 2, 9, 6, 7, 1, 8], [0, 0, 0, 0, 4, 7, 0, 0, 0], [0, 2, 0, 0, 3, 8, 0, 0, 0]],
    "test_grid27":[[1, 2, 4, 9, 8, 5, 6, 7, 3], [7, 5, 3, 6, 1, 2, 8, 0, 0], [8, 9, 6, 3, 4, 7, 5, 2, 1], [4, 8, 0, 5, 0, 0, 0, 1, 0], [9, 0, 5, 1, 2, 0, 4, 8, 7], [0, 1, 0, 4, 0, 8, 0, 0, 5], [5, 0, 1, 8, 3, 9, 7, 0, 2], [0, 0, 8, 0, 5, 1, 9, 3, 0], [0, 0, 9, 0, 6, 4, 1, 5, 8]]


    }

import json
json.dumps(templates)
`;

    const templatesJson = await pyodide.runPythonAsync(templateCode);
    templates = JSON.parse(templatesJson);
    
    // Populate template selector
    const select = document.getElementById('template-select');
    for (const name of Object.keys(templates)) {
        const option = document.createElement('option');
        option.value = name;
        option.textContent = name.replace('test_grid', 'Puzzle ');
        select.appendChild(option);
    }
}

function setupEventListeners() {
    document.getElementById('solve-btn').addEventListener('click', solvePuzzle);
    document.getElementById('clear-btn').addEventListener('click', clearGrid);
    document.getElementById('random-btn').addEventListener('click', loadRandomPuzzle);
    document.getElementById('template-select').addEventListener('change', loadTemplate);
    document.getElementById('clear-log-btn').addEventListener('click', clearLog);
    document.getElementById('export-log-btn').addEventListener('click', exportLog);
}

function getGridValues() {
    const inputs = document.querySelectorAll('.sudoku-grid input');
    return Array.from(inputs).map(input => {
        const val = parseInt(input.value);
        return isNaN(val) ? 0 : val;
    });
}

function setGridValues(values, markOriginal = true) {
    const inputs = document.querySelectorAll('.sudoku-grid input');
    inputs.forEach((input, i) => {
        input.value = values[i] || '';
        input.classList.remove('original', 'solved', 'invalid');
        if (markOriginal && values[i]) {
            input.classList.add('original');
        }
    });
}

function clearGrid() {
    const inputs = document.querySelectorAll('.sudoku-grid input');
    inputs.forEach(input => {
        input.value = '';
        input.classList.remove('original', 'solved', 'invalid');
    });
    document.getElementById('template-select').value = '';
    document.getElementById('results-panel').innerHTML = '<p class="placeholder">Solve a puzzle to see results</p>';
}

function loadTemplate() {
    const select = document.getElementById('template-select');
    const name = select.value;
    if (name && templates[name]) {
        const flat = templates[name].flat();
        setGridValues(flat);
    }
}

function loadRandomPuzzle() {
    const names = Object.keys(templates);
    const randomName = names[Math.floor(Math.random() * names.length)];
    const flat = templates[randomName].flat();
    setGridValues(flat);
    document.getElementById('template-select').value = randomName;
}

async function solvePuzzle() {
    if (!pyodide) {
        alert('Python environment not loaded yet!');
        return;
    }
    
    const gridValues = getGridValues();
    const templateName = document.getElementById('template-select').value || 'Custom';
    
    // Mark original values
    const inputs = document.querySelectorAll('.sudoku-grid input');
    inputs.forEach((input, i) => {
        input.classList.remove('solved', 'invalid');
        if (gridValues[i]) {
            input.classList.add('original');
        }
    });
    
    document.querySelector('.grid-container').classList.add('solving');
    document.getElementById('solve-btn').disabled = true;
    updateStatus('Solving...', 'loading');
    
    try {
        // Run solver in Python
        pyodide.globals.set('input_grid', gridValues);
        const resultJson = await pyodide.runPythonAsync(`
import json
result = solve_from_flat(list(input_grid))
json.dumps(result)
`);
        
        const result = JSON.parse(resultJson);
        
        document.querySelector('.grid-container').classList.remove('solving');
        
        if (result.success) {
            // Display solution
            inputs.forEach((input, i) => {
                if (!gridValues[i] && result.solution[i]) {
                    input.value = result.solution[i];
                    input.classList.add('solved');
                }
            });
            
            updateStatus('Solved!', 'ready');
            showResults(result);
            
            // Add to log
            addLogEntry({
                template: templateName,
                emptyCells: result.empty_cells,
                time: result.time,
                guesses: result.guesses,
                success: true
            });
        } else {
            updateStatus('Could not solve puzzle', 'error');
            showResults(result);
            
            addLogEntry({
                template: templateName,
                emptyCells: result.empty_cells,
                time: result.time,
                guesses: result.guesses,
                success: false
            });
        }
    } catch (error) {
        console.error(error);
        updateStatus('Error: ' + error.message, 'error');
        document.querySelector('.grid-container').classList.remove('solving');
    }
    
    document.getElementById('solve-btn').disabled = false;
}

function showResults(result) {
    const panel = document.getElementById('results-panel');
    panel.innerHTML = `
        <div class="result-item">
            <span class="result-label">Status</span>
            <span class="result-value ${result.success ? '' : 'error'}">${result.success ? '✓ Solved' : '✗ Failed'}</span>
        </div>
        <div class="result-item">
            <span class="result-label">Empty Cells</span>
            <span class="result-value">${result.empty_cells}</span>
        </div>
        <div class="result-item">
            <span class="result-label">Solve Time</span>
            <span class="result-value">${result.time} seconds</span>
        </div>
        <div class="result-item">
            <span class="result-label">Guesses Made</span>
            <span class="result-value">${result.guesses}</span>
        </div>
    `;
}

function addLogEntry(entry) {
    logCounter++;
    const logEntry = {
        id: logCounter,
        timestamp: new Date().toLocaleString(),
        ...entry
    };
    logEntries.push(logEntry);
    saveLogToStorage();
    renderLog();
}

function renderLog() {
    const tbody = document.getElementById('log-body');
    tbody.innerHTML = '';
    
    logEntries.slice().reverse().forEach(entry => {
        const row = document.createElement('tr');
        row.innerHTML = `
            <td>${entry.id}</td>
            <td>${entry.timestamp}</td>
            <td>${entry.template}</td>
            <td>${entry.emptyCells}</td>
            <td>${entry.time}</td>
            <td>${entry.guesses}</td>
            <td class="${entry.success ? 'status-success' : 'status-failed'}">${entry.success ? '✓ Success' : '✗ Failed'}</td>
        `;
        tbody.appendChild(row);
    });
}

function clearLog() {
    if (confirm('Clear all log entries?')) {
        logEntries = [];
        logCounter = 0;
        saveLogToStorage();
        renderLog();
    }
}

function exportLog() {
    if (logEntries.length === 0) {
        alert('No log entries to export');
        return;
    }
    
    const headers = ['#', 'Timestamp', 'Template', 'Empty Cells', 'Solve Time (s)', 'Guesses', 'Status'];
    const rows = logEntries.map(e => [
        e.id, e.timestamp, e.template, e.emptyCells, e.time, e.guesses, e.success ? 'Success' : 'Failed'
    ]);
    
    const csv = [headers, ...rows].map(row => row.join(',')).join('\n');
    const blob = new Blob([csv], { type: 'text/csv' });
    const url = URL.createObjectURL(blob);
    
    const a = document.createElement('a');
    a.href = url;
    a.download = `sudoku_log_${new Date().toISOString().slice(0,10)}.csv`;
    a.click();
    
    URL.revokeObjectURL(url);
}

function saveLogToStorage() {
    localStorage.setItem('sudokuLog', JSON.stringify({ entries: logEntries, counter: logCounter }));
}

function loadLogFromStorage() {
    const data = localStorage.getItem('sudokuLog');
    if (data) {
        const parsed = JSON.parse(data);
        logEntries = parsed.entries || [];
        logCounter = parsed.counter || 0;
    }
}

// Initialize on load
init();
