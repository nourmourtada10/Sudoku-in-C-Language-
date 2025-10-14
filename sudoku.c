/* ============================================================================
 * SUDOKU.C - Source File for Optimized Sudoku Game
 * 
 * Implementation of Sudoku game with:
 * - Dancing Links (DLX) Algorithm X solver
 * - GTK4 graphical interface
 * - Report-based difficulty formula
 * - Complete memory leak fixes
 * 
 * Compilation: gcc -std=c99 -O2 sudoku.c -o sudoku $(pkg-config --cflags --libs gtk4)
 * 
 * Author: Optimized Implementation
 * Date: 2025
 * ========================================================================== */

#include "sudoku.h"
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <stdio.h>
#include <limits.h>

/* ========== DIFFICULTY CALCULATION ========== */

/**
 * Calculate number of cells to remove based on difficulty level
 * Uses formula from report Section 3.2: clues(L) = clip(56 - 3*L, 24, 56)
 * Then converts to cells_to_remove = 81 - clues
 */
int calculate_cells_to_remove_for_difficulty(DifficultyLevel level) {
    // Formula from report: clues = clip(56 - 3*L, 24, 56)
    int target_clues = 56 - (3 * level);
    
    // Apply bounds: min 24 clues, max 56 clues
    if (target_clues < 24) target_clues = 24;
    if (target_clues > 56) target_clues = 56;
    
    // Convert to cells to remove
    int cells_to_remove = TOTAL_CELLS - target_clues;
    
    return cells_to_remove;
}

/**
 * Convert difficulty enum to user-friendly string
 */
const char *get_difficulty_display_name(DifficultyLevel difficulty) {
    switch (difficulty) {
        case DIFFICULTY_BEGINNER: return "Beginner";
        case DIFFICULTY_MEDIUM:   return "Medium";
        case DIFFICULTY_HARD:     return "Hard";
        case DIFFICULTY_EXPERT:   return "Expert";
        default:                  return "Unknown";
    }
}

/* ========== FILE I/O OPERATIONS ========== */

/**
 * Save current game state to file
 * Uses binary format for fast I/O
 */
void save_game_to_file(SudokuGameState *game) {
    if (!game) return;
    
    FILE *file = fopen(SAVE_FILE_PATH, "wb");
    if (file) {
        fwrite(game, sizeof(SudokuGameState), 1, file);
        fclose(file);
    }
}

/**
 * Load game state from file
 * Returns true if successful, false otherwise
 */
bool load_game_from_file(SudokuGameState *game) {
    if (!game) return false;
    
    FILE *file = fopen(SAVE_FILE_PATH, "rb");
    if (file) {
        size_t bytes_read = fread(game, sizeof(SudokuGameState), 1, file);
        fclose(file);
        return (bytes_read == 1);
    }
    return false;
}

/**
 * Check if a saved game exists
 */
bool check_saved_game_exists(void) {
    FILE *file = fopen(SAVE_FILE_PATH, "rb");
    if (file) {
        fclose(file);
        return true;
    }
    return false;
}

/* ========== SUDOKU LOGIC - VALIDATION ========== */

/**
 * Check if placing a number at given position is valid
 * Checks row, column, and 3x3 subgrid constraints
 * 
 * @param grid: The sudoku grid to check
 * @param row: Row index (0-8)
 * @param col: Column index (0-8)
 * @param number: Number to place (1-9)
 * @return: true if placement is valid
 */
bool is_placement_valid(int grid[GRID_SIZE][GRID_SIZE], int row, int col, int number) {
    // Check row and column constraints
    for (int i = 0; i < GRID_SIZE; i++) {
        if (grid[row][i] == number || grid[i][col] == number) {
            return false;
        }
    }
    
    // Check 3x3 subgrid constraint
    int subgrid_start_row = (row / SUBGRID_SIZE) * SUBGRID_SIZE;
    int subgrid_start_col = (col / SUBGRID_SIZE) * SUBGRID_SIZE;
    int subgrid_end_row = subgrid_start_row + SUBGRID_SIZE;
    int subgrid_end_col = subgrid_start_col + SUBGRID_SIZE;
    
    for (int i = subgrid_start_row; i < subgrid_end_row; i++) {
        for (int j = subgrid_start_col; j < subgrid_end_col; j++) {
            if (grid[i][j] == number) {
                return false;
            }
        }
    }
    
    return true;
}

/**
 * Validate if a filled cell violates sudoku rules
 * This checks if the current value creates conflicts
 */
bool is_cell_value_valid(int grid[GRID_SIZE][GRID_SIZE], int row, int col, int number) {
    // Check row and column (excluding current cell)
    for (int i = 0; i < GRID_SIZE; i++) {
        if (i != col && grid[row][i] == number) return false;
        if (i != row && grid[i][col] == number) return false;
    }
    
    // Check 3x3 subgrid (excluding current cell)
    int subgrid_start_row = (row / SUBGRID_SIZE) * SUBGRID_SIZE;
    int subgrid_start_col = (col / SUBGRID_SIZE) * SUBGRID_SIZE;
    int subgrid_end_row = subgrid_start_row + SUBGRID_SIZE;
    int subgrid_end_col = subgrid_start_col + SUBGRID_SIZE;
    
    for (int i = subgrid_start_row; i < subgrid_end_row; i++) {
        for (int j = subgrid_start_col; j < subgrid_end_col; j++) {
            if (!(i == row && j == col) && grid[i][j] == number) {
                return false;
            }
        }
    }
    
    return true;
}

/**
 * Check if grid is completely filled
 */
bool is_grid_complete(int grid[GRID_SIZE][GRID_SIZE]) {
    for (int i = 0; i < GRID_SIZE; i++) {
        for (int j = 0; j < GRID_SIZE; j++) {
            if (grid[i][j] == 0) {
                return false;
            }
        }
    }
    return true;
}

/* ========== SUDOKU GENERATION ========== */

/**
 * Copy one grid to another
 */
void copy_grid_data(int source[GRID_SIZE][GRID_SIZE], int destination[GRID_SIZE][GRID_SIZE]) {
    memcpy(destination, source, sizeof(int) * GRID_SIZE * GRID_SIZE);
}

/**
 * Recursively fill grid with valid numbers using backtracking
 * Uses randomization for variety
 */
bool fill_grid_recursively(int grid[GRID_SIZE][GRID_SIZE], int row, int col) {
    // Base case: reached end of grid
    if (row == GRID_SIZE) {
        return true;
    }
    
    // Calculate next position
    int next_row = row;
    int next_col = col + 1;
    if (next_col == GRID_SIZE) {
        next_row++;
        next_col = 0;
    }
    
    // Create randomized list of numbers 1-9
    int numbers[GRID_SIZE];
    for (int i = 0; i < GRID_SIZE; i++) {
        numbers[i] = i + 1;
    }
    
    // Fisher-Yates shuffle for randomization
    for (int i = GRID_SIZE - 1; i > 0; i--) {
        int j = rand() % (i + 1);
        int temp = numbers[i];
        numbers[i] = numbers[j];
        numbers[j] = temp;
    }
    
    // Try each number in random order
    for (int i = 0; i < GRID_SIZE; i++) {
        if (is_placement_valid(grid, row, col, numbers[i])) {
            grid[row][col] = numbers[i];
            
            if (fill_grid_recursively(grid, next_row, next_col)) {
                return true;
            }
            
            // Backtrack
            grid[row][col] = 0;
        }
    }
    
    return false;
}

