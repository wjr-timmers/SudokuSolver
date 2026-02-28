import time
import itertools
from collections import defaultdict
import random

VALUES = [1,2,3,4,5,6,7,8,9]
_BLOCK_COORDINATES_CACHE = None

def get_all_block_coordinates():
    """Generate coordinates for all 9 blocks. Cached after first call."""
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
    """Get block coordinates for a given cell position."""
    block_row = row_idx // 3
    block_col = col_idx // 3
    block_idx = block_row * 3 + block_col
    
    all_blocks = get_all_block_coordinates()
    return all_blocks[block_idx]

def print_grid(grid):
    for row in grid:
        print(f"{row}")
    return


def check_block(grid, temp_options, cell_coordinate, verbose=False):

    row_idx,col_idx = cell_coordinate
    coordinates = get_block_coordinates(row_idx, col_idx).copy()
    coordinates.remove((row_idx,col_idx))

    for i,j in coordinates:
        value = grid[i][j]
        if value in temp_options:
            temp_options.remove(value)
    
    return temp_options


def check_hidden(list_of_possibilities_in_block, option_grid, flag="None", verbose=False, hidden_quant=2):
    
    occurences = {}
    for v in VALUES:
        occurences[v] = {
        'count': 0, 
        'coordinates': []
    }

    #print(list_of_possibilities_in_block)
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
            #print(f"|------------- Hidden Quant Found! Numbers {numbers_hidden} at {coords}")

            coords = list(coords)

            for coordinate, values in list_of_possibilities_in_block:
                if coordinate in coords:
                    for v in values[:]:
                        if v in numbers_hidden:
                            continue
                        else:
                            if verbose:
                                type_name = {1: "single", 2: "pair", 3: "triple", 4: "quad"}.get(hidden_quant, "set")
                                print(f"|-0--0--0-- Removed {v} from cell {coordinate} (hidden {type_name}, {flag})")
                            values.remove(v)
                            if v in option_grid[coordinate[0]][coordinate[1]]:
                                option_grid[coordinate[0]][coordinate[1]].remove(v)


    return list_of_possibilities_in_block, option_grid


def check_naked(list_of_possibilities_in_block, option_grid, flag="None",verbose=False, naked_quant=2):

    naked_options= []
    
    # Optimize for naked singles (most common case)
    if naked_quant == 1:
        # For singles, just find all cells with exactly 1 possibility
        naked_options = [x for x in list_of_possibilities_in_block if len(x[1]) == 1]
    else:
        # For pairs/triples/quads, use combinations
        naked_quant_options = [x for x in list_of_possibilities_in_block if (len(x[1])==naked_quant)]

        for combo in itertools.combinations(naked_quant_options,naked_quant):
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
                        if verbose:
                            type_name = {1: "single", 2: "pair", 3: "triple", 4: "quad"}.get(naked_quant, "set")
                            print(f"|---------- Removed {naked_value} from {values} in cell {coordinate} (naked {type_name}, {flag})")
                        values.remove(naked_value)
                        if naked_value in option_grid[coordinate[0]][coordinate[1]]:
                            option_grid[coordinate[0]][coordinate[1]].remove(naked_value)

    return list_of_possibilities_in_block,option_grid


def get_block_options(cell_coordinate, option_grid, verbose=False):
    row_idx,col_idx = cell_coordinate
    current_possibilities = option_grid[row_idx][col_idx]

    coordinates_block = get_block_coordinates(row_idx, col_idx)
    coordinates_col = [(i, col_idx) for i in range(0,9)]
    coordinates_row = [(row_idx,j) for j in range(0,9)]

    list_of_possibilities_block_total = []
    list_of_possibitlies_row_total = []
    list_of_possibitlies_col_total = []

    for i,j in coordinates_block:
        list_of_possibilities_block_total.append(((i,j),option_grid[i][j])) # Cell coordinates and possible values
    
    for i,j in coordinates_row:
        list_of_possibitlies_row_total.append(((i,j), option_grid[i][j]))

    for i,j in coordinates_col:
        list_of_possibitlies_col_total.append(((i,j), option_grid[i][j]))


    assert len(list_of_possibilities_block_total) == 9
    assert len(list_of_possibitlies_row_total) == 9
    assert len(list_of_possibitlies_col_total) == 9

    return list_of_possibilities_block_total, list_of_possibitlies_row_total, list_of_possibitlies_col_total


