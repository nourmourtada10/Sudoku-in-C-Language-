#include <gtk/gtk.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <time.h>
#include <stdio.h>

#define SIZE 9
#define SUBGRID 3
#define SAVE_FILE "sudoku_save.dat"
#define MAX_MISTAKES 3

typedef enum {
    DIFFICULTY_EASY = 30,
    DIFFICULTY_MEDIUM = 40,
    DIFFICULTY_HARD = 50,
    DIFFICULTY_EXPERT = 60
} Difficulty;

typedef struct {
    int grid[SIZE][SIZE];
    int solution[SIZE][SIZE];
    int original[SIZE][SIZE];
    int validation[SIZE][SIZE];
    int steps;
    bool solving;
    Difficulty difficulty;
    /* Persisted gameplay state */
    int score;
    int mistakes;
    int seconds_elapsed;
    bool game_over;
} SudokuGame;

typedef struct {
    GtkWindow *window;
    GtkWidget *main_container;
    GtkWidget *menu_container;
    GtkWidget *drawing_area;
    GtkWidget *number_pad[10]; /* 1..9 */
    GtkWidget *status_label;

    /* Info bar widgets */
    GtkWidget *score_label;
    GtkWidget *mistakes_label;
    GtkWidget *difficulty_label;
    GtkWidget *timer_label;

    int selected_row, selected_col;
    int selected_number;
    guint timer_id;

    SudokuGame *game;
} UIState;

/* ========== DANCING LINKS (DLX) DATA STRUCTURES ========== */

typedef struct DLXNode {
    struct DLXNode *left, *right, *up, *down;
    struct DLXNode *column;
    int row_id;
    int size; /* only for column headers */
} DLXNode;

typedef struct {
    DLXNode *header;
    DLXNode *columns[324]; /* 4 constraint types × 81 cells */
    int solution[81];
    int solution_count;
    SudokuGame *game;
} DLXSolver;

/* ========== FILE I/O ========== */
void save_game(SudokuGame *game) {
    FILE *f = fopen(SAVE_FILE, "wb");
    if (f) {
        fwrite(game, sizeof(SudokuGame), 1, f);
        fclose(f);
    }
}

bool load_game(SudokuGame *game) {
    FILE *f = fopen(SAVE_FILE, "rb");
    if (f) {
        size_t r = fread(game, sizeof(SudokuGame), 1, f);
        fclose(f);
        return r == 1;
    }
    return false;
}

bool has_saved_game() {
    FILE *f = fopen(SAVE_FILE, "rb");
    if (f) {
        fclose(f);
        return true;
    }
    return false;
}

/* ========== SUDOKU LOGIC ========== */
bool is_valid_placement(int grid[SIZE][SIZE], int row, int col, int num) {
    for (int i = 0; i < SIZE; i++) {
        if (grid[row][i] == num) return false;
        if (grid[i][col] == num) return false;
    }
    int sr = (row / SUBGRID) * SUBGRID;
    int sc = (col / SUBGRID) * SUBGRID;
    for (int i = sr; i < sr + SUBGRID; i++)
        for (int j = sc; j < sc + SUBGRID; j++)
            if (grid[i][j] == num) return false;
    return true;
}

bool fill_grid(int grid[SIZE][SIZE], int row, int col) {
    if (row == SIZE) return true;
    int next_row = row, next_col = col + 1;
    if (next_col == SIZE) { next_row++; next_col = 0; }

    int nums[SIZE];
    for (int i = 0; i < SIZE; i++) nums[i] = i + 1;
    for (int i = SIZE - 1; i > 0; i--) {
        int j = rand() % (i + 1);
        int tmp = nums[i]; nums[i] = nums[j]; nums[j] = tmp;
    }
    for (int i = 0; i < SIZE; i++) {
        if (is_valid_placement(grid, row, col, nums[i])) {
            grid[row][col] = nums[i];
            if (fill_grid(grid, next_row, next_col)) return true;
            grid[row][col] = 0;
        }
    }
    return false;
}

void generate_complete_grid(int grid[SIZE][SIZE]) {
    memset(grid, 0, sizeof(int) * SIZE * SIZE);
    fill_grid(grid, 0, 0);
}

void remove_numbers(int grid[SIZE][SIZE], int cells_to_remove) {
    int removed = 0;
    while (removed < cells_to_remove) {
        int r = rand() % SIZE, c = rand() % SIZE;
        if (grid[r][c] != 0) { grid[r][c] = 0; removed++; }
    }
}

void copy_grid(int src[SIZE][SIZE], int dst[SIZE][SIZE]) {
    memcpy(dst, src, sizeof(int) * SIZE * SIZE);
}

bool is_valid(int grid[SIZE][SIZE], int row, int col, int num) {
    for (int i = 0; i < SIZE; i++) {
        if (i != col && grid[row][i] == num) return false;
        if (i != row && grid[i][col] == num) return false;
    }
    int sr = (row / SUBGRID) * SUBGRID;
    int sc = (col / SUBGRID) * SUBGRID;
    for (int i = sr; i < sr + SUBGRID; i++)
        for (int j = sc; j < sc + SUBGRID; j++)
            if (!(i == row && j == col) && grid[i][j] == num) return false;
    return true;
}