/**
 * Generate a complete valid sudoku grid
 */
void generate_complete_sudoku_grid(int grid[GRID_SIZE][GRID_SIZE]) {
    // Initialize grid to zeros
    memset(grid, 0, sizeof(int) * GRID_SIZE * GRID_SIZE);
    
    // Fill using backtracking
    fill_grid_recursively(grid, 0, 0);
}

/**
 * Remove numbers from grid to create puzzle
 * Uses report formula for determining how many to remove
 */
void remove_numbers_from_grid(int grid[GRID_SIZE][GRID_SIZE], int cells_to_remove) {
    int removed_count = 0;
    
    while (removed_count < cells_to_remove) {
        int row = rand() % GRID_SIZE;
        int col = rand() % GRID_SIZE;
        
        // Only remove if cell has a number
        if (grid[row][col] != 0) {
            grid[row][col] = 0;
            removed_count++;
        }
    }
}

/* ========== DANCING LINKS ALGORITHM (DLX) ========== */

/**
 * Create and initialize a DLX node
 * All links point to self initially (circular list)
 */
DLXNode* create_dlx_node(void) {
    DLXNode *node = (DLXNode*)calloc(1, sizeof(DLXNode));
    if (!node) return NULL;
    
    // Initialize circular links pointing to self
    node->left_link = node;
    node->right_link = node;
    node->up_link = node;
    node->down_link = node;
    node->column_header = NULL;
    node->row_identifier = -1;
    node->column_size = 0;
    
    return node;
}

/**
 * Cover a column in DLX structure (remove from consideration)
 * This is the core operation of Algorithm X
 */
void cover_dlx_column(DLXNode *column) {
    // Remove column from header list
    column->right_link->left_link = column->left_link;
    column->left_link->right_link = column->right_link;
    
    // Remove all rows that have a 1 in this column
    for (DLXNode *row = column->down_link; row != column; row = row->down_link) {
        for (DLXNode *node = row->right_link; node != row; node = node->right_link) {
            node->down_link->up_link = node->up_link;
            node->up_link->down_link = node->down_link;
            node->column_header->column_size--;
        }
    }
}

/**
 * Uncover a column in DLX structure (restore it)
 * Reverse operation of cover - must be done in exact reverse order
 */
void uncover_dlx_column(DLXNode *column) {
    // Restore all rows in reverse order
    for (DLXNode *row = column->up_link; row != column; row = row->up_link) {
        for (DLXNode *node = row->left_link; node != row; node = node->left_link) {
            node->column_header->column_size++;
            node->down_link->up_link = node;
            node->up_link->down_link = node;
        }
    }
    
    // Restore column to header list
    column->right_link->left_link = column;
    column->left_link->right_link = column;
}

/**
 * Recursive search for exact cover solution (Algorithm X)
 * 
 * @param solver: DLX solver state
 * @param depth: Current recursion depth
 * @return: true if solution found
 */
bool search_dlx_solution(DLXSolverState *solver, int depth) {
    solver->game_reference->algorithm_steps++;
    
    // Base case: all columns covered - solution found
    if (solver->root_header->right_link == solver->root_header) {
        solver->solution_length = depth;
        return true;
    }
    
    // Choose column with minimum size (heuristic for efficiency)
    DLXNode *selected_column = NULL;
    int minimum_size = INT_MAX;
    
    for (DLXNode *col = solver->root_header->right_link; 
         col != solver->root_header; 
         col = col->right_link) {
        if (col->column_size < minimum_size) {
            minimum_size = col->column_size;
            selected_column = col;
            
            // Optimization: if size is 0 or 1, no need to search further
            if (minimum_size <= 1) break;
        }
    }
    
    // No valid column found
    if (selected_column == NULL || selected_column->column_size == 0) {
        return false;
    }
    
    cover_dlx_column(selected_column);
    
    // Try each row in the selected column
    for (DLXNode *row = selected_column->down_link; 
         row != selected_column; 
         row = row->down_link) {
        
        solver->solution_rows[depth] = row->row_identifier;
        
        // Cover all columns in this row
        for (DLXNode *node = row->right_link; node != row; node = node->right_link) {
            cover_dlx_column(node->column_header);
        }
        
        // Recurse
        if (search_dlx_solution(solver, depth + 1)) {
            return true;
        }
        
        // Backtrack: uncover all columns in reverse order
        for (DLXNode *node = row->left_link; node != row; node = node->left_link) {
            uncover_dlx_column(node->column_header);
        }
    }
    
    uncover_dlx_column(selected_column);
    return false;
}

/**
 * Initialize DLX solver structure for given sudoku grid
 * Creates the constraint matrix for exact cover problem
 * 
 * Constraints (324 total):
 * - 81 for cell coverage (each cell has exactly one number)
 * - 81 for row numbers (each row has each number once)
 * - 81 for column numbers (each column has each number once)  
 * - 81 for box numbers (each 3x3 box has each number once)
 */
void initialize_dlx_solver(DLXSolverState *solver, int grid[GRID_SIZE][GRID_SIZE]) {
    solver->root_header = create_dlx_node();
    solver->solution_length = 0;
    
    // Create column headers (324 constraints)
    DLXNode *previous_column = solver->root_header;
    
    for (int i = 0; i < TOTAL_CONSTRAINTS; i++) {
        DLXNode *column = create_dlx_node();
        solver->constraint_columns[i] = column;
        column->column_size = 0;
        column->column_header = column;  // Column headers point to themselves
        
        // Link into horizontal list
        previous_column->right_link = column;
        column->left_link = previous_column;
        previous_column = column;
    }
    
    // Complete the circular list
    previous_column->right_link = solver->root_header;
    solver->root_header->left_link = previous_column;
    
    // Add rows for each possible (row, col, number) combination
    for (int row = 0; row < GRID_SIZE; row++) {
        for (int col = 0; col < GRID_SIZE; col++) {
            
            // If cell is filled, only add row for that number
            int start_num = (grid[row][col] != 0) ? grid[row][col] : 1;
            int end_num = (grid[row][col] != 0) ? grid[row][col] : 9;
            
            for (int num = start_num; num <= end_num; num++) {
                // Calculate row identifier
                int row_id = row * 81 + col * 9 + (num - 1);
                
                // Calculate box index
                int box_index = (row / 3) * 3 + (col / 3);
                
                // Four constraints for each (row, col, num) combination
                int constraint_indices[4] = {
                    row * 9 + col,                      // Cell constraint
                    81 + row * 9 + (num - 1),          // Row-number constraint
                    162 + col * 9 + (num - 1),         // Column-number constraint
                    243 + box_index * 9 + (num - 1)    // Box-number constraint
                };
                
                // Create nodes for each constraint
                DLXNode *previous_node = NULL;
                
                for (int i = 0; i < 4; i++) {
                    DLXNode *node = create_dlx_node();
                    node->row_identifier = row_id;
                    node->column_header = solver->constraint_columns[constraint_indices[i]];
                    
                    // Link vertically into column
                    node->up_link = node->column_header->up_link;
                    node->down_link = node->column_header;
                    node->column_header->up_link->down_link = node;
                    node->column_header->up_link = node;
                    node->column_header->column_size++;
                    
                    // Link horizontally into row
                    if (previous_node == NULL) {
                        // First node in row - point to self
                        node->left_link = node;
                        node->right_link = node;
                    } else {
                        // Link into circular row list
                        node->left_link = previous_node;
                        node->right_link = previous_node->right_link;
                        previous_node->right_link->left_link = node;
                        previous_node->right_link = node;
                    }
                    
                    previous_node = node;
                }
            }
        }
    }
}