def check_if_fill_in(list_of_possibilities, cell_coordinate, option_grid, verbose=False):
    
    # Remove the cell itself from the list
    list_of_possibilities = [item for item in list_of_possibilities if item[0] != cell_coordinate]
    
    row_idx, col_idx = cell_coordinate
    current_possibilities = option_grid[row_idx][col_idx]
    
    # Early exit if current cell has no possibilities or is filled
    if not current_possibilities or current_possibilities == [0]:
        return None, option_grid
    
    # Collect all values that appear in neighboring cells (using set for O(1) lookup)
    neighbor_values = set()
    for coordinate, possibilities in list_of_possibilities:
        if possibilities != [0] and len(possibilities) > 0:
            neighbor_values.update(possibilities)
    
    # Find values in current_possibilities that don't appear in any neighbor
    unique_value = None
    unique_count = 0
    
    for num in current_possibilities:
        if num not in neighbor_values:
            unique_count += 1
            if unique_count > 1:
                # More than one unique value, not a hidden single
                return None, option_grid
            unique_value = num
    
    if unique_count == 1:
        if verbose:
            print('We have a unique cell!!')
        return unique_value, option_grid
    else:
        return None, option_grid


def check_block_options(option_grid, cell_coordinate, verbose=False):


    list_of_possibilities_block_total, list_of_possibitlies_row_total, list_of_possibitlies_col_total = get_block_options(cell_coordinate, option_grid, verbose=verbose)

    # Check the naked single, pairs, triples and quads
    for i in range(1,5):
        list_of_possibilities_block_total, option_grid = check_naked(list_of_possibilities_block_total, option_grid, flag="block",verbose=verbose, naked_quant=i)
        value_to_fill_in, option_grid = check_if_fill_in(list_of_possibilities_block_total, cell_coordinate, option_grid, verbose=False)
        if value_to_fill_in == None:
            pass
        else:
            if verbose:
                print("Returning from hidden")
            return value_to_fill_in, option_grid
        
        list_of_possibitlies_row_total, option_grid = check_naked(list_of_possibitlies_row_total, option_grid, flag="row",verbose=verbose, naked_quant=i)
        value_to_fill_in, option_grid = check_if_fill_in(list_of_possibitlies_row_total, cell_coordinate, option_grid, verbose=False)
        if value_to_fill_in == None:
            pass
        else:
            if verbose:
                print("Returning from hidden")
            return value_to_fill_in, option_grid
        
        list_of_possibitlies_col_total, option_grid = check_naked(list_of_possibitlies_col_total, option_grid, flag="col",verbose=verbose, naked_quant=i)
        value_to_fill_in, option_grid = check_if_fill_in(list_of_possibitlies_col_total, cell_coordinate, option_grid, verbose=False)
        if value_to_fill_in == None:
            pass
        else:
            if verbose:
                print("Returning from hidden")
            return value_to_fill_in, option_grid

        list_of_possibilities_block_total, option_grid = check_hidden(list_of_possibilities_block_total, option_grid, flag="block",verbose=verbose, hidden_quant=i)
        value_to_fill_in, option_grid = check_if_fill_in(list_of_possibilities_block_total, cell_coordinate, option_grid, verbose=False)
        if value_to_fill_in == None:
            pass
        else:
            if verbose:
                print("Returning from hidden")
            return value_to_fill_in, option_grid
        
        list_of_possibitlies_row_total, option_grid = check_hidden(list_of_possibitlies_row_total, option_grid, flag="row",verbose=verbose, hidden_quant=i)
        value_to_fill_in, option_grid = check_if_fill_in(list_of_possibitlies_row_total, cell_coordinate, option_grid, verbose=False)
        if value_to_fill_in == None:
            pass
        else:
            if verbose:
                print("Returning from hidden")
            return value_to_fill_in, option_grid
        
        list_of_possibitlies_col_total, option_grid = check_hidden(list_of_possibitlies_col_total, option_grid, flag="col",verbose=verbose, hidden_quant=i)
        value_to_fill_in, option_grid = check_if_fill_in(list_of_possibitlies_col_total, cell_coordinate, option_grid, verbose=False)
        if value_to_fill_in == None:
            pass
        else:
            if verbose:
                print("Returning from hidden")
            return value_to_fill_in, option_grid
        
    return None, option_grid


def get_grid_options(grid, verbose=False):
    options_grid = [[[0] for i in range(9)] for j in range(9)]

    for row_idx in range(len(grid)):
        for col_idx in range(len(grid[0])):

            cell_value = grid[row_idx][col_idx]
            
            possibilities = []

            # Check if value is already filled in, if not we proceed. 
            if cell_value != 0:
                options_grid[row_idx][col_idx] = [0]
                #print(f"Cell is already occupied ({row_idx},{col_idx})")
            else:
                for v in VALUES:
                    row = grid[row_idx]
                    col = [row[col_idx] for row in grid]
                    if (v not in row) and (v not in col):
                        possibilities.append(v)
                        possibilities = check_block(grid, possibilities,(row_idx,col_idx),verbose=verbose)
                
                #print(f"Possibilities are {possibilities} for cell ({row_idx},{col_idx})")
                options_grid[row_idx][col_idx] = possibilities

    return options_grid
    