bool is_complete(int grid[SIZE][SIZE]) {
    for (int i = 0; i < SIZE; i++)
        for (int j = 0; j < SIZE; j++)
            if (grid[i][j] == 0) return false;
    return true;
}

/* ========== DANCING LINKS ALGORITHM ========== */

DLXNode* create_node() {
    DLXNode *node = (DLXNode*)calloc(1, sizeof(DLXNode));
    node->left = node->right = node->up = node->down = node;
    node->column = NULL;
    node->row_id = -1;
    node->size = 0;
    return node;
}

void cover(DLXNode *col) {
    col->right->left = col->left;
    col->left->right = col->right;
    
    for (DLXNode *row = col->down; row != col; row = row->down) {
        for (DLXNode *node = row->right; node != row; node = node->right) {
            node->down->up = node->up;
            node->up->down = node->down;
            node->column->size--;
        }
    }
}

void uncover(DLXNode *col) {
    for (DLXNode *row = col->up; row != col; row = row->up) {
        for (DLXNode *node = row->left; node != row; node = node->left) {
            node->column->size++;
            node->down->up = node;
            node->up->down = node;
        }
    }
    
    col->right->left = col;
    col->left->right = col;
}

bool search(DLXSolver *solver, int k) {
    solver->game->steps++;
    
    if (solver->header->right == solver->header) {
        solver->solution_count = k;
        return true;
    }
    
    /* Choose column with minimum size (heuristic S) */
    DLXNode *col = NULL;
    int min_size = 999999;
    for (DLXNode *c = solver->header->right; c != solver->header; c = c->right) {
        if (c->size < min_size) {
            min_size = c->size;
            col = c;
        }
    }
    
    if (col == NULL || col->size == 0) return false;
    
    cover(col);
    
    for (DLXNode *row = col->down; row != col; row = row->down) {
        solver->solution[k] = row->row_id;
        
        for (DLXNode *node = row->right; node != row; node = node->right) {
            cover(node->column);
        }
        
        if (search(solver, k + 1)) {
            return true;
        }
        
        for (DLXNode *node = row->left; node != row; node = node->left) {
            uncover(node->column);
        }
    }
    
    uncover(col);
    return false;
}

/* Convert Sudoku constraints to exact cover problem */
void init_dlx_solver(DLXSolver *solver, int grid[SIZE][SIZE]) {
    solver->header = create_node();
    solver->solution_count = 0;
    
    /* Create 324 column headers:
       - 81 for cell constraints (each cell must be filled)
       - 81 for row constraints (each row must have 1-9)
       - 81 for column constraints (each column must have 1-9)
       - 81 for box constraints (each 3x3 box must have 1-9)
    */
    DLXNode *prev = solver->header;
    for (int i = 0; i < 324; i++) {
        DLXNode *col = create_node();
        solver->columns[i] = col;
        col->size = 0;
        col->column = col;
        
        prev->right = col;
        col->left = prev;
        prev = col;
    }
    prev->right = solver->header;
    solver->header->left = prev;
    
    /* Create rows for each possible placement
       Row ID encoding: r * 81 + c * 9 + (num - 1)
       where r=row, c=col, num=number (1-9)
    */
    for (int r = 0; r < SIZE; r++) {
        for (int c = 0; c < SIZE; c++) {
            /* If cell is filled, only create row for that number */
            int start_num = (grid[r][c] != 0) ? grid[r][c] : 1;
            int end_num = (grid[r][c] != 0) ? grid[r][c] : 9;
            
            for (int num = start_num; num <= end_num; num++) {
                int row_id = r * 81 + c * 9 + (num - 1);
                int box = (r / 3) * 3 + (c / 3);
                
                /* Four constraints for this placement */
                int constraints[4] = {
                    r * 9 + c,                    /* Cell constraint */
                    81 + r * 9 + (num - 1),      /* Row constraint */
                    162 + c * 9 + (num - 1),     /* Column constraint */
                    243 + box * 9 + (num - 1)    /* Box constraint */
                };
                
                DLXNode *prev_node = NULL;
                for (int i = 0; i < 4; i++) {
                    DLXNode *node = create_node();
                    node->row_id = row_id;
                    node->column = solver->columns[constraints[i]];
                    
                    /* Link vertically */
                    node->up = node->column->up;
                    node->down = node->column;
                    node->column->up->down = node;
                    node->column->up = node;
                    node->column->size++;
                    
                    /* Link horizontally */
                    if (prev_node == NULL) {
                        node->left = node->right = node;
                    } else {
                        node->left = prev_node;
                        node->right = prev_node->right;
                        prev_node->right->left = node;
                        prev_node->right = node;
                    }
                    prev_node = node;
                }
            }
        }
    }
}