/**
 * Free all memory allocated for DLX solver
 * CRITICAL for preventing memory leaks
 */
void free_dlx_solver_memory(DLXSolverState *solver) {
    if (!solver) return;
    
    // Free all nodes in each column
    for (int i = 0; i < TOTAL_CONSTRAINTS; i++) {
        if (!solver->constraint_columns[i]) continue;
        
        DLXNode *column = solver->constraint_columns[i];
        DLXNode *row = column->down_link;
        
        // Free each row in this column
        while (row != column) {
            DLXNode *next_row = row->down_link;
            
            // Free all nodes in this row (except first, which we're currently on)
            DLXNode *node = row->right_link;
            while (node != row) {
                DLXNode *next_node = node->right_link;
                free(node);
                node = next_node;
            }
            
            // Free the first node in the row
            free(row);
            row = next_row;
        }
        
        // Free the column header
        free(column);
    }
    
    // Free root header
    if (solver->root_header) {
        free(solver->root_header);
    }
}

/**
 * Solve sudoku puzzle using DLX algorithm
 * 
 * @param game: Game state for tracking steps
 * @param grid: Grid to solve (modified in place)
 * @return: true if solution found
 */
bool solve_sudoku_with_dlx(SudokuGameState *game, int grid[GRID_SIZE][GRID_SIZE]) {
    DLXSolverState solver;
    solver.game_reference = game;
    
    // Initialize the DLX structure
    initialize_dlx_solver(&solver, grid);
    
    // Search for solution
    bool solution_found = search_dlx_solution(&solver, 0);
    
    if (solution_found) {
        // Extract solution from solver state
        for (int i = 0; i < solver.solution_length; i++) {
            int row_id = solver.solution_rows[i];
            int row = row_id / 81;
            int col = (row_id % 81) / 9;
            int num = (row_id % 9) + 1;
            grid[row][col] = num;
        }
    }
    
    // CRITICAL: Free all allocated memory
    free_dlx_solver_memory(&solver);
    
    return solution_found;
}

/* ========== CAIRO DRAWING ========== */

/**
 * Draw the sudoku grid using Cairo
 * Handles highlighting, colors, and number display
 */
void draw_sudoku_grid(GtkDrawingArea *area, cairo_t *cr, int width, int height, gpointer data) {
    UIState *ui = (UIState *)data;
    SudokuGameState *game = ui->game_state;

    double margin = 20;
    double grid_size = (width < height ? width : height) - 2 * margin;
    double cell_size = grid_size / GRID_SIZE;
    double start_x = (width - grid_size) / 2;
    double start_y = (height - grid_size) / 2;

    // White background
    cairo_set_source_rgb(cr, 1, 1, 1);
    cairo_paint(cr);

    // Draw cell backgrounds with highlighting
    for (int row = 0; row < GRID_SIZE; row++) {
        for (int col = 0; col < GRID_SIZE; col++) {
            bool should_highlight = false;
            bool should_highlight_number = false;

            // Highlight selected row/column/box
            if (ui->currently_selected_row >= 0 && ui->currently_selected_col >= 0) {
                if (row == ui->currently_selected_row || 
                    col == ui->currently_selected_col ||
                    (row / SUBGRID_SIZE == ui->currently_selected_row / SUBGRID_SIZE && 
                     col / SUBGRID_SIZE == ui->currently_selected_col / SUBGRID_SIZE)) {
                    should_highlight = true;
                }
            }

            // Highlight cells with same number as selected
            int selected_num = ui->currently_selected_number;
            if (selected_num > 0 && game->current_grid[row][col] == selected_num) {
                should_highlight_number = true;
            }

            // Draw cell background
            if (should_highlight_number) {
                cairo_set_source_rgb(cr, 0.71, 0.86, 1.0);  // Blue highlight
                cairo_rectangle(cr, start_x + col * cell_size, 
                              start_y + row * cell_size, cell_size, cell_size);
                cairo_fill(cr);
            } else if (should_highlight) {
                cairo_set_source_rgb(cr, 0.91, 0.94, 1.0);  // Light blue highlight
                cairo_rectangle(cr, start_x + col * cell_size, 
                              start_y + row * cell_size, cell_size, cell_size);
                cairo_fill(cr);
            }
        }
    }

    // Draw grid lines
    cairo_set_source_rgb(cr, 0, 0, 0);
    for (int i = 0; i <= GRID_SIZE; i++) {
        // Thicker lines for 3x3 boxes
        cairo_set_line_width(cr, (i % 3 == 0) ? 3.0 : 1.0);
        
        // Horizontal lines
        cairo_move_to(cr, start_x, start_y + i * cell_size);
        cairo_line_to(cr, start_x + grid_size, start_y + i * cell_size);
        cairo_stroke(cr);
        
        // Vertical lines
        cairo_move_to(cr, start_x + i * cell_size, start_y);
        cairo_line_to(cr, start_x + i * cell_size, start_y + grid_size);
        cairo_stroke(cr);
    }

    // Draw numbers
    cairo_select_font_face(cr, "Sans", CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_BOLD);
    cairo_set_font_size(cr, cell_size * 0.5);

    for (int row = 0; row < GRID_SIZE; row++) {
        for (int col = 0; col < GRID_SIZE; col++) {
            int value = game->current_grid[row][col];
            if (value == 0) continue;
            
            char number_str[2];
            number_str[0] = '0' + value;
            number_str[1] = '\0';

            cairo_text_extents_t extents;
            cairo_text_extents(cr, number_str, &extents);

            // Center the text in the cell
            double x = start_x + col * cell_size + (cell_size - extents.width) / 2 - extents.x_bearing;
            double y = start_y + row * cell_size + (cell_size - extents.height) / 2 - extents.y_bearing;

            // Color based on cell type
            if (game->initial_grid[row][col] != 0) {
                // Original numbers in black
                cairo_set_source_rgb(cr, 0, 0, 0);
            } else if (game->validation_status[row][col] == 2) {
                // Invalid numbers in red
                cairo_set_source_rgb(cr, 0.9, 0.1, 0.1);
            } else {
                // User-entered valid numbers in blue
                cairo_set_source_rgb(cr, 0.2, 0.2, 0.8);
            }

            cairo_move_to(cr, x, y);
            cairo_show_text(cr, number_str);
        }
    }

    // Draw outer border
    cairo_set_source_rgb(cr, 0, 0, 0);
    cairo_set_line_width(cr, 4.0);
    cairo_rectangle(cr, start_x, start_y, grid_size, grid_size);
    cairo_stroke(cr);
}

/**
 * Handle mouse click on grid
 * Converts screen coordinates to grid cell coordinates
 */
