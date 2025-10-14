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
    GtkWidget *number_pad[10];
    GtkWidget *status_label;
    GtkWidget *score_label;
    GtkWidget *mistakes_label;
    GtkWidget *difficulty_label;
    GtkWidget *timer_label;
    int selected_row, selected_col;
    int selected_number;
    guint timer_id;
    SudokuGame *game;
} UIState;

/* Forward declarations */
void create_game_ui(UIState *ui);
void create_main_menu(UIState *ui);
gboolean timer_tick(gpointer data);
void update_ui(UIState *ui);
void set_number_pad_sensitive(UIState *ui, bool sensitive);
void on_number_clicked(GtkButton *btn, UIState *ui);

/* ========== DANCING LINKS DATA STRUCTURES ========== */

typedef struct DLXNode {
    struct DLXNode *left, *right, *up, *down;
    struct DLXNode *column;
    int row_id;
    int size;
} DLXNode;

typedef struct {
    DLXNode *header;
    DLXNode *columns[324];
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
static inline bool is_valid_placement(int grid[SIZE][SIZE], int row, int col, int num) {
    for (int i = 0; i < SIZE; i++) {
        if (grid[row][i] == num || grid[i][col] == num) return false;
    }
    
    int sr = (row / SUBGRID) * SUBGRID;
    int sc = (col / SUBGRID) * SUBGRID;
    int er = sr + SUBGRID;
    int ec = sc + SUBGRID;
    
    for (int i = sr; i < er; i++)
        for (int j = sc; j < ec; j++)
            if (grid[i][j] == num) return false;
    return true;
}

bool fill_grid(int grid[SIZE][SIZE], int row, int col) {
    if (row == SIZE) return true;
    
    int next_row = row;
    int next_col = col + 1;
    if (next_col == SIZE) { 
        next_row++; 
        next_col = 0; 
    }

    int nums[SIZE];
    for (int i = 0; i < SIZE; i++) nums[i] = i + 1;
    for (int i = SIZE - 1; i > 0; i--) {
        int j = rand() % (i + 1);
        int tmp = nums[i]; 
        nums[i] = nums[j]; 
        nums[j] = tmp;
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
        int r = rand() % SIZE;
        int c = rand() % SIZE;
        if (grid[r][c] != 0) { 
            grid[r][c] = 0; 
            removed++; 
        }
    }
}

static inline void copy_grid(int src[SIZE][SIZE], int dst[SIZE][SIZE]) {
    memcpy(dst, src, sizeof(int) * SIZE * SIZE);
}

static inline bool is_valid(int grid[SIZE][SIZE], int row, int col, int num) {
    for (int i = 0; i < SIZE; i++) {
        if (i != col && grid[row][i] == num) return false;
        if (i != row && grid[i][col] == num) return false;
    }
    
    int sr = (row / SUBGRID) * SUBGRID;
    int sc = (col / SUBGRID) * SUBGRID;
    int er = sr + SUBGRID;
    int ec = sc + SUBGRID;
    
    for (int i = sr; i < er; i++)
        for (int j = sc; j < ec; j++)
            if (!(i == row && j == col) && grid[i][j] == num) return false;
    return true;
}

static inline bool is_complete(int grid[SIZE][SIZE]) {
    for (int i = 0; i < SIZE; i++)
        for (int j = 0; j < SIZE; j++)
            if (grid[i][j] == 0) return false;
    return true;
}

/* ========== DANCING LINKS ALGORITHM ========== */

static inline DLXNode* create_node() {
    DLXNode *node = (DLXNode*)calloc(1, sizeof(DLXNode));
    node->left = node->right = node->up = node->down = node;
    node->column = NULL;
    node->row_id = -1;
    node->size = 0;
    return node;
}

static inline void cover(DLXNode *col) {
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

static inline void uncover(DLXNode *col) {
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
    
    DLXNode *col = NULL;
    int min_size = INT_MAX;
    for (DLXNode *c = solver->header->right; c != solver->header; c = c->right) {
        if (c->size < min_size) {
            min_size = c->size;
            col = c;
            if (min_size <= 1) break;
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

void init_dlx_solver(DLXSolver *solver, int grid[SIZE][SIZE]) {
    solver->header = create_node();
    solver->solution_count = 0;
    
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
    
    for (int r = 0; r < SIZE; r++) {
        for (int c = 0; c < SIZE; c++) {
            int start_num = (grid[r][c] != 0) ? grid[r][c] : 1;
            int end_num = (grid[r][c] != 0) ? grid[r][c] : 9;
            
            for (int num = start_num; num <= end_num; num++) {
                int row_id = r * 81 + c * 9 + (num - 1);
                int box = (r / 3) * 3 + (c / 3);
                
                int constraints[4] = {
                    r * 9 + c,
                    81 + r * 9 + (num - 1),
                    162 + c * 9 + (num - 1),
                    243 + box * 9 + (num - 1)
                };
                
                DLXNode *prev_node = NULL;
                for (int i = 0; i < 4; i++) {
                    DLXNode *node = create_node();
                    node->row_id = row_id;
                    node->column = solver->columns[constraints[i]];
                    
                    node->up = node->column->up;
                    node->down = node->column;
                    node->column->up->down = node;
                    node->column->up = node;
                    node->column->size++;
                    
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
    if (!solver) return;
    
    for (int i = 0; i < 324; i++) {
        if (!solver->columns[i]) continue;
        
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
    if (solver->header) free(solver->header);
}

bool solve_dlx(SudokuGame *game, int grid[SIZE][SIZE]) {
    DLXSolver solver;
    solver.game = game;
    
    init_dlx_solver(&solver, grid);
    bool result = search(&solver, 0);
    
    if (result) {
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

    cairo_set_source_rgb(cr, 1, 1, 1);
    cairo_paint(cr);

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
                cairo_set_source_rgb(cr, 0.71, 0.86, 1.0);
                cairo_rectangle(cr, start_x + c * cell_size, start_y + r * cell_size, cell_size, cell_size);
                cairo_fill(cr);
            } else if (highlight) {
                cairo_set_source_rgb(cr, 0.91, 0.94, 1.0);
                cairo_rectangle(cr, start_x + c * cell_size, start_y + r * cell_size, cell_size, cell_size);
                cairo_fill(cr);
            }
        }
    }

    cairo_set_source_rgb(cr, 0, 0, 0);
    for (int i = 0; i <= SIZE; i++) {
        cairo_set_line_width(cr, (i % 3 == 0) ? 3.0 : 1.0);
        cairo_move_to(cr, start_x, start_y + i * cell_size);
        cairo_line_to(cr, start_x + grid_size, start_y + i * cell_size);
        cairo_stroke(cr);
        cairo_move_to(cr, start_x + i * cell_size, start_y);
        cairo_line_to(cr, start_x + i * cell_size, start_y + grid_size);
        cairo_stroke(cr);
    }

    cairo_select_font_face(cr, "Sans", CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_BOLD);
    cairo_set_font_size(cr, cell_size * 0.5);

    for (int r = 0; r < SIZE; r++) {
        for (int c = 0; c < SIZE; c++) {
            int val = game->grid[r][c];
            if (val == 0) continue;
            
            char num[2];
            num[0] = '0' + val;
            num[1] = '\0';

            cairo_text_extents_t ext;
            cairo_text_extents(cr, num, &ext);

            double x = start_x + c * cell_size + (cell_size - ext.width) / 2 - ext.x_bearing;
            double y = start_y + r * cell_size + (cell_size - ext.height) / 2 - ext.y_bearing;

            if (game->original[r][c] != 0) {
                cairo_set_source_rgb(cr, 0, 0, 0);
            } else if (game->validation[r][c] == 2) {
                cairo_set_source_rgb(cr, 0.9, 0.1, 0.1);
            } else {
                cairo_set_source_rgb(cr, 0.2, 0.2, 0.8);
            }

            cairo_move_to(cr, x, y);
            cairo_show_text(cr, num);
        }
    }

    cairo_set_source_rgb(cr, 0, 0, 0);
    cairo_set_line_width(cr, 4.0);
    cairo_rectangle(cr, start_x, start_y, grid_size, grid_size);
    cairo_stroke(cr);
}

static gboolean on_click(GtkGestureClick *gesture, int n_press, double x, double y, gpointer data) {
    GtkWidget *widget = gtk_event_controller_get_widget(GTK_EVENT_CONTROLLER(gesture));
    int width = gtk_widget_get_width(widget);
    int height = gtk_widget_get_height(widget);
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

/* ========== DIALOGS ========== */

void show_info_dialog(GtkWindow *parent, const char *title, const char *message) {
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

typedef struct {
    void (*callback)(UIState*);
    UIState *ui;
} ConfirmData;

void on_confirm_response(GtkDialog *dialog, int response, gpointer user_data) {
    ConfirmData *data = (ConfirmData*)user_data;
    
    if (response == GTK_RESPONSE_YES && data->callback) {
        data->callback(data->ui);
    }
    
    gtk_window_destroy(GTK_WINDOW(dialog));
}

void show_confirm_dialog(UIState *ui, const char *title, const char *message, void (*callback)(UIState*)) {
    GtkWidget *dialog = gtk_dialog_new_with_buttons(
        title,
        ui->window,
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
    
    ConfirmData *data = g_new(ConfirmData, 1);
    data->callback = callback;
    data->ui = ui;
    
    g_signal_connect(dialog, "response", G_CALLBACK(on_confirm_response), data);
    g_signal_connect_swapped(dialog, "destroy", G_CALLBACK(g_free), data);
    gtk_window_present(GTK_WINDOW(dialog));
}

/* ========== UI HELPERS ========== */

static const char *difficulty_to_string(Difficulty d) {
    switch (d) {
        case DIFFICULTY_EASY: return "Beginner";
        case DIFFICULTY_MEDIUM: return "Medium";
        case DIFFICULTY_HARD: return "Hard";
        case DIFFICULTY_EXPERT: return "Expert";
        default: return "Unknown";
    }
}

void set_number_pad_sensitive(UIState *ui, bool sensitive) {
    for (int i = 1; i <= 9; i++) {
        if (ui->number_pad[i])
            gtk_widget_set_sensitive(ui->number_pad[i], sensitive);
    }
}

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

    if (is_complete(ui->game->grid)) {
        bool all_valid = true;
        for (int i = 0; i < SIZE && all_valid; i++)
            for (int j = 0; j < SIZE; j++)
                if (ui->game->grid[i][j] != 0 && !is_valid(ui->game->grid, i, j, ui->game->grid[i][j])) { 
                    all_valid = false; 
                    break; 
                }
        if (all_valid) {
            char st[128];
            int s = ui->game->seconds_elapsed % 60;
            int m = (ui->game->seconds_elapsed / 60) % 60;
            snprintf(st, sizeof(st), "Puzzle solved! Time %02d:%02d – Score: %d", m, s, ui->game->score);
            gtk_label_set_text(GTK_LABEL(ui->status_label), st);
            ui->game->game_over = true;
            set_number_pad_sensitive(ui, false);
            if (ui->timer_id) { g_source_remove(ui->timer_id); ui->timer_id = 0; }
            save_game(ui->game);
            
            char msg[256];
            snprintf(msg, sizeof(msg), "Congratulations! You solved the puzzle in %02d:%02d with a score of %d points!", m, s, ui->game->score);
            show_info_dialog(ui->window, "Puzzle Complete!", msg);
            
            return G_SOURCE_REMOVE;
        }
    }

    if (ui->game->mistakes >= MAX_MISTAKES) {
        gtk_label_set_text(GTK_LABEL(ui->status_label), "Game Over – Too many mistakes!");
        ui->game->game_over = true;
        set_number_pad_sensitive(ui, false);
        save_game(ui->game);
        show_info_dialog(ui->window, "Game Over", "You've made too many mistakes! Try again or start a new game.");
        return G_SOURCE_REMOVE;
    }

    return G_SOURCE_CONTINUE;
}

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
            gtk_label_set_text(GTK_LABEL(ui->status_label), "Hint revealed!");
            save_game(ui->game);
            update_ui(ui);
        } else {
            gtk_label_set_text(GTK_LABEL(ui->status_label), "Cell already filled!");
        }
    } else {
        gtk_label_set_text(GTK_LABEL(ui->status_label), "Please select a cell first!");
    }
}

void on_reset_clicked(GtkButton *btn, UIState *ui) {
    copy_grid(ui->game->original, ui->game->grid);
    memset(ui->game->validation, 0, sizeof(ui->game->validation));
    ui->game->steps = 0;
    ui->selected_number = -1;
    ui->game->score = 0;
    ui->game->mistakes = 0;
    ui->game->seconds_elapsed = 0;
    ui->game->game_over = false;
    
    /* Remove old timer if running */
    if (ui->timer_id > 0) {
        g_source_remove(ui->timer_id);
        ui->timer_id = 0;
    }
    
    ui->timer_id = g_timeout_add_seconds(1, timer_tick, ui);
    set_number_pad_sensitive(ui, true);
    save_game(ui->game);
    update_ui(ui);
    gtk_label_set_text(GTK_LABEL(ui->status_label), "Game reset to initial state");
}

void on_solve_clicked(GtkButton *btn, UIState *ui) {
    copy_grid(ui->game->grid, ui->game->solution);
    ui->game->steps = 0;
    ui->game->solving = true;
    
    bool solved = solve_dlx(ui->game, ui->game->solution);
    
    if (solved) {
        copy_grid(ui->game->solution, ui->game->grid);
        for (int i = 0; i < SIZE; i++)
            for (int j = 0; j < SIZE; j++)
                if (ui->game->original[i][j] == 0) ui->game->validation[i][j] = 1;
        update_ui(ui);
        char status[256];
        snprintf(status, sizeof(status), "Puzzle solved using DLX in %d steps!", ui->game->steps);
        gtk_label_set_text(GTK_LABEL(ui->status_label), status);
        ui->game->game_over = true;
        set_number_pad_sensitive(ui, false);
        if (ui->timer_id) { g_source_remove(ui->timer_id); ui->timer_id = 0; }
        save_game(ui->game);
        
        show_info_dialog(ui->window, "Puzzle Solved!", 
                          "The puzzle has been solved using Donald Knuth's Dancing Links Algorithm!");
    } else {
        show_info_dialog(ui->window, "Error", "Could not solve the puzzle!");
    }
}

void return_to_menu(UIState *ui) {
    if (ui->timer_id) {
        g_source_remove(ui->timer_id);
        ui->timer_id = 0;
    }
    
    if (ui->main_container) {
        gtk_widget_set_visible(ui->main_container, FALSE);
    }
    
    create_main_menu(ui);
}

void restart_game_confirmed(UIState *ui) {
    copy_grid(ui->game->original, ui->game->grid);
    memset(ui->game->validation, 0, sizeof(ui->game->validation));
    ui->game->steps = 0;
    ui->selected_number = -1;
    ui->game->score = 0;
    ui->game->mistakes = 0;
    ui->game->seconds_elapsed = 0;
    ui->game->game_over = false;
    
    if (ui->timer_id) {
        g_source_remove(ui->timer_id);
        ui->timer_id = 0;
    }
    
    ui->timer_id = g_timeout_add_seconds(1, timer_tick, ui);
    set_number_pad_sensitive(ui, true);
    save_game(ui->game);
    update_ui(ui);
    gtk_label_set_text(GTK_LABEL(ui->status_label), "Game restarted!");
}

void on_restart_game_clicked(GtkButton *btn, UIState *ui) {
    show_confirm_dialog(ui, "Restart Game", 
                       "Are you sure you want to restart? All progress will be lost.",
                       restart_game_confirmed);
}

void on_return_to_menu_clicked(GtkButton *btn, UIState *ui) {
    show_confirm_dialog(ui, "Return to Menu", 
                       "Are you sure you want to return to menu? Current game will be saved.",
                       return_to_menu);
}

/* ========== MENU AND UI BUILD ========== */

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

    ui->game->score = 0;
    ui->game->mistakes = 0;
    ui->game->seconds_elapsed = 0;
    ui->game->game_over = false;

    ui->selected_row = -1; ui->selected_col = -1; ui->selected_number = -1;

    save_game(ui->game);

    if (ui->menu_container) gtk_widget_set_visible(ui->menu_container, FALSE);
    create_game_ui(ui);

    if (ui->timer_id) { g_source_remove(ui->timer_id); ui->timer_id = 0; }
    ui->timer_id = g_timeout_add_seconds(1, timer_tick, ui);
}

void continue_game_cb(GtkButton *btn, gpointer user_data) {
    UIState *ui = (UIState *)user_data;
    if (load_game(ui->game)) {
        ui->selected_row = -1; ui->selected_col = -1; ui->selected_number = -1;
        if (ui->menu_container) gtk_widget_set_visible(ui->menu_container, FALSE);
        create_game_ui(ui);
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
    ui->main_container = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    gtk_window_set_child(ui->window, ui->main_container);

    GtkWidget *top_area = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    gtk_box_append(GTK_BOX(ui->main_container), top_area);

    GtkWidget *menu_bar = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
    gtk_widget_set_margin_top(menu_bar, 10);
    gtk_widget_set_margin_start(menu_bar, 15);
    gtk_widget_set_margin_end(menu_bar, 15);
    gtk_widget_set_halign(menu_bar, GTK_ALIGN_START);
    gtk_box_append(GTK_BOX(top_area), menu_bar);

    GtkWidget *menu_btn_top = gtk_button_new_with_label("Menu");
    gtk_widget_add_css_class(menu_btn_top, "menu-btn");
    gtk_box_append(GTK_BOX(menu_bar), menu_btn_top);
    g_signal_connect(menu_btn_top, "clicked", G_CALLBACK(on_return_to_menu_clicked), ui);

    GtkWidget *title_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
    gtk_widget_set_halign(title_box, GTK_ALIGN_CENTER);
    gtk_widget_set_margin_top(title_box, 5);
    gtk_box_append(GTK_BOX(top_area), title_box);

    GtkWidget *title = gtk_label_new("SUDOKU");
    PangoAttrList *attrs = pango_attr_list_new();
    pango_attr_list_insert(attrs, pango_attr_size_new(24 * 1024));
    pango_attr_list_insert(attrs, pango_attr_weight_new(PANGO_WEIGHT_BOLD));
    gtk_label_set_attributes(GTK_LABEL(title), attrs);
    gtk_box_append(GTK_BOX(title_box), title);
    pango_attr_list_unref(attrs);

    GtkWidget *info_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
    gtk_widget_set_halign(info_box, GTK_ALIGN_CENTER);
    gtk_widget_set_margin_bottom(info_box, 10);
    gtk_box_append(GTK_BOX(ui->main_container), info_box);

    ui->score_label = gtk_label_new("Score: 0");
    ui->mistakes_label = gtk_label_new("Mistakes: 0/3");
    ui->difficulty_label = gtk_label_new(difficulty_to_string(ui->game->difficulty));
    ui->timer_label = gtk_label_new("00:00");

    GtkWidget *info_grid = gtk_grid_new();
    gtk_grid_set_column_spacing(GTK_GRID(info_grid), 30);
    gtk_grid_set_column_homogeneous(GTK_GRID(info_grid), TRUE);
    gtk_box_append(GTK_BOX(info_box), info_grid);

    gtk_grid_attach(GTK_GRID(info_grid), ui->score_label, 0, 0, 1, 1);
    gtk_grid_attach(GTK_GRID(info_grid), ui->mistakes_label, 1, 0, 1, 1);
    gtk_grid_attach(GTK_GRID(info_grid), ui->difficulty_label, 2, 0, 1, 1);
    gtk_grid_attach(GTK_GRID(info_grid), ui->timer_label, 3, 0, 1, 1);

    GtkWidget *drawing_container = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    gtk_widget_set_hexpand(drawing_container, TRUE);
    gtk_widget_set_vexpand(drawing_container, TRUE);
    gtk_widget_set_margin_start(drawing_container, 15);
    gtk_widget_set_margin_end(drawing_container, 15);
    gtk_widget_set_margin_top(drawing_container, 10);
    gtk_widget_set_margin_bottom(drawing_container, 10);
    gtk_box_append(GTK_BOX(ui->main_container), drawing_container);

    ui->drawing_area = gtk_drawing_area_new();
    gtk_widget_set_hexpand(ui->drawing_area, TRUE);
    gtk_widget_set_vexpand(ui->drawing_area, TRUE);
    gtk_drawing_area_set_draw_func(GTK_DRAWING_AREA(ui->drawing_area), draw_sudoku, ui, NULL);

    GtkGesture *click = gtk_gesture_click_new();
    g_signal_connect(click, "pressed", G_CALLBACK(on_click), ui);
    gtk_widget_add_controller(ui->drawing_area, GTK_EVENT_CONTROLLER(click));

    gtk_box_append(GTK_BOX(drawing_container), ui->drawing_area);

    GtkWidget *pad_label = gtk_label_new("Enter Number:");
    gtk_box_append(GTK_BOX(ui->main_container), pad_label);
    gtk_widget_set_halign(pad_label, GTK_ALIGN_CENTER);
    gtk_widget_set_margin_top(pad_label, 10);
    gtk_widget_set_margin_bottom(pad_label, 8);

    GtkWidget *pad_container = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    gtk_widget_set_halign(pad_container, GTK_ALIGN_CENTER);
    gtk_box_append(GTK_BOX(ui->main_container), pad_container);

    GtkWidget *pad_box = gtk_grid_new();
    gtk_grid_set_row_spacing(GTK_GRID(pad_box), 8);
    gtk_grid_set_column_spacing(GTK_GRID(pad_box), 8);
    gtk_grid_set_row_homogeneous(GTK_GRID(pad_box), TRUE);
    gtk_grid_set_column_homogeneous(GTK_GRID(pad_box), TRUE);
    gtk_box_append(GTK_BOX(pad_container), pad_box);

    for (int i = 1; i <= 9; i++) {
        char lbl[3];
        snprintf(lbl, sizeof(lbl), "%d", i);
        GtkWidget *btn = gtk_button_new_with_label(lbl);
        ui->number_pad[i] = btn;
        gtk_widget_add_css_class(btn, "number-btn");
        gtk_grid_attach(GTK_GRID(pad_box), btn, (i - 1) % 5, (i - 1) / 5, 1, 1);
        g_signal_connect(btn, "clicked", G_CALLBACK(on_number_clicked), ui);
    }

    GtkWidget *action_container = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    gtk_widget_set_halign(action_container, GTK_ALIGN_CENTER);
    gtk_widget_set_margin_top(action_container, 15);
    gtk_box_append(GTK_BOX(ui->main_container), action_container);

    GtkWidget *action_box = gtk_grid_new();
    gtk_grid_set_column_spacing(GTK_GRID(action_box), 10);
    gtk_grid_set_column_homogeneous(GTK_GRID(action_box), TRUE);
    gtk_box_append(GTK_BOX(action_container), action_box);

    GtkWidget *clear_btn = gtk_button_new_with_label("Clear");
    gtk_widget_add_css_class(clear_btn, "action-btn");
    gtk_grid_attach(GTK_GRID(action_box), clear_btn, 0, 0, 1, 1);
    g_signal_connect(clear_btn, "clicked", G_CALLBACK(on_clear_cell_clicked), ui);

    GtkWidget *hint_btn = gtk_button_new_with_label("Hint");
    gtk_widget_add_css_class(hint_btn, "action-btn");
    gtk_grid_attach(GTK_GRID(action_box), hint_btn, 1, 0, 1, 1);
    g_signal_connect(hint_btn, "clicked", G_CALLBACK(on_hint_clicked), ui);

    GtkWidget *solve_btn = gtk_button_new_with_label("Solve");
    gtk_widget_add_css_class(solve_btn, "action-btn");
    gtk_grid_attach(GTK_GRID(action_box), solve_btn, 2, 0, 1, 1);
    g_signal_connect(solve_btn, "clicked", G_CALLBACK(on_solve_clicked), ui);

    GtkWidget *reset_btn = gtk_button_new_with_label("Reset");
    gtk_widget_add_css_class(reset_btn, "action-btn");
    gtk_grid_attach(GTK_GRID(action_box), reset_btn, 3, 0, 1, 1);
    g_signal_connect(reset_btn, "clicked", G_CALLBACK(on_reset_clicked), ui);

    ui->status_label = gtk_label_new("Select a cell and enter a number");
    gtk_box_append(GTK_BOX(ui->main_container), ui->status_label);
    gtk_widget_set_halign(ui->status_label, GTK_ALIGN_CENTER);
    gtk_widget_set_margin_top(ui->status_label, 12);
    gtk_widget_set_margin_bottom(ui->status_label, 15);

    if (ui->game->game_over) set_number_pad_sensitive(ui, false);
    else set_number_pad_sensitive(ui, true);

    update_ui(ui);
}

/* ========== CLEANUP ========== */
void cleanup_ui(GtkWidget *window, gpointer data) {
    UIState *ui = (UIState *)data;
    if (!ui) return;
    
    /* Stop timer */
    if (ui->timer_id > 0) {
        g_source_remove(ui->timer_id);
        ui->timer_id = 0;
    }
    
    /* Free game state */
    if (ui->game) {
        save_game(ui->game);
        g_free(ui->game);
        ui->game = NULL;
    }
    
    /* Free UI state structure */
    g_free(ui);
}

/* ========== UPDATE UI ========== */
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

void update_ui(UIState *ui) {
    if (ui) {
        update_info_bar(ui);
        if (ui->drawing_area) {
            gtk_widget_queue_draw(ui->drawing_area);
        }
    }
}

/* ========== GAME ACTIONS ========== */

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
        ui->game->grid[ui->selected_row][ui->selected_col] = num;
        if (is_valid(ui->game->grid, ui->selected_row, ui->selected_col, num)) {
            ui->game->validation[ui->selected_row][ui->selected_col] = 1;
            gtk_label_set_text(GTK_LABEL(ui->status_label), "Valid move");

            if (prev == 0) {
                ui->game->score += 10;
            }
        } else {
            ui->game->validation[ui->selected_row][ui->selected_col] = 2;
            ui->game->mistakes += 1;
            char st[128];
            snprintf(st, sizeof(st), "Invalid move (%d/%d mistakes)", ui->game->mistakes, MAX_MISTAKES);
            gtk_label_set_text(GTK_LABEL(ui->status_label), st);

            if (ui->game->mistakes >= MAX_MISTAKES) {
                gtk_label_set_text(GTK_LABEL(ui->status_label), "Game Over – Too many mistakes!");
                ui->game->game_over = true;
                set_number_pad_sensitive(ui, false);
                show_info_dialog(ui->window, "Game Over", "You've made too many mistakes! Try again or start a new game.");
            }
        }
    }

    ui->selected_number = ui->game->grid[ui->selected_row][ui->selected_col] > 0
                          ? ui->game->grid[ui->selected_row][ui->selected_col] : -1;

    save_game(ui->game);
    update_ui(ui);
}

/* ========== ACTIVATE / MAIN ========== */
void activate(GtkApplication *app, gpointer user_data) {
    srand((unsigned)time(NULL));

    UIState *ui = g_new0(UIState, 1);
    ui->game = g_new0(SudokuGame, 1);
    ui->selected_row = -1; 
    ui->selected_col = -1; 
    ui->selected_number = -1;
    ui->timer_id = 0;

    GtkWidget *window = gtk_application_window_new(app);
    ui->window = GTK_WINDOW(window);
    gtk_window_set_title(GTK_WINDOW(window), "Sudoku DLX Solver");
    gtk_window_set_default_size(GTK_WINDOW(window), 600, 800);

    g_signal_connect(window, "destroy", G_CALLBACK(cleanup_ui), ui);

    GtkCssProvider *provider = gtk_css_provider_new();
    gtk_css_provider_load_from_string(provider,
        "window { background: white; }"
        " .number-btn { font-size: 20px; padding: 12px; min-width: 55px; min-height: 50px; font-weight: bold; color: #4a90e2; border: 2px solid #4a90e2; border-radius: 5px; }"
        " .action-btn { font-size: 14px; padding: 10px 20px; font-weight: bold; border-radius: 5px; }"
        " .menu-btn { font-size: 14px; padding: 8px 16px; font-weight: bold; border-radius: 5px; background: #f0f0f0; }"
    );
    gtk_style_context_add_provider_for_display(gdk_display_get_default(), GTK_STYLE_PROVIDER(provider), GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
    g_object_unref(provider);

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