void free_dlx_solver(DLXSolver *solver) {
    /* Free all nodes - traverse through columns */
    for (int i = 0; i < 324; i++) {
        DLXNode *col = solver->columns[i];
        DLXNode *row = col->down;
        while (row != col) {
            DLXNode *next_row = row->down;
            DLXNode *node = row->right;
            while (node != row) {
                DLXNode *next_node = node->right;
                free(node);
                node = next_node;
            }
            free(row);
            row = next_row;
        }
        free(col);
    }
    free(solver->header);
}

bool solve_dlx(SudokuGame *game, int grid[SIZE][SIZE]) {
    DLXSolver solver;
    solver.game = game;
    
    init_dlx_solver(&solver, grid);
    
    bool result = search(&solver, 0);
    
    if (result) {
        /* Extract solution from row IDs */
        for (int i = 0; i < solver.solution_count; i++) {
            int row_id = solver.solution[i];
            int r = row_id / 81;
            int c = (row_id % 81) / 9;
            int num = (row_id % 9) + 1;
            grid[r][c] = num;
        }
    }
    
    free_dlx_solver(&solver);
    return result;
}

/* ========== CAIRO DRAWING ========== */
static void draw_sudoku(GtkDrawingArea *area, cairo_t *cr, int width, int height, gpointer data) {
    UIState *ui = (UIState *)data;
    SudokuGame *game = ui->game;

    double margin = 20;
    double grid_size = MIN(width, height) - 2 * margin;
    double cell_size = grid_size / SIZE;
    double start_x = (width - grid_size) / 2;
    double start_y = (height - grid_size) / 2;

    // Background
    cairo_set_source_rgb(cr, 1, 1, 1);
    cairo_paint(cr);

    // Highlights (row/col/subgrid and matching numbers)
    for (int r = 0; r < SIZE; r++) {
        for (int c = 0; c < SIZE; c++) {
            bool highlight = false;
            bool highlight_number = false;

            if (ui->selected_row >= 0 && ui->selected_col >= 0) {
                if (r == ui->selected_row || c == ui->selected_col ||
                    (r / SUBGRID == ui->selected_row / SUBGRID && c / SUBGRID == ui->selected_col / SUBGRID)) {
                    highlight = true;
                }
            }

            int sel_num = ui->selected_number;
            if (sel_num > 0 && game->grid[r][c] == sel_num) {
                highlight_number = true;
            }

            if (highlight_number) {
                cairo_set_source_rgb(cr, 0.71, 0.86, 1.0); // light blue
                cairo_rectangle(cr, start_x + c * cell_size, start_y + r * cell_size, cell_size, cell_size);
                cairo_fill(cr);
            } else if (highlight) {
                cairo_set_source_rgb(cr, 0.91, 0.94, 1.0); // pale highlight
                cairo_rectangle(cr, start_x + c * cell_size, start_y + r * cell_size, cell_size, cell_size);
                cairo_fill(cr);
            }
        }
    }

    // Grid lines
    cairo_set_source_rgb(cr, 0, 0, 0);
    for (int i = 0; i <= SIZE; i++) {
        cairo_set_line_width(cr, (i % 3 == 0) ? 3.0 : 1.0);

        // horizontal
        cairo_move_to(cr, start_x, start_y + i * cell_size);
        cairo_line_to(cr, start_x + grid_size, start_y + i * cell_size);
        cairo_stroke(cr);

        // vertical
        cairo_move_to(cr, start_x + i * cell_size, start_y);
        cairo_line_to(cr, start_x + i * cell_size, start_y + grid_size);
        cairo_stroke(cr);
    }

    // Numbers
    cairo_select_font_face(cr, "Sans", CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_BOLD);
    cairo_set_font_size(cr, cell_size * 0.5);

    for (int r = 0; r < SIZE; r++) {
        for (int c = 0; c < SIZE; c++) {
            int val = game->grid[r][c];
            if (val == 0) continue;
            char num[2];
            snprintf(num, sizeof(num), "%d", val);

            cairo_text_extents_t ext;
            cairo_text_extents(cr, num, &ext);

            double x = start_x + c * cell_size + (cell_size - ext.width) / 2 - ext.x_bearing;
            double y = start_y + r * cell_size + (cell_size - ext.height) / 2 - ext.y_bearing;

            if (game->original[r][c] != 0) {
                cairo_set_source_rgb(cr, 0, 0, 0); // fixed cell
            } else if (game->validation[r][c] == 2) {
                cairo_set_source_rgb(cr, 0.9, 0.1, 0.1); // invalid entry - RED
            } else {
                cairo_set_source_rgb(cr, 0.2, 0.2, 0.8); // valid user-entered - BLUE
            }

            cairo_move_to(cr, x, y);
            cairo_show_text(cr, num);
        }
    }

    // Outer border
    cairo_set_source_rgb(cr, 0, 0, 0);
    cairo_set_line_width(cr, 4.0);
    cairo_rectangle(cr, start_x, start_y, grid_size, grid_size);
    cairo_stroke(cr);
}