gboolean handle_grid_click(GtkGestureClick *gesture, int n_press, double x, double y, gpointer data) {
    GtkWidget *widget = gtk_event_controller_get_widget(GTK_EVENT_CONTROLLER(gesture));
    int width = gtk_widget_get_width(widget);
    int height = gtk_widget_get_height(widget);
    UIState *ui = (UIState *)data;

    double margin = 20;
    double grid_size = (width < height ? width : height) - 2 * margin;
    double cell_size = grid_size / GRID_SIZE;
    double start_x = (width - grid_size) / 2;
    double start_y = (height - grid_size) / 2;

    // Check if click is within grid bounds
    if (x < start_x || y < start_y || x > start_x + grid_size || y > start_y + grid_size) {
        return FALSE;
    }

    // Convert to grid coordinates
    int col = (x - start_x) / cell_size;
    int row = (y - start_y) / cell_size;

    ui->currently_selected_row = row;
    ui->currently_selected_col = col;
    ui->currently_selected_number = (ui->game_state->current_grid[row][col] > 0) 
                                    ? ui->game_state->current_grid[row][col] : -1;

    gtk_widget_queue_draw(widget);
    return TRUE;
}

/* ========== DIALOG FUNCTIONS ========== */

/**
 * Show informational dialog
 */
void show_information_dialog(GtkWindow *parent, const char *title, const char *message) {
    GtkWidget *dialog = gtk_dialog_new_with_buttons(
        title,
        parent,
        GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT,
        "OK", GTK_RESPONSE_OK,
        NULL
    );
    
    GtkWidget *content = gtk_dialog_get_content_area(GTK_DIALOG(dialog));
    GtkWidget *label = gtk_label_new(message);
    gtk_widget_set_margin_start(label, 20);
    gtk_widget_set_margin_end(label, 20);
    gtk_widget_set_margin_top(label, 20);
    gtk_widget_set_margin_bottom(label, 20);
    gtk_box_append(GTK_BOX(content), label);
    
    g_signal_connect(dialog, "response", G_CALLBACK(gtk_window_destroy), NULL);
    gtk_window_present(GTK_WINDOW(dialog));
}

/**
 * Confirmation dialog data structure
 */
typedef struct {
    void (*callback_function)(UIState*);
    UIState *ui_state;
} ConfirmDialogData;

/**
 * Handle confirmation dialog response
 */
static void handle_confirm_dialog_response(GtkDialog *dialog, int response, gpointer user_data) {
    ConfirmDialogData *data = (ConfirmDialogData*)user_data;
    
    if (response == GTK_RESPONSE_YES && data->callback_function) {
        data->callback_function(data->ui_state);
    }
    
    gtk_window_destroy(GTK_WINDOW(dialog));
}

/**
 * Show confirmation dialog with Yes/No buttons
 */
void show_confirmation_dialog(UIState *ui, const char *title, 
                             const char *message, void (*callback)(UIState*)) {
    GtkWidget *dialog = gtk_dialog_new_with_buttons(
        title,
        ui->main_window,
        GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT,
        "No", GTK_RESPONSE_NO,
        "Yes", GTK_RESPONSE_YES,
        NULL
    );
    
    GtkWidget *content = gtk_dialog_get_content_area(GTK_DIALOG(dialog));
    GtkWidget *label = gtk_label_new(message);
    gtk_widget_set_margin_start(label, 20);
    gtk_widget_set_margin_end(label, 20);
    gtk_widget_set_margin_top(label, 20);
    gtk_widget_set_margin_bottom(label, 20);
    gtk_box_append(GTK_BOX(content), label);
    
    // Allocate callback data (freed on dialog destroy)
    ConfirmDialogData *data = g_new(ConfirmDialogData, 1);
    data->callback_function = callback;
    data->ui_state = ui;
    
    g_signal_connect(dialog, "response", G_CALLBACK(handle_confirm_dialog_response), data);
    g_signal_connect_swapped(dialog, "destroy", G_CALLBACK(g_free), data);
    gtk_window_present(GTK_WINDOW(dialog));
}

/* ========== UI HELPER FUNCTIONS ========== */

/**
 * Enable or disable number pad buttons
 */
void set_number_pad_sensitivity(UIState *ui, bool is_sensitive) {
    for (int i = 1; i <= 9; i++) {
        if (ui->number_buttons[i]) {
            gtk_widget_set_sensitive(ui->number_buttons[i], is_sensitive);
        }
    }
}

/**
 * Timer callback - called every second
 * Updates timer display and checks for game completion
 */
gboolean timer_tick_callback(gpointer data) {
    UIState *ui = (UIState *)data;
    if (!ui || !ui->game_state) return G_SOURCE_REMOVE;
    if (ui->game_state->is_game_over) return G_SOURCE_REMOVE;

    ui->game_state->elapsed_seconds++;
    int seconds = ui->game_state->elapsed_seconds % 60;
    int minutes = (ui->game_state->elapsed_seconds / 60) % 60;
    int hours = ui->game_state->elapsed_seconds / 3600;

    char buffer[64];
    if (hours > 0) {
        snprintf(buffer, sizeof(buffer), "%02d:%02d:%02d", hours, minutes, seconds);
    } else {
        snprintf(buffer, sizeof(buffer), "%02d:%02d", minutes, seconds);
    }
    gtk_label_set_text(GTK_LABEL(ui->timer_display_label), buffer);

    // Check for completion
    if (is_grid_complete(ui->game_state->current_grid)) {
        bool all_valid = true;
        
        for (int i = 0; i < GRID_SIZE && all_valid; i++) {
            for (int j = 0; j < GRID_SIZE; j++) {
                if (ui->game_state->current_grid[i][j] != 0 && 
                    !is_cell_value_valid(ui->game_state->current_grid, i, j, 
                                        ui->game_state->current_grid[i][j])) {
                    all_valid = false;
                    break;
                }
            }
        }
        
        if (all_valid) {
            char status[128];
            snprintf(status, sizeof(status), 
                    "Puzzle solved! Time %02d:%02d — Score: %d", 
                    minutes, seconds, ui->game_state->player_score);
            gtk_label_set_text(GTK_LABEL(ui->status_message_label), status);
            
            ui->game_state->is_game_over = true;
            set_number_pad_sensitivity(ui, false);
            
            if (ui->timer_source_id) {
                g_source_remove(ui->timer_source_id);
                ui->timer_source_id = 0;
            }
            
            save_game_to_file(ui->game_state);
            
            char message[256];
            snprintf(message, sizeof(message), 
                    "Congratulations! You solved the puzzle in %02d:%02d with a score of %d points!", 
                    minutes, seconds, ui->game_state->player_score);
            show_information_dialog(ui->main_window, "Puzzle Complete!", message);
            
            return G_SOURCE_REMOVE;
        }
    }

    // Check for game over (too many mistakes)
    if (ui->game_state->mistake_count >= MAX_MISTAKES_ALLOWED) {
        gtk_label_set_text(GTK_LABEL(ui->status_message_label), 
                          "Game Over — Too many mistakes!");
        ui->game_state->is_game_over = true;
        set_number_pad_sensitivity(ui, false);
        save_game_to_file(ui->game_state);
        show_information_dialog(ui->main_window, "Game Over", 
                               "You've made too many mistakes! Try again or start a new game.");
        return G_SOURCE_REMOVE;
    }

    return G_SOURCE_CONTINUE;
}

/* ========== GAME ACTION HANDLERS ========== */

/**
 * Handle clear cell button click
 */