def propagate_constraint(options_grid, row_idx, col_idx, fill_in_value):
    """Remove fill_in_value from all peers (same row, column, and block)."""
    # Remove from row
    for j in range(9):
        if fill_in_value in options_grid[row_idx][j]:
            options_grid[row_idx][j].remove(fill_in_value)
    
    # Remove from column
    for i in range(9):
        if fill_in_value in options_grid[i][col_idx]:
            options_grid[i][col_idx].remove(fill_in_value)
    
    # Remove from block
    block_row_start = (row_idx // 3) * 3
    block_col_start = (col_idx // 3) * 3
    for i in range(block_row_start, block_row_start + 3):
        for j in range(block_col_start, block_col_start + 3):
            if fill_in_value in options_grid[i][j]:
                options_grid[i][j].remove(fill_in_value)
    
    return options_grid


def solve_sudoku(grid, counter,total_to_fillin, verbose=False, recalculated_grid=None, guessed=False, guess_count=0):
    

        
    if counter == total_to_fillin:
        if verbose:
            print(f"We're done!")
        success = True
        return grid, success, guess_count

    if recalculated_grid == None:
        options_grid = get_grid_options(grid, verbose=verbose)
    else:
        options_grid = recalculated_grid


    if guessed and check_for_contradiction(options_grid):
        if verbose:
            print("Contradiction found! Backtracking...")
        return grid, False, guess_count

    fill_in_value = None
    for row_idx in range(len(grid)):
        for col_idx in range(len(grid[0])):

            cell_value = options_grid[row_idx][col_idx]

            if cell_value == [0]:
                pass
                # print('Cell already filled in')
            
            elif len(cell_value) == 1:
                fill_in_value = cell_value[0]
                #Only one option possible. 
            else:
                fill_in_value, options_grid = check_block_options(options_grid,(row_idx,col_idx), verbose=verbose)

            if fill_in_value == None:
                #print('Could not update cell')
                pass
            else:
                if verbose:
                    print(f"We can update cell ({row_idx},{col_idx}) with value {fill_in_value}")
                grid[row_idx][col_idx] = fill_in_value

                # Update the options grid
                options_grid[row_idx][col_idx] = [0]
                options_grid = propagate_constraint(options_grid, row_idx, col_idx, fill_in_value)
                counter += 1
                if verbose:
                    print(f"Solved {counter} out of {total_to_fillin}")
                return solve_sudoku(grid, counter,total_to_fillin, verbose=verbose, recalculated_grid=options_grid, guessed=False, guess_count=guess_count)
    
    if verbose:
        print("Not found a solution, we try guessing now...")


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
    
    # Try each option with backtracking
    for guess in options:
        if verbose:
            print(f"*****DIFFICULT******* - Guessing cell ({row_idx},{col_idx}) = {guess} (options were {options})")
        
        # Save state before guessing (deep copy)
        grid_backup = copy.deepcopy(grid)
        options_grid_backup = copy.deepcopy(options_grid)
        
        # Make the guess
        grid[row_idx][col_idx] = guess
        options_grid[row_idx][col_idx] = [0]
        options_grid = propagate_constraint(options_grid, row_idx, col_idx, guess)
        guess_count += 1
        
        # Recurse with guessed=True
        result_grid, success, guess_count = solve_sudoku(grid, counter + 1, total_to_fillin, verbose=verbose, recalculated_grid=options_grid, guessed=True, guess_count=guess_count)
        
        if success:
            return result_grid, True, guess_count
        else:
            # Restore state and try next option
            if verbose:
                print(f"Guess {guess} at ({row_idx},{col_idx}) failed, backtracking...")
            grid = grid_backup
            options_grid = options_grid_backup
    
    # All guesses failed
    return grid, False, guess_count


import copy

def check_for_contradiction(options_grid):
    """Check if any unfilled cell has no options left (empty list) - means we hit a dead end."""
    for row_idx in range(9):
        for col_idx in range(9):
            cell = options_grid[row_idx][col_idx]
            # If cell is not filled ([0]) and has no options, we have a contradiction
            if cell != [0] and len(cell) == 0:
                return True
    return False



if __name__ == "__main__":

    import templates as tpl

    test_grid = tpl.test_grid14
    start = time.time()
    counter = 0
    count_zeros = lambda grid: sum(row.count(0) for row in grid)
    total_to_fillin = count_zeros(test_grid)

    print("Initial grid:")
    print_grid(test_grid)
    print('---------')
    solution, success_flag, total_guesses = solve_sudoku(test_grid, counter, total_to_fillin, verbose=True)
    print('---------')
    if success_flag:
        print("Final solution:")
    else:
        print("How far we got:")

    if total_guesses >2 :
        print(f"Total guesses: {total_guesses}")
    print_grid(solution)
    print('---------')
    end = time.time()
    if success_flag:
        print(f"Solve took {round(end-start,5)} seconds")
        print(f"Total guesses: {total_guesses}")