static gboolean on_click(GtkGestureClick *gesture, int n_press, double x, double y, gpointer data) {
    GtkWidget *widget = gtk_event_controller_get_widget(GTK_EVENT_CONTROLLER(gesture));
    int width = gtk_widget_get_allocated_width(widget);
    int height = gtk_widget_get_allocated_height(widget);
    UIState *ui = (UIState *)data;

    double margin = 20;
    double grid_size = MIN(width, height) - 2 * margin;
    double cell_size = grid_size / SIZE;
    double start_x = (width - grid_size) / 2;
    double start_y = (height - grid_size) / 2;

    if (x < start_x || y < start_y || x > start_x + grid_size || y > start_y + grid_size)
        return FALSE;

    int col = (x - start_x) / cell_size;
    int row = (y - start_y) / cell_size;

    ui->selected_row = row;
    ui->selected_col = col;
    ui->selected_number = ui->game->grid[row][col] > 0 ? ui->game->grid[row][col] : -1;

    gtk_widget_queue_draw(widget);
    return TRUE;
}

/* ========== UI / GAME BEHAVIOR ========== */

static const char *difficulty_to_string(Difficulty d) {
    switch (d) {
        case DIFFICULTY_EASY: return "Beginner";
        case DIFFICULTY_MEDIUM: return "Medium";
        case DIFFICULTY_HARD: return "Hard";
        case DIFFICULTY_EXPERT: return "Expert";
        default: return "Unknown";
    }
}

static void set_number_pad_sensitive(UIState *ui, bool sensitive) {
    for (int i = 1; i <= 9; i++) {
        if (ui->number_pad[i])
            gtk_widget_set_sensitive(ui->number_pad[i], sensitive);
    }
}

/* Timer callback: update timer label every second */
gboolean timer_tick(gpointer data) {
    UIState *ui = (UIState *)data;
    if (!ui || !ui->game) return G_SOURCE_REMOVE;

    if (ui->game->game_over) return G_SOURCE_REMOVE;

    ui->game->seconds_elapsed++;
    int s = ui->game->seconds_elapsed % 60;
    int m = (ui->game->seconds_elapsed / 60) % 60;
    int h = ui->game->seconds_elapsed / 3600;

    char buf[64];
    if (h > 0) snprintf(buf, sizeof(buf), "%02d:%02d:%02d", h, m, s);
    else snprintf(buf, sizeof(buf), "%02d:%02d", m, s);
    gtk_label_set_text(GTK_LABEL(ui->timer_label), buf);

    /* Stop if solved or game over */
    if (is_complete(ui->game->grid)) {
        // Check valid
        bool all_valid = true;
        for (int i = 0; i < SIZE && all_valid; i++)
            for (int j = 0; j < SIZE; j++)
                if (ui->game->grid[i][j] != 0 && !is_valid(ui->game->grid, i, j, ui->game->grid[i][j])) { all_valid = false; break; }
        if (all_valid) {
            char st[128];
            snprintf(st, sizeof(st), "🎉 Solved in %02d:%02d! Score: %d", ui->game->seconds_elapsed / 60, ui->game->seconds_elapsed % 60, ui->game->score);
            gtk_label_set_text(GTK_LABEL(ui->status_label), st);
            ui->game->game_over = true;
            set_number_pad_sensitive(ui, false);
            save_game(ui->game);
            return G_SOURCE_REMOVE;
        }
    }
    if (ui->game->mistakes >= MAX_MISTAKES) {
        gtk_label_set_text(GTK_LABEL(ui->status_label), "❌ Game Over – Too many mistakes!");
        ui->game->game_over = true;
        set_number_pad_sensitive(ui, false);
        save_game(ui->game);
        return G_SOURCE_REMOVE;
    }

    return G_SOURCE_CONTINUE;
}

void update_info_bar(UIState *ui) {
    if (!ui->score_label || !ui->mistakes_label || !ui->difficulty_label || !ui->timer_label) {
        return;
    }
    
    char buf[64];
    snprintf(buf, sizeof(buf), "Score: %d", ui->game->score);
    gtk_label_set_text(GTK_LABEL(ui->score_label), buf);

    snprintf(buf, sizeof(buf), "Mistakes: %d/%d", ui->game->mistakes, MAX_MISTAKES);
    gtk_label_set_text(GTK_LABEL(ui->mistakes_label), buf);

    const char *dstr = difficulty_to_string(ui->game->difficulty);
    gtk_label_set_text(GTK_LABEL(ui->difficulty_label), dstr);

    int s = ui->game->seconds_elapsed % 60;
    int m = (ui->game->seconds_elapsed / 60) % 60;
    int h = ui->game->seconds_elapsed / 3600;
    if (h > 0) snprintf(buf, sizeof(buf), "%02d:%02d:%02d", h, m, s);
    else snprintf(buf, sizeof(buf), "%02d:%02d", m, s);
    gtk_label_set_text(GTK_LABEL(ui->timer_label), buf);
}