void handle_clear_cell_click(GtkButton *button, UIState *ui) {
    if (ui->currently_selected_row != -1 && ui->currently_selected_col != -1) {
        if (ui->game_state->initial_grid[ui->currently_selected_row][ui->currently_selected_col] == 0) {
            ui->game_state->current_grid[ui->currently_selected_row][ui->currently_selected_col] = 0;
            ui->game_state->validation_status[ui->currently_selected_row][ui->currently_selected_col] = 0;
            save_game_to_file(ui->game_state);
            refresh_user_interface(ui);
            gtk_label_set_text(GTK_LABEL(ui->status_message_label), "Cell cleared");
            ui->currently_selected_number = -1;
        } else {
            gtk_label_set_text(GTK_LABEL(ui->status_message_label), "Cannot clear original cells!");
        }
    } else {
        gtk_label_set_text(GTK_LABEL(ui->status_message_label), "Please select a cell first!");
    }
}

/**
 * Handle hint button click
 */
void handle_hint_click(GtkButton *button, UIState *ui) {
    if (ui->currently_selected_row != -1 && ui->currently_selected_col != -1) {
        if (ui->game_state->initial_grid[ui->currently_selected_row][ui->currently_selected_col] != 0) {
            gtk_label_set_text(GTK_LABEL(ui->status_message_label), "This is an original cell!");
            return;
        }
        
        if (ui->game_state->current_grid[ui->currently_selected_row][ui->currently_selected_col] == 0) {
            ui->game_state->current_grid[ui->currently_selected_row][ui->currently_selected_col] =
                ui->game_state->solution_grid[ui->currently_selected_row][ui->currently_selected_col];
            ui->game_state->validation_status[ui->currently_selected_row][ui->currently_selected_col] = 1;
            gtk_label_set_text(GTK_LABEL(ui->status_message_label), "Hint revealed!");
            save_game_to_file(ui->game_state);
            refresh_user_interface(ui);
        } else {
            gtk_label_set_text(GTK_LABEL(ui->status_message_label), "Cell already filled!");
        }
    } else {
        gtk_label_set_text(GTK_LABEL(ui->status_message_label), "Please select a cell first!");
    }
}

/**
 * Handle reset button click
 */
void handle_reset_click(GtkButton *button, UIState *ui) {
    copy_grid_data(ui->game_state->initial_grid, ui->game_state->current_grid);
    memset(ui->game_state->validation_status, 0, sizeof(ui->game_state->validation_status));
    ui->game_state->algorithm_steps = 0;
    ui->currently_selected_number = -1;
    ui->game_state->player_score = 0;
    ui->game_state->mistake_count = 0;
    ui->game_state->elapsed_seconds = 0;
    ui->game_state->is_game_over = false;
    
    // Remove old timer if running
    if (ui->timer_source_id > 0) {
        g_source_remove(ui->timer_source_id);
        ui->timer_source_id = 0;
    }
    
    ui->timer_source_id = g_timeout_add_seconds(1, timer_tick_callback, ui);
    set_number_pad_sensitivity(ui, true);
    save_game_to_file(ui->game_state);
    refresh_user_interface(ui);
    gtk_label_set_text(GTK_LABEL(ui->status_message_label), "Game reset to initial state");
}

/**
 * Handle solve button click
 */
void handle_solve_click(GtkButton *button, UIState *ui) {
    copy_grid_data(ui->game_state->current_grid, ui->game_state->solution_grid);
    ui->game_state->algorithm_steps = 0;
    ui->game_state->is_solving = true;
    
    bool solved = solve_sudoku_with_dlx(ui->game_state, ui->game_state->solution_grid);
    
    if (solved) {
        copy_grid_data(ui->game_state->solution_grid, ui->game_state->current_grid);
        
        for (int i = 0; i < GRID_SIZE; i++) {
            for (int j = 0; j < GRID_SIZE; j++) {
                if (ui->game_state->initial_grid[i][j] == 0) {
                    ui->game_state->validation_status[i][j] = 1;
                }
            }
        }
        
        refresh_user_interface(ui);
        
        char status[256];
        snprintf(status, sizeof(status), "Puzzle solved using DLX in %d steps!", 
                ui->game_state->algorithm_steps);
        gtk_label_set_text(GTK_LABEL(ui->status_message_label), status);
        
        ui->game_state->is_game_over = true;
        set_number_pad_sensitivity(ui, false);
        
        if (ui->timer_source_id) {
            g_source_remove(ui->timer_source_id);
            ui->timer_source_id = 0;
        }
        
        save_game_to_file(ui->game_state);
        
        show_information_dialog(ui->main_window, "Puzzle Solved!", 
                               "The puzzle has been solved using Donald Knuth's Dancing Links Algorithm!");
    } else {
        show_information_dialog(ui->main_window, "Error", "Could not solve the puzzle!");
    }
}

/**
 * Navigate to main menu
 */
void navigate_to_main_menu(UIState *ui) {
    if (ui->timer_source_id) {
        g_source_remove(ui->timer_source_id);
        ui->timer_source_id = 0;
    }
    
    if (ui->main_game_container) {
        gtk_widget_set_visible(ui->main_game_container, FALSE);
    }
    
    build_main_menu_interface(ui);
}

/**
 * Confirm and restart game
 */
void confirm_and_restart_game(UIState *ui) {
    copy_grid_data(ui->game_state->initial_grid, ui->game_state->current_grid);
    memset(ui->game_state->validation_status, 0, sizeof(ui->game_state->validation_status));
    ui->game_state->algorithm_steps = 0;
    ui->currently_selected_number = -1;
    ui->game_state->player_score = 0;
    ui->game_state->mistake_count = 0;
    ui->game_state->elapsed_seconds = 0;
    ui->game_state->is_game_over = false;
    
    if (ui->timer_source_id) {
        g_source_remove(ui->timer_source_id);
        ui->timer_source_id = 0;
    }
    
    ui->timer_source_id = g_timeout_add_seconds(1, timer_tick_callback, ui);
    set_number_pad_sensitivity(ui, true);
    save_game_to_file(ui->game_state);
    refresh_user_interface(ui);
    gtk_label_set_text(GTK_LABEL(ui->status_message_label), "Game restarted!");
}

/**
 * Handle restart game button click
 */
void handle_restart_game_click(GtkButton *button, UIState *ui) {
    show_confirmation_dialog(ui, "Restart Game", 
                            "Are you sure you want to restart? All progress will be lost.",
                            confirm_and_restart_game);
}

/**
 * Handle return to menu button click
 */
void handle_return_to_menu_click(GtkButton *button, UIState *ui) {
    show_confirmation_dialog(ui, "Return to Menu", 
                            "Are you sure you want to return to menu? Current game will be saved.",
                            navigate_to_main_menu);
}

/* ========== MENU AND GAME UI CONSTRUCTION ========== */

/**
 * Start a new game with selected difficulty
 */
void start_new_game_with_difficulty(GtkButton *button, gpointer user_data) {
    UIState *ui = (UIState *)user_data;
    DifficultyLevel difficulty = GPOINTER_TO_INT(g_object_get_data(G_OBJECT(button), "difficulty"));

    // Generate complete solution
    generate_complete_sudoku_grid(ui->game_state->solution_grid);
    copy_grid_data(ui->game_state->solution_grid, ui->game_state->current_grid);
    
    // Remove numbers based on difficulty (using report formula)
    int cells_to_remove = calculate_cells_to_remove_for_difficulty(difficulty);
    remove_numbers_from_grid(ui->game_state->current_grid, cells_to_remove);
    
    // Save initial state
    copy_grid_data(ui->game_state->current_grid, ui->game_state->initial_grid);
    memset(ui->game_state->validation_status, 0, sizeof(ui->game_state->validation_status));
    
    // Initialize game state
    ui->game_state->algorithm_steps = 0;
    ui->game_state->difficulty = difficulty;
    ui->game_state->player_score = 0;
    ui->game_state->mistake_count = 0;
    ui->game_state->elapsed_seconds = 0;
    ui->game_state->is_game_over = false;

    ui->currently_selected_row = -1;
    ui->currently_selected_col = -1;
    ui->currently_selected_number = -1;

    save_game_to_file(ui->game_state);

    // Switch to game UI
    if (ui->menu_screen_container) {
        gtk_widget_set_visible(ui->menu_screen_container, FALSE);
    }
    
    build_game_user_interface(ui);

    // Start timer
    if (ui->timer_source_id) {
        g_source_remove(ui->timer_source_id);
        ui->timer_source_id = 0;
    }
    ui->timer_source_id = g_timeout_add_seconds(1, timer_tick_callback, ui);
}

/**
 * Continue saved game
 */
void continue_saved_game(GtkButton *button, gpointer user_data) {
    UIState *ui = (UIState *)user_data;
    
    if (load_game_from_file(ui->game_state)) {
        ui->currently_selected_row = -1;
        ui->currently_selected_col = -1;
        ui->currently_selected_number = -1;
        
        if (ui->menu_screen_container) {
            gtk_widget_set_visible(ui->menu_screen_container, FALSE);
        }
        
        build_game_user_interface(ui);
        
        if (ui->timer_source_id) {
            g_source_remove(ui->timer_source_id);
            ui->timer_source_id = 0;
        }
        
        if (!ui->game_state->is_game_over) {
            ui->timer_source_id = g_timeout_add_seconds(1, timer_tick_callback, ui);
        } else {
            set_number_pad_sensitivity(ui, false);
        }
    }
}

/**
 * Build main menu interface
 */
void build_main_menu_interface(UIState *ui) {
    ui->menu_screen_container = gtk_box_new(GTK_ORIENTATION_VERTICAL, 20);
    gtk_window_set_child(ui->main_window, ui->menu_screen_container);
    gtk_widget_set_margin_start(ui->menu_screen_container, 30);
    gtk_widget_set_margin_end(ui->menu_screen_container, 30);
    gtk_widget_set_margin_top(ui->menu_screen_container, 50);
    gtk_widget_set_margin_bottom(ui->menu_screen_container, 50);
    gtk_widget_set_halign(ui->menu_screen_container, GTK_ALIGN_CENTER);
    gtk_widget_set_valign(ui->menu_screen_container, GTK_ALIGN_CENTER);

    // Title
    GtkWidget *title = gtk_label_new("SUDOKU");
    PangoAttrList *title_attrs = pango_attr_list_new();
    pango_attr_list_insert(title_attrs, pango_attr_size_new(36 * 1024));
    pango_attr_list_insert(title_attrs, pango_attr_weight_new(PANGO_WEIGHT_BOLD));
    gtk_label_set_attributes(GTK_LABEL(title), title_attrs);
    gtk_box_append(GTK_BOX(ui->menu_screen_container), title);
    pango_attr_list_unref(title_attrs);  // MEMORY FIX: Free attrs

    // Continue button if save exists
    if (check_saved_game_exists()) {
        GtkWidget *continue_button = gtk_button_new_with_label("Continue Game");
        gtk_box_append(GTK_BOX(ui->menu_screen_container), continue_button);
        g_signal_connect(continue_button, "clicked", G_CALLBACK(continue_saved_game), ui);
    }

    // New game label
    GtkWidget *new_game_label = gtk_label_new("New Game - Select Difficulty");
    gtk_box_append(GTK_BOX(ui->menu_screen_container), new_game_label);

    // Difficulty buttons (GUI stays EXACTLY the same)
    const char *difficulty_names[] = {"Beginner", "Medium", "Hard", "Expert"};
    DifficultyLevel difficulty_values[] = {
        DIFFICULTY_BEGINNER,  // L=1:  53 clues
        DIFFICULTY_MEDIUM,    // L=4:  44 clues
        DIFFICULTY_HARD,      // L=7:  35 clues
        DIFFICULTY_EXPERT     // L=10: 26 clues
    };

    for (int i = 0; i < 4; i++) {
        GtkWidget *button = gtk_button_new_with_label(difficulty_names[i]);
        gtk_box_append(GTK_BOX(ui->menu_screen_container), button);
        g_object_set_data(G_OBJECT(button), "difficulty", 
                         GINT_TO_POINTER(difficulty_values[i]));
        g_signal_connect(button, "clicked", G_CALLBACK(start_new_game_with_difficulty), ui);
    }
}

/**
 * Build game user interface
 */