/* Called whenever display/state should refresh */
void update_ui(UIState *ui) {
    if (ui) {
        update_info_bar(ui);
        if (ui->drawing_area) {
            gtk_widget_queue_draw(ui->drawing_area);
        }
    }
}

/* Number pad pressed */
void on_number_clicked(GtkButton *btn, UIState *ui) {
    if (!ui || !ui->game) return;

    if (ui->game->game_over) {
        gtk_label_set_text(GTK_LABEL(ui->status_label), "Game over – start a new game!");
        return;
    }

    if (ui->selected_row == -1 || ui->selected_col == -1) {
        gtk_label_set_text(GTK_LABEL(ui->status_label), "Please select a cell first!");
        return;
    }

    const char *label = gtk_button_get_label(btn);
    int num = atoi(label);

    if (ui->game->original[ui->selected_row][ui->selected_col] != 0) {
        gtk_label_set_text(GTK_LABEL(ui->status_label), "Cannot modify original cells!");
        return;
    }

    int prev = ui->game->grid[ui->selected_row][ui->selected_col];

    if (num == 0) {
        ui->game->grid[ui->selected_row][ui->selected_col] = 0;
        ui->game->validation[ui->selected_row][ui->selected_col] = 0;
        gtk_label_set_text(GTK_LABEL(ui->status_label), "Cell cleared");
    } else {
        /* Place number and validate */
        ui->game->grid[ui->selected_row][ui->selected_col] = num;
        if (is_valid(ui->game->grid, ui->selected_row, ui->selected_col, num)) {
            ui->game->validation[ui->selected_row][ui->selected_col] = 1;
            gtk_label_set_text(GTK_LABEL(ui->status_label), "Valid move ✓");

            /* increment score when placing a valid number into an empty cell */
            if (prev == 0) {
                ui->game->score += 10;  // Award 10 points for each correct placement
            }
        } else {
            ui->game->validation[ui->selected_row][ui->selected_col] = 2;
            ui->game->mistakes += 1;
            char st[128];
            snprintf(st, sizeof(st), "Invalid move ✗ (%d/%d mistakes)", ui->game->mistakes, MAX_MISTAKES);
            gtk_label_set_text(GTK_LABEL(ui->status_label), st);

            if (ui->game->mistakes >= MAX_MISTAKES) {
                gtk_label_set_text(GTK_LABEL(ui->status_label), "❌ Game Over – Too many mistakes!");
                ui->game->game_over = true;
                set_number_pad_sensitive(ui, false);
            }
        }
    }

    /* update selected number so highlights update */
    ui->selected_number = ui->game->grid[ui->selected_row][ui->selected_col] > 0
                          ? ui->game->grid[ui->selected_row][ui->selected_col] : -1;

    save_game(ui->game);
    update_ui(ui);

    /* Check for solved condition */
    if (is_complete(ui->game->grid)) {
        bool all_valid = true;
        for (int i = 0; i < SIZE && all_valid; i++)
            for (int j = 0; j < SIZE; j++)
                if (ui->game->grid[i][j] != 0 && !is_valid(ui->game->grid, i, j, ui->game->grid[i][j])) { all_valid = false; break; }
        if (all_valid) {
            char st[128];
            int s = ui->game->seconds_elapsed % 60;
            int m = (ui->game->seconds_elapsed / 60) % 60;
            snprintf(st, sizeof(st), "🎉 Puzzle solved! Time %02d:%02d – Score: %d", m, s, ui->game->score);
            gtk_label_set_text(GTK_LABEL(ui->status_label), st);
            ui->game->game_over = true;
            set_number_pad_sensitive(ui, false);
            if (ui->timer_id) { g_source_remove(ui->timer_id); ui->timer_id = 0; }
            save_game(ui->game);
        }
    }
}

/* Clear cell action (button) */
void on_clear_cell_clicked(GtkButton *btn, UIState *ui) {
    if (ui->selected_row != -1 && ui->selected_col != -1) {
        if (ui->game->original[ui->selected_row][ui->selected_col] == 0) {
            ui->game->grid[ui->selected_row][ui->selected_col] = 0;
            ui->game->validation[ui->selected_row][ui->selected_col] = 0;
            save_game(ui->game);
            update_ui(ui);
            gtk_label_set_text(GTK_LABEL(ui->status_label), "Cell cleared");
            ui->selected_number = -1;
        } else {
            gtk_label_set_text(GTK_LABEL(ui->status_label), "Cannot clear original cells!");
        }
    } else {
        gtk_label_set_text(GTK_LABEL(ui->status_label), "Please select a cell first!");
    }
}

/* Hint action */
void on_hint_clicked(GtkButton *btn, UIState *ui) {
    if (ui->selected_row != -1 && ui->selected_col != -1) {
        if (ui->game->original[ui->selected_row][ui->selected_col] != 0) {
            gtk_label_set_text(GTK_LABEL(ui->status_label), "This is an original cell!");
            return;
        }
        if (ui->game->grid[ui->selected_row][ui->selected_col] == 0) {
            ui->game->grid[ui->selected_row][ui->selected_col] =
                ui->game->solution[ui->selected_row][ui->selected_col];
            ui->game->validation[ui->selected_row][ui->selected_col] = 1;
            gtk_label_set_text(GTK_LABEL(ui->status_label), "💡 Hint revealed!");
            save_game(ui->game);
            update_ui(ui);
        } else {
            gtk_label_set_text(GTK_LABEL(ui->status_label), "Cell already filled!");
        }
    } else {
        gtk_label_set_text(GTK_LABEL(ui->status_label), "Please select a cell first!");
    }
}

/* Reset action */
void on_reset_clicked(GtkButton *btn, UIState *ui) {
    copy_grid(ui->game->original, ui->game->grid);
    memset(ui->game->validation, 0, sizeof(ui->game->validation));
    ui->game->steps = 0;
    ui->selected_number = -1;
    ui->game->score = 0;
    ui->game->mistakes = 0;
    ui->game->seconds_elapsed = 0;
    ui->game->game_over = false;
    if (ui->timer_id) { g_source_remove(ui->timer_id); ui->timer_id = 0; }
    /* restart timer */
    ui->timer_id = g_timeout_add_seconds(1, timer_tick, ui);
    set_number_pad_sensitive(ui, true);
    save_game(ui->game);
    update_ui(ui);
    gtk_label_set_text(GTK_LABEL(ui->status_label), "Game reset to initial state");
}

/* Solve action - NOW USES DLX ALGORITHM */
void on_solve_clicked(GtkButton *btn, UIState *ui) {
    copy_grid(ui->game->grid, ui->game->solution);
    ui->game->steps = 0;
    ui->game->solving = true;
    
    /* Use Dancing Links Algorithm X to solve */
    solve_dlx(ui->game, ui->game->solution);
    
    copy_grid(ui->game->solution, ui->game->grid);
    for (int i = 0; i < SIZE; i++)
        for (int j = 0; j < SIZE; j++)
            if (ui->game->original[i][j] == 0) ui->game->validation[i][j] = 1;
    update_ui(ui);
    char status[256];
    snprintf(status, sizeof(status), "✓ Puzzle solved using DLX in %d steps!", ui->game->steps);
    gtk_label_set_text(GTK_LABEL(ui->status_label), status);
    ui->game->game_over = true;
    set_number_pad_sensitive(ui, false);
    if (ui->timer_id) { g_source_remove(ui->timer_id); ui->timer_id = 0; }
    save_game(ui->game);
}

/* ========== MENU and UI BUILD ========== */

void create_game_ui(UIState *ui);

void start_new_game(GtkButton *btn, gpointer user_data) {
    UIState *ui = (UIState *)user_data;
    Difficulty diff = GPOINTER_TO_INT(g_object_get_data(G_OBJECT(btn), "difficulty"));

    generate_complete_grid(ui->game->solution);
    copy_grid(ui->game->solution, ui->game->grid);
    remove_numbers(ui->game->grid, diff);
    copy_grid(ui->game->grid, ui->game->original);
    memset(ui->game->validation, 0, sizeof(ui->game->validation));
    ui->game->steps = 0;
    ui->game->difficulty = diff;

    /* reset gameplay counters */
    ui->game->score = 0;
    ui->game->mistakes = 0;
    ui->game->seconds_elapsed = 0;
    ui->game->game_over = false;

    ui->selected_row = -1; ui->selected_col = -1; ui->selected_number = -1;

    save_game(ui->game);

    if (ui->menu_container) gtk_widget_set_visible(ui->menu_container, FALSE);
    create_game_ui(ui);

    /* start timer */
    if (ui->timer_id) { g_source_remove(ui->timer_id); ui->timer_id = 0; }
    ui->timer_id = g_timeout_add_seconds(1, timer_tick, ui);
}

void continue_game_cb(GtkButton *btn, gpointer user_data) {
    UIState *ui = (UIState *)user_data;
    if (load_game(ui->game)) {
        ui->selected_row = -1; ui->selected_col = -1; ui->selected_number = -1;
        if (ui->menu_container) gtk_widget_set_visible(ui->menu_container, FALSE);
        create_game_ui(ui);
        /* resume timer from saved seconds */
        if (ui->timer_id) { g_source_remove(ui->timer_id); ui->timer_id = 0; }
        if (!ui->game->game_over) {
            ui->timer_id = g_timeout_add_seconds(1, timer_tick, ui);
        } else {
            set_number_pad_sensitive(ui, false);
        }
    }
}