void build_game_user_interface(UIState *ui) {
    ui->main_game_container = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    gtk_window_set_child(ui->main_window, ui->main_game_container);

    // Top area
    GtkWidget *top_area = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    gtk_box_append(GTK_BOX(ui->main_game_container), top_area);

    // Menu bar
    GtkWidget *menu_bar = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
    gtk_widget_set_margin_top(menu_bar, 10);
    gtk_widget_set_margin_start(menu_bar, 15);
    gtk_widget_set_margin_end(menu_bar, 15);
    gtk_widget_set_halign(menu_bar, GTK_ALIGN_START);
    gtk_box_append(GTK_BOX(top_area), menu_bar);

    GtkWidget *menu_button_top = gtk_button_new_with_label("Menu");
    gtk_widget_add_css_class(menu_button_top, "menu-btn");
    gtk_box_append(GTK_BOX(menu_bar), menu_button_top);
    g_signal_connect(menu_button_top, "clicked", G_CALLBACK(handle_return_to_menu_click), ui);

    // Title
    GtkWidget *title_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
    gtk_widget_set_halign(title_box, GTK_ALIGN_CENTER);
    gtk_widget_set_margin_top(title_box, 5);
    gtk_box_append(GTK_BOX(top_area), title_box);

    GtkWidget *title = gtk_label_new("SUDOKU");
    PangoAttrList *title_attrs = pango_attr_list_new();
    pango_attr_list_insert(title_attrs, pango_attr_size_new(24 * 1024));
    pango_attr_list_insert(title_attrs, pango_attr_weight_new(PANGO_WEIGHT_BOLD));
    gtk_label_set_attributes(GTK_LABEL(title), title_attrs);
    gtk_box_append(GTK_BOX(title_box), title);
    pango_attr_list_unref(title_attrs);  // MEMORY FIX: Free attrs

    // Info bar
    GtkWidget *info_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
    gtk_widget_set_halign(info_box, GTK_ALIGN_CENTER);
    gtk_widget_set_margin_bottom(info_box, 10);
    gtk_box_append(GTK_BOX(ui->main_game_container), info_box);

    ui->score_display_label = gtk_label_new("Score: 0");
    ui->mistakes_display_label = gtk_label_new("Mistakes: 0/3");
    ui->difficulty_display_label = gtk_label_new(
        get_difficulty_display_name(ui->game_state->difficulty));
    ui->timer_display_label = gtk_label_new("00:00");

    GtkWidget *info_grid = gtk_grid_new();
    gtk_grid_set_column_spacing(GTK_GRID(info_grid), 30);
    gtk_grid_set_column_homogeneous(GTK_GRID(info_grid), TRUE);
    gtk_box_append(GTK_BOX(info_box), info_grid);

    gtk_grid_attach(GTK_GRID(info_grid), ui->score_display_label, 0, 0, 1, 1);
    gtk_grid_attach(GTK_GRID(info_grid), ui->mistakes_display_label, 1, 0, 1, 1);
    gtk_grid_attach(GTK_GRID(info_grid), ui->difficulty_display_label, 2, 0, 1, 1);
    gtk_grid_attach(GTK_GRID(info_grid), ui->timer_display_label, 3, 0, 1, 1);

    // Drawing area
    GtkWidget *drawing_container = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    gtk_widget_set_hexpand(drawing_container, TRUE);
    gtk_widget_set_vexpand(drawing_container, TRUE);
    gtk_widget_set_margin_start(drawing_container, 15);
    gtk_widget_set_margin_end(drawing_container, 15);
    gtk_widget_set_margin_top(drawing_container, 10);
    gtk_widget_set_margin_bottom(drawing_container, 10);
    gtk_box_append(GTK_BOX(ui->main_game_container), drawing_container);

    ui->grid_drawing_area = gtk_drawing_area_new();
    gtk_widget_set_hexpand(ui->grid_drawing_area, TRUE);
    gtk_widget_set_vexpand(ui->grid_drawing_area, TRUE);
    gtk_drawing_area_set_draw_func(GTK_DRAWING_AREA(ui->grid_drawing_area), 
                                   draw_sudoku_grid, ui, NULL);

    GtkGesture *click_gesture = gtk_gesture_click_new();
    g_signal_connect(click_gesture, "pressed", G_CALLBACK(handle_grid_click), ui);
    gtk_widget_add_controller(ui->grid_drawing_area, GTK_EVENT_CONTROLLER(click_gesture));

    gtk_box_append(GTK_BOX(drawing_container), ui->grid_drawing_area);

    // Number pad
    GtkWidget *pad_label = gtk_label_new("Enter Number:");
    gtk_box_append(GTK_BOX(ui->main_game_container), pad_label);
    gtk_widget_set_halign(pad_label, GTK_ALIGN_CENTER);
    gtk_widget_set_margin_top(pad_label, 10);
    gtk_widget_set_margin_bottom(pad_label, 8);

    GtkWidget *pad_container = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    gtk_widget_set_halign(pad_container, GTK_ALIGN_CENTER);
    gtk_box_append(GTK_BOX(ui->main_game_container), pad_container);

    GtkWidget *pad_grid = gtk_grid_new();
    gtk_grid_set_row_spacing(GTK_GRID(pad_grid), 8);
    gtk_grid_set_column_spacing(GTK_GRID(pad_grid), 8);
    gtk_grid_set_row_homogeneous(GTK_GRID(pad_grid), TRUE);
    gtk_grid_set_column_homogeneous(GTK_GRID(pad_grid), TRUE);
    gtk_box_append(GTK_BOX(pad_container), pad_grid);

    for (int i = 1; i <= 9; i++) {
        char label[3];
        snprintf(label, sizeof(label), "%d", i);
        GtkWidget *button = gtk_button_new_with_label(label);
        ui->number_buttons[i] = button;
        gtk_widget_add_css_class(button, "number-btn");
        gtk_grid_attach(GTK_GRID(pad_grid), button, (i - 1) % 5, (i - 1) / 5, 1, 1);
        g_signal_connect(button, "clicked", G_CALLBACK(handle_number_button_click), ui);
    }

    // Action buttons
    GtkWidget *action_container = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    gtk_widget_set_halign(action_container, GTK_ALIGN_CENTER);
    gtk_widget_set_margin_top(action_container, 15);
    gtk_box_append(GTK_BOX(ui->main_game_container), action_container);

    GtkWidget *action_grid = gtk_grid_new();
    gtk_grid_set_column_spacing(GTK_GRID(action_grid), 10);
    gtk_grid_set_column_homogeneous(GTK_GRID(action_grid), TRUE);
    gtk_box_append(GTK_BOX(action_container), action_grid);

    GtkWidget *clear_button = gtk_button_new_with_label("Clear");
    gtk_widget_add_css_class(clear_button, "action-btn");
    gtk_grid_attach(GTK_GRID(action_grid), clear_button, 0, 0, 1, 1);
    g_signal_connect(clear_button, "clicked", G_CALLBACK(handle_clear_cell_click), ui);

    GtkWidget *hint_button = gtk_button_new_with_label("Hint");
    gtk_widget_add_css_class(hint_button, "action-btn");
    gtk_grid_attach(GTK_GRID(action_grid), hint_button, 1, 0, 1, 1);
    g_signal_connect(hint_button, "clicked", G_CALLBACK(handle_hint_click), ui);

    GtkWidget *solve_button = gtk_button_new_with_label("Solve");
    gtk_widget_add_css_class(solve_button, "action-btn");
    gtk_grid_attach(GTK_GRID(action_grid), solve_button, 2, 0, 1, 1);
    g_signal_connect(solve_button, "clicked", G_CALLBACK(handle_solve_click), ui);

    GtkWidget *reset_button = gtk_button_new_with_label("Reset");
    gtk_widget_add_css_class(reset_button, "action-btn");
    gtk_grid_attach(GTK_GRID(action_grid), reset_button, 3, 0, 1, 1);
    g_signal_connect(reset_button, "clicked", G_CALLBACK(handle_reset_click), ui);

    // Status label
    ui->status_message_label = gtk_label_new("Select a cell and enter a number");
    gtk_box_append(GTK_BOX(ui->main_game_container), ui->status_message_label);
    gtk_widget_set_halign(ui->status_message_label, GTK_ALIGN_CENTER);
    gtk_widget_set_margin_top(ui->status_message_label, 12);
    gtk_widget_set_margin_bottom(ui->status_message_label, 15);

    // Set number pad sensitivity based on game state
    if (ui->game_state->is_game_over) {
        set_number_pad_sensitivity(ui, false);
    } else {
        set_number_pad_sensitivity(ui, true);
    }

    refresh_user_interface(ui);
}

/* ========== UI CLEANUP (MEMORY LEAK FIX) ========== */

/**
 * Cleanup all UI resources when window is destroyed
 * CRITICAL for preventing memory leaks
 */
void cleanup_ui_resources(GtkWidget *window, gpointer data) {
    UIState *ui = (UIState *)data;
    if (!ui) return;
    
    // Stop timer to prevent callback on freed memory
    if (ui->timer_source_id > 0) {
        g_source_remove(ui->timer_source_id);
        ui->timer_source_id = 0;
    }
    
    // Save and free game state
    if (ui->game_state) {
        save_game_to_file(ui->game_state);
        g_free(ui->game_state);
        ui->game_state = NULL;
    }
    
    // Free UI state structure
    g_free(ui);
}

/* ========== UI UPDATE FUNCTIONS ========== */

/**
 * Update information bar displays
 */