void create_main_menu(UIState *ui) {
    ui->menu_container = gtk_box_new(GTK_ORIENTATION_VERTICAL, 20);
    gtk_window_set_child(ui->window, ui->menu_container);
    gtk_widget_set_margin_start(ui->menu_container, 30);
    gtk_widget_set_margin_end(ui->menu_container, 30);
    gtk_widget_set_margin_top(ui->menu_container, 50);
    gtk_widget_set_margin_bottom(ui->menu_container, 50);
    gtk_widget_set_halign(ui->menu_container, GTK_ALIGN_CENTER);
    gtk_widget_set_valign(ui->menu_container, GTK_ALIGN_CENTER);

    GtkWidget *title = gtk_label_new("SUDOKU");
    PangoAttrList *attrs = pango_attr_list_new();
    pango_attr_list_insert(attrs, pango_attr_size_new(36 * 1024));
    pango_attr_list_insert(attrs, pango_attr_weight_new(PANGO_WEIGHT_BOLD));
    gtk_label_set_attributes(GTK_LABEL(title), attrs);
    gtk_box_append(GTK_BOX(ui->menu_container), title);
    pango_attr_list_unref(attrs);

    if (has_saved_game()) {
        GtkWidget *continue_btn = gtk_button_new_with_label("Continue Game");
        gtk_box_append(GTK_BOX(ui->menu_container), continue_btn);
        g_signal_connect(continue_btn, "clicked", G_CALLBACK(continue_game_cb), ui);
    }

    GtkWidget *new_game_label = gtk_label_new("New Game - Select Difficulty");
    gtk_box_append(GTK_BOX(ui->menu_container), new_game_label);

    const char *difficulties[] = {"Beginner", "Medium", "Hard", "Expert"};
    Difficulty diff_values[] = {DIFFICULTY_EASY, DIFFICULTY_MEDIUM, DIFFICULTY_HARD, DIFFICULTY_EXPERT};

    for (int i = 0; i < 4; i++) {
        GtkWidget *btn = gtk_button_new_with_label(difficulties[i]);
        gtk_box_append(GTK_BOX(ui->menu_container), btn);
        g_object_set_data(G_OBJECT(btn), "difficulty", GINT_TO_POINTER(diff_values[i]));
        g_signal_connect(btn, "clicked", G_CALLBACK(start_new_game), ui);
    }
}

void create_game_ui(UIState *ui) {
    /* main container */
    ui->main_container = gtk_box_new(GTK_ORIENTATION_VERTICAL, 8);
    gtk_window_set_child(ui->window, ui->main_container);
    gtk_widget_set_margin_start(ui->main_container, 15);
    gtk_widget_set_margin_end(ui->main_container, 15);
    gtk_widget_set_margin_top(ui->main_container, 15);
    gtk_widget_set_margin_bottom(ui->main_container, 15);

    /* Title */
    GtkWidget *title = gtk_label_new("SUDOKU");
    PangoAttrList *attrs = pango_attr_list_new();
    pango_attr_list_insert(attrs, pango_attr_size_new(24 * 1024));
    pango_attr_list_insert(attrs, pango_attr_weight_new(PANGO_WEIGHT_BOLD));
    gtk_label_set_attributes(GTK_LABEL(title), attrs);
    gtk_box_append(GTK_BOX(ui->main_container), title);
    gtk_widget_set_halign(title, GTK_ALIGN_CENTER);
    pango_attr_list_unref(attrs);

    /* Info bar (Score / Mistakes / Difficulty / Timer) - equally spaced */
    GtkWidget *info_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 20);
    gtk_widget_set_halign(info_box, GTK_ALIGN_CENTER);
    gtk_box_append(GTK_BOX(ui->main_container), info_box);

    ui->score_label = gtk_label_new("Score: 0");
    ui->mistakes_label = gtk_label_new("Mistakes: 0/3");
    ui->difficulty_label = gtk_label_new(difficulty_to_string(ui->game->difficulty));
    ui->timer_label = gtk_label_new("00:00");

    /* Use a grid so items are equally spaced in the center */
    GtkWidget *info_grid = gtk_grid_new();
    gtk_grid_set_column_spacing(GTK_GRID(info_grid), 20);
    gtk_box_append(GTK_BOX(info_box), info_grid);

    gtk_grid_attach(GTK_GRID(info_grid), ui->score_label, 0, 0, 1, 1);
    gtk_grid_attach(GTK_GRID(info_grid), ui->mistakes_label, 1, 0, 1, 1);
    gtk_grid_attach(GTK_GRID(info_grid), ui->difficulty_label, 2, 0, 1, 1);
    gtk_grid_attach(GTK_GRID(info_grid), ui->timer_label, 3, 0, 1, 1);

    /* Drawing area for Cairo grid */
    ui->drawing_area = gtk_drawing_area_new();
    gtk_widget_set_hexpand(ui->drawing_area, TRUE);
    gtk_widget_set_vexpand(ui->drawing_area, TRUE);
    gtk_drawing_area_set_draw_func(GTK_DRAWING_AREA(ui->drawing_area), draw_sudoku, ui, NULL);

    /* Click handling */
    GtkGesture *click = gtk_gesture_click_new();
    g_signal_connect(click, "pressed", G_CALLBACK(on_click), ui);
    gtk_widget_add_controller(ui->drawing_area, GTK_EVENT_CONTROLLER(click));

    gtk_box_append(GTK_BOX(ui->main_container), ui->drawing_area);

    /* Number pad */
    GtkWidget *pad_label = gtk_label_new("Enter Number:");
    gtk_box_append(GTK_BOX(ui->main_container), pad_label);
    gtk_widget_set_halign(pad_label, GTK_ALIGN_CENTER);
    gtk_widget_set_margin_top(pad_label, 10);

    GtkWidget *pad_box = gtk_grid_new();
    gtk_grid_set_row_spacing(GTK_GRID(pad_box), 8);
    gtk_grid_set_column_spacing(GTK_GRID(pad_box), 8);
    gtk_box_append(GTK_BOX(ui->main_container), pad_box);
    gtk_widget_set_halign(pad_box, GTK_ALIGN_CENTER);

    for (int i = 1; i <= 9; i++) {
        char lbl[3];
        snprintf(lbl, sizeof(lbl), "%d", i);
        GtkWidget *btn = gtk_button_new_with_label(lbl);
        ui->number_pad[i] = btn;
        gtk_widget_add_css_class(btn, "number-btn");
        /* arrange in two rows: 5 cols first, then 4 next */
        gtk_grid_attach(GTK_GRID(pad_box), btn, (i - 1) % 5, (i - 1) / 5, 1, 1);
        g_signal_connect(btn, "clicked", G_CALLBACK(on_number_clicked), ui);
    }

    /* Actions */
    GtkWidget *action_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
    gtk_box_append(GTK_BOX(ui->main_container), action_box);
    gtk_widget_set_halign(action_box, GTK_ALIGN_CENTER);
    gtk_widget_set_margin_top(action_box, 10);

    GtkWidget *clear_btn = gtk_button_new_with_label("Clear");
    gtk_widget_add_css_class(clear_btn, "action-btn");
    gtk_box_append(GTK_BOX(action_box), clear_btn);
    g_signal_connect(clear_btn, "clicked", G_CALLBACK(on_clear_cell_clicked), ui);

    GtkWidget *hint_btn = gtk_button_new_with_label("💡 Hint");
    gtk_widget_add_css_class(hint_btn, "action-btn");
    gtk_box_append(GTK_BOX(action_box), hint_btn);
    g_signal_connect(hint_btn, "clicked", G_CALLBACK(on_hint_clicked), ui);

    GtkWidget *solve_btn = gtk_button_new_with_label("Solve");
    gtk_widget_add_css_class(solve_btn, "action-btn");
    gtk_box_append(GTK_BOX(action_box), solve_btn);
    g_signal_connect(solve_btn, "clicked", G_CALLBACK(on_solve_clicked), ui);

    GtkWidget *reset_btn = gtk_button_new_with_label("Reset");
    gtk_widget_add_css_class(reset_btn, "action-btn");
    gtk_box_append(GTK_BOX(action_box), reset_btn);
    g_signal_connect(reset_btn, "clicked", G_CALLBACK(on_reset_clicked), ui);

    ui->status_label = gtk_label_new("Select a cell and enter a number");
    gtk_box_append(GTK_BOX(ui->main_container), ui->status_label);
    gtk_widget_set_halign(ui->status_label, GTK_ALIGN_CENTER);
    gtk_widget_set_margin_top(ui->status_label, 10);

    /* sensitive state */
    if (ui->game->game_over) set_number_pad_sensitive(ui, false);
    else set_number_pad_sensitive(ui, true);

    update_ui(ui);
}

/* ========== ACTIVATE / MAIN ========== */
void activate(GtkApplication *app, gpointer user_data) {
    srand((unsigned)time(NULL));

    UIState *ui = calloc(1, sizeof(UIState));
    ui->game = calloc(1, sizeof(SudokuGame));
    ui->selected_row = -1; ui->selected_col = -1; ui->selected_number = -1;
    ui->timer_id = 0;

    GtkWidget *window = gtk_application_window_new(app);
    ui->window = GTK_WINDOW(window);
    gtk_window_set_title(GTK_WINDOW(window), "Sudoku DLX Solver");
    gtk_window_set_default_size(GTK_WINDOW(window), 600, 800);

    /* Basic CSS */
    GtkCssProvider *provider = gtk_css_provider_new();
    gtk_css_provider_load_from_string(provider,
        "window { background: white; }"
        " .number-btn { font-size: 20px; padding: 12px; min-width: 55px; min-height: 50px; font-weight: bold; color: #4a90e2; border: 2px solid #4a90e2; border-radius: 5px; }"
        " .action-btn { font-size: 14px; padding: 10px 20px; font-weight: bold; border-radius: 5px; }"
    );
    gtk_style_context_add_provider_for_display(gdk_display_get_default(), GTK_STYLE_PROVIDER(provider), GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);

    create_main_menu(ui);
    gtk_window_present(GTK_WINDOW(window));
}

int main(int argc, char **argv) {
    GtkApplication *app = gtk_application_new("org.sudoku.dlx.solver", G_APPLICATION_DEFAULT_FLAGS);
    g_signal_connect(app, "activate", G_CALLBACK(activate), NULL);
    int status = g_application_run(G_APPLICATION(app), argc, argv);
    g_object_unref(app);
    return status;
}