void update_information_bar(UIState *ui) {
    if (!ui->score_display_label || !ui->mistakes_display_label || 
        !ui->difficulty_display_label || !ui->timer_display_label) {
        return;
    }
    
    char buffer[64];
    
    // Update score
    snprintf(buffer, sizeof(buffer), "Score: %d", ui->game_state->player_score);
    gtk_label_set_text(GTK_LABEL(ui->score_display_label), buffer);

    // Update mistakes
    snprintf(buffer, sizeof(buffer), "Mistakes: %d/%d", 
            ui->game_state->mistake_count, MAX_MISTAKES_ALLOWED);
    gtk_label_set_text(GTK_LABEL(ui->mistakes_display_label), buffer);

    // Update difficulty
    const char *difficulty_str = get_difficulty_display_name(ui->game_state->difficulty);
    gtk_label_set_text(GTK_LABEL(ui->difficulty_display_label), difficulty_str);

    // Update timer
    int seconds = ui->game_state->elapsed_seconds % 60;
    int minutes = (ui->game_state->elapsed_seconds / 60) % 60;
    int hours = ui->game_state->elapsed_seconds / 3600;
    
    if (hours > 0) {
        snprintf(buffer, sizeof(buffer), "%02d:%02d:%02d", hours, minutes, seconds);
    } else {
        snprintf(buffer, sizeof(buffer), "%02d:%02d", minutes, seconds);
    }
    gtk_label_set_text(GTK_LABEL(ui->timer_display_label), buffer);
}

/**
 * Refresh entire user interface
 */
void refresh_user_interface(UIState *ui) {
    if (ui) {
        update_information_bar(ui);
        if (ui->grid_drawing_area) {
            gtk_widget_queue_draw(ui->grid_drawing_area);
        }
    }
}

/* ========== NUMBER INPUT HANDLER ========== */

/**
 * Handle number button click
 * Validates placement and updates game state
 */
void handle_number_button_click(GtkButton *button, UIState *ui) {
    if (!ui || !ui->game_state) return;

    if (ui->game_state->is_game_over) {
        gtk_label_set_text(GTK_LABEL(ui->status_message_label), 
                          "Game over — start a new game!");
        return;
    }

    if (ui->currently_selected_row == -1 || ui->currently_selected_col == -1) {
        gtk_label_set_text(GTK_LABEL(ui->status_message_label), 
                          "Please select a cell first!");
        return;
    }

    const char *label = gtk_button_get_label(button);
    int number = atoi(label);

    // Cannot modify original cells
    if (ui->game_state->initial_grid[ui->currently_selected_row][ui->currently_selected_col] != 0) {
        gtk_label_set_text(GTK_LABEL(ui->status_message_label), 
                          "Cannot modify original cells!");
        return;
    }

    int previous_value = ui->game_state->current_grid[ui->currently_selected_row][ui->currently_selected_col];

    if (number == 0) {
        // Clear cell
        ui->game_state->current_grid[ui->currently_selected_row][ui->currently_selected_col] = 0;
        ui->game_state->validation_status[ui->currently_selected_row][ui->currently_selected_col] = 0;
        gtk_label_set_text(GTK_LABEL(ui->status_message_label), "Cell cleared");
    } else {
        // Place number
        ui->game_state->current_grid[ui->currently_selected_row][ui->currently_selected_col] = number;
        
        if (is_cell_value_valid(ui->game_state->current_grid, 
                                ui->currently_selected_row, 
                                ui->currently_selected_col, number)) {
            // Valid placement
            ui->game_state->validation_status[ui->currently_selected_row][ui->currently_selected_col] = 1;
            gtk_label_set_text(GTK_LABEL(ui->status_message_label), "Valid move");

            // Award points for new placement
            if (previous_value == 0) {
                ui->game_state->player_score += 10;
            }
        } else {
            // Invalid placement
            ui->game_state->validation_status[ui->currently_selected_row][ui->currently_selected_col] = 2;
            ui->game_state->mistake_count += 1;
            
            char status[128];
            snprintf(status, sizeof(status), "Invalid move (%d/%d mistakes)", 
                    ui->game_state->mistake_count, MAX_MISTAKES_ALLOWED);
            gtk_label_set_text(GTK_LABEL(ui->status_message_label), status);

            // Check for game over
            if (ui->game_state->mistake_count >= MAX_MISTAKES_ALLOWED) {
                gtk_label_set_text(GTK_LABEL(ui->status_message_label), 
                                  "Game Over — Too many mistakes!");
                ui->game_state->is_game_over = true;
                set_number_pad_sensitivity(ui, false);
                show_information_dialog(ui->main_window, "Game Over", 
                                       "You've made too many mistakes! Try again or start a new game.");
            }
        }
    }

    // Update selected number highlight
    ui->currently_selected_number = (ui->game_state->current_grid[ui->currently_selected_row][ui->currently_selected_col] > 0)
                                    ? ui->game_state->current_grid[ui->currently_selected_row][ui->currently_selected_col] : -1;

    save_game_to_file(ui->game_state);
    refresh_user_interface(ui);
}

/* ========== APPLICATION ACTIVATION ========== */

/**
 * Activate callback - initializes application
 */
void activate_application(GtkApplication *app, gpointer user_data) {
    srand((unsigned)time(NULL));

    // Allocate UI state (freed in cleanup_ui_resources)
    UIState *ui = g_new0(UIState, 1);
    ui->game_state = g_new0(SudokuGameState, 1);
    ui->currently_selected_row = -1;
    ui->currently_selected_col = -1;
    ui->currently_selected_number = -1;
    ui->timer_source_id = 0;

    // Create main window
    GtkWidget *window = gtk_application_window_new(app);
    ui->main_window = GTK_WINDOW(window);
    gtk_window_set_title(GTK_WINDOW(window), "Sudoku DLX Solver");
    gtk_window_set_default_size(GTK_WINDOW(window), 600, 800);

    // Connect cleanup handler
    g_signal_connect(window, "destroy", G_CALLBACK(cleanup_ui_resources), ui);

    // Apply CSS styling
    GtkCssProvider *css_provider = gtk_css_provider_new();
    gtk_css_provider_load_from_string(css_provider,
        "window { background: white; }"
        ".number-btn { "
        "  font-size: 20px; "
        "  padding: 12px; "
        "  min-width: 55px; "
        "  min-height: 50px; "
        "  font-weight: bold; "
        "  color: #4a90e2; "
        "  border: 2px solid #4a90e2; "
        "  border-radius: 5px; "
        "}"
        ".action-btn { "
        "  font-size: 14px; "
        "  padding: 10px 20px; "
        "  font-weight: bold; "
        "  border-radius: 5px; "
        "}"
        ".menu-btn { "
        "  font-size: 14px; "
        "  padding: 8px 16px; "
        "  font-weight: bold; "
        "  border-radius: 5px; "
        "  background: #f0f0f0; "
        "}"
    );
    
    gtk_style_context_add_provider_for_display(
        gdk_display_get_default(), 
        GTK_STYLE_PROVIDER(css_provider), 
        GTK_STYLE_PROVIDER_PRIORITY_APPLICATION
    );
    
    g_object_unref(css_provider);  // MEMORY FIX: Unref CSS provider

    // Build initial menu
    build_main_menu_interface(ui);
    
    gtk_window_present(GTK_WINDOW(window));
}

/* ========== MAIN FUNCTION ========== */

/**
 * Main entry point
 */
int main(int argc, char **argv) {
    GtkApplication *app = gtk_application_new(
        "org.sudoku.dlx.solver", 
        G_APPLICATION_DEFAULT_FLAGS
    );
    
    g_signal_connect(app, "activate", G_CALLBACK(activate_application), NULL);
    
    int status = g_application_run(G_APPLICATION(app), argc, argv);
    
    g_object_unref(app);  // MEMORY FIX: Unref application
    
    return status;
}