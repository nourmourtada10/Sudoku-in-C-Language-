#include "sudoku.h"

/* ===================== DLX Algorithm X Solver ===================== */

static int DLX_L[DLX_NODES], DLX_R[DLX_NODES], DLX_U[DLX_NODES], DLX_D[DLX_NODES];
static int DLX_C[DLX_NODES], DLX_S[DLX_COLS + 1], DLX_H, DLX_rowhead[DLX_ROWS];
static int DLX_nodeCnt, DLX_ans[DLX_ROWS], DLX_ansK;

static inline int col_cell(int r, int c) { return r*9 + c; }
static inline int col_rownum(int r, int v) { return 81 + r*9 + (v-1); }
static inline int col_colnum(int c, int v) { return 162 + c*9 + (v-1); }
static inline int col_boxnum(int br, int bc, int v) { return 243 + (br*3+bc)*9 + (v-1); }
static inline int box_r(int r) { return r/3; }
static inline int box_c(int c) { return c/3; }

static void dlx_init(void) {
    DLX_H = DLX_COLS;
    DLX_L[DLX_H] = DLX_COLS - 1;
    DLX_R[DLX_H] = 0;
    for (int c = 0; c < DLX_COLS; c++) {
        DLX_L[c] = (c == 0 ? DLX_H : c - 1);
        DLX_R[c] = (c == DLX_COLS - 1 ? DLX_H : c + 1);
        DLX_U[c] = DLX_D[c] = c;
        DLX_S[c] = 0;
    }
    DLX_nodeCnt = DLX_COLS + 1;
    for (int r = 0; r < DLX_ROWS; r++) DLX_rowhead[r] = -1;
    DLX_ansK = 0;
}

static void dlx_addNode(int rowID, int colID) {
    int node = DLX_nodeCnt++;
    DLX_C[node] = colID;
    DLX_D[node] = colID;
    DLX_U[node] = DLX_U[colID];
    DLX_D[DLX_U[colID]] = node;
    DLX_U[colID] = node;
    DLX_S[colID]++;
    if (DLX_rowhead[rowID] == -1) {
        DLX_rowhead[rowID] = node;
        DLX_L[node] = DLX_R[node] = node;
    } else {
        int head = DLX_rowhead[rowID];
        int right = DLX_R[head];
        DLX_R[head] = node;
        DLX_L[node] = head;
        DLX_R[node] = right;
        DLX_L[right] = node;
    }
}

static void dlx_cover(int c) {
    DLX_L[DLX_R[c]] = DLX_L[c];
    DLX_R[DLX_L[c]] = DLX_R[c];
    for (int i = DLX_D[c]; i != c; i = DLX_D[i]) {
        for (int j = DLX_R[i]; j != i; j = DLX_R[j]) {
            DLX_U[DLX_D[j]] = DLX_U[j];
            DLX_D[DLX_U[j]] = DLX_D[j];
            DLX_S[DLX_C[j]]--;
        }
    }
}

static void dlx_uncover(int c) {
    for (int i = DLX_U[c]; i != c; i = DLX_U[i]) {
        for (int j = DLX_L[i]; j != i; j = DLX_L[j]) {
            DLX_S[DLX_C[j]]++;
            DLX_U[DLX_D[j]] = j;
            DLX_D[DLX_U[j]] = j;
        }
    }
    DLX_L[DLX_R[c]] = c;
    DLX_R[DLX_L[c]] = c;
}

static int dlx_chooseCol(void) {
    int best = -1, bS = 1000000000;
    for (int c = DLX_R[DLX_H]; c != DLX_H; c = DLX_R[c]) {
        if (DLX_S[c] < bS) { bS = DLX_S[c]; best = c; }
    }
    return best;
}

static int dlx_search(void) {
    if (DLX_R[DLX_H] == DLX_H) return 1;
    int c = dlx_chooseCol();
    if (c == -1) return 0;
    dlx_cover(c);
    for (int i = DLX_D[c]; i != c; i = DLX_D[i]) {
        DLX_ans[DLX_ansK++] = i;
        for (int j = DLX_R[i]; j != i; j = DLX_R[j]) dlx_cover(DLX_C[j]);
        if (dlx_search()) return 1;
        for (int j = DLX_L[i]; j != i; j = DLX_L[j]) dlx_uncover(DLX_C[j]);
        DLX_ansK--;
    }
    dlx_uncover(c);
    return 0;
}

static void dlx_build_from_board(const int board[N][N]) {
    dlx_init();
    int rowID = 0;
    for (int r = 0; r < 9; r++) {
        for (int c = 0; c < 9; c++) {
            int sv = 1, ev = 9;
            if (board[r][c] != 0) { sv = ev = board[r][c]; }
            for (int v = sv; v <= ev; v++) {
                int cols[4] = { col_cell(r,c), col_rownum(r,v), col_colnum(c,v), col_boxnum(box_r(r),box_c(c),v) };
                DLX_rowhead[rowID] = -1;
                for (int k = 0; k < 4; k++) dlx_addNode(rowID, cols[k]);
                rowID++;
            }
        }
    }
}

static void dlx_extract_solution(int out[N][N]) {
    for (int r = 0; r < 9; r++) for (int c = 0; c < 9; c++) out[r][c] = 0;
    for (int k = 0; k < DLX_ansK; k++) {
        int i = DLX_ans[k], cols[4], t = 0;
        cols[t++] = DLX_C[i];
        for (int j = DLX_R[i]; j != i; j = DLX_R[j]) cols[t++] = DLX_C[j];
        int r = -1, c = -1, v = -1;
        for (int m = 0; m < 4; m++) {
            int cc = cols[m];
            if (cc < 81) { r = cc/9; c = cc%9; }
            else if (cc >= 81 && cc < 162) { v = ((cc-81)%9)+1; }
        }
        if (r >= 0 && c >= 0 && v >= 1) out[r][c] = v;
    }
}

static int sudoku_solve_algoX(const int in[N][N], int out[N][N]) {
    dlx_build_from_board(in);
    if (!dlx_search()) return 0;
    dlx_extract_solution(out);
    return 1;
}

/* ===================== Puzzle Generation ===================== */

static void shuffle(int a[], int n) {
    for (int i = n-1; i > 0; i--) {
        int j = rand() % (i+1);
        int t = a[i]; a[i] = a[j]; a[j] = t;
    }
}

static void clearBoard(int b[N][N]) {
    for (int r = 0; r < N; r++)
        for (int c = 0; c < N; c++)
            b[r][c] = 0;
}

static int fillCellRowWise(int b[N][N], int r, int c) {
    if (r == N) return 1;
    int nr = r, nc = c+1;
    if (nc == N) { nr = r+1; nc = 0; }
    if (b[r][c] != 0) return fillCellRowWise(b, nr, nc);
    int vals[9];
    for (int i = 0; i < 9; i++) vals[i] = i+1;
    shuffle(vals, 9);
    for (int i = 0; i < 9; i++) {
        int v = vals[i];
        if (isSafe(b, r, c, v)) {
            b[r][c] = v;
            if (fillCellRowWise(b, nr, nc)) return 1;
            b[r][c] = 0;
        }
    }
    return 0;
}

static int generateSolvedBoardByRows(int out[N][N]) {
    clearBoard(out);
    return fillCellRowWise(out, 0, 0);
}

static void makePuzzleSimple(int board[N][N], int fixed[N][N], int target_clues) {
    if (target_clues < 17) target_clues = 17;
    if (target_clues > 81) target_clues = 81;
    int idx[81];
    for (int i = 0; i < 81; i++) idx[i] = i;
    shuffle(idx, 81);
    int filled = 81;
    for (int k = 0; k < 81 && filled > target_clues; k++) {
        int pos = idx[k], r = pos/9, c = pos%9;
        if (board[r][c] == 0) continue;
        board[r][c] = 0;
        filled--;
    }
    for (int r = 0; r < N; r++)
        for (int c = 0; c < N; c++)
            fixed[r][c] = (board[r][c] != 0) ? 1 : 0;
}

static int mapComplexityToClues(int level) {
    if (level < 1) level = 1;
    if (level > 10) level = 10;
    const int MIN_CLUES = 22, MAX_CLUES = 60;
    return MIN_CLUES + (level-1)*(MAX_CLUES-MIN_CLUES)/9;
}

int generateNewPuzzle(int board[N][N], int fixed[N][N], int original[N][N], int solution[N][N], int level) {
    int clues = mapComplexityToClues(level);
    int gen[N][N];
    if (!generateSolvedBoardByRows(gen)) return 0;
    copyBoard(board, gen);
    makePuzzleSimple(board, fixed, clues);
    copyBoard(original, board);
    if (!sudoku_solve_algoX(board, solution))
        copyBoard(solution, gen);
    return 1;
}

/* ===================== Core Logic ===================== */

void copyBoard(int dst[N][N], const int src[N][N]) {
    for (int r = 0; r < N; r++)
        for (int c = 0; c < N; c++)
            dst[r][c] = src[r][c];
}

int isSafe(const int board[N][N], int r, int c, int val) {
    for (int x = 0; x < N; x++) if (board[r][x] == val) return 0;
    for (int y = 0; y < N; y++) if (board[y][c] == val) return 0;
    int br = (r/3)*3, bc = (c/3)*3;
    for (int i = 0; i < 3; i++)
        for (int j = 0; j < 3; j++)
            if (board[br+i][bc+j] == val) return 0;
    return 1;
}

int boardComplete(const int board[N][N]) {
    for (int r = 0; r < N; r++)
        for (int c = 0; c < N; c++)
            if (board[r][c] == 0) return 0;
    return 1;
}

static int checkUnit9(const int a[9]) {
    int seen = 0;
    for (int i = 0; i < 9; i++) {
        int v = a[i];
        if (v < 1 || v > 9) return 0;
        int bit = 1 << v;
        if (seen & bit) return 0;
        seen |= bit;
    }
    return 1;
}

int isSolvedByRules(const int b[N][N]) {
    int tmp[9];
    for (int r = 0; r < 9; r++) if (!checkUnit9(b[r])) return 0;
    for (int c = 0; c < 9; c++) {
        for (int r = 0; r < 9; r++) tmp[r] = b[r][c];
        if (!checkUnit9(tmp)) return 0;
    }
    for (int br = 0; br < 9; br += 3) {
        for (int bc = 0; bc < 9; bc += 3) {
            int k = 0;
            for (int i = 0; i < 3; i++)
                for (int j = 0; j < 3; j++)
                    tmp[k++] = b[br+i][bc+j];
            if (!checkUnit9(tmp)) return 0;
        }
    }
    return 1;
}

/* ===================== Save/Load ===================== */

int saveExists(const char *path) {
    FILE *f = fopen(path, "rb");
    if (!f) return 0;
    fclose(f);
    return 1;
}

int saveGame(const char *path, const int board[N][N], const int fixed[N][N],
             const int original[N][N], const int solution[N][N]) {
    SaveData s;
    s.magic = SAVE_MAGIC;
    s.version = SAVE_VERSION;
    for (int r = 0; r < N; r++) {
        for (int c = 0; c < N; c++) {
            s.board[r][c] = board[r][c];
            s.fixed[r][c] = fixed[r][c];
            s.original[r][c] = original[r][c];
            s.solution[r][c] = solution[r][c];
        }
    }
    FILE *f = fopen(path, "wb");
    if (!f) return 0;
    size_t n = fwrite(&s, 1, sizeof(SaveData), f);
    fclose(f);
    return n == sizeof(SaveData);
}

int loadGame(const char *path, int board[N][N], int fixed[N][N],
             int original[N][N], int solution[N][N]) {
    FILE *f = fopen(path, "rb");
    if (!f) return 0;
    SaveData s;
    size_t n = fread(&s, 1, sizeof(SaveData), f);
    fclose(f);
    if (n != sizeof(SaveData) || s.magic != SAVE_MAGIC || s.version != SAVE_VERSION)
        return 0;
    for (int r = 0; r < N; r++) {
        for (int c = 0; c < N; c++) {
            board[r][c] = s.board[r][c];
            fixed[r][c] = s.fixed[r][c];
            original[r][c] = s.original[r][c];
            solution[r][c] = s.solution[r][c];
        }
    }
    return 1;
}

int deleteSave(const char *path) {
    return remove(path) == 0;
}

/* ===================== UI Rendering ===================== */

static int isConflictAt(const int b[N][N], int r, int c) {
    int v = b[r][c];
    if (v == 0) return 0;
    ((int (*)[N])b)[r][c] = 0;
    int bad = !isSafe(b, r, c, v);
    ((int (*)[N])b)[r][c] = v;
    return bad;
}

static void draw_center_text(cairo_t *cr, int x, int y, const char *txt, double r, double g, double b, gboolean bold) {
    PangoLayout *layout = pango_cairo_create_layout(cr);
    PangoFontDescription *desc = pango_font_description_from_string(bold ? "Sans Bold 22" : "Sans 22");
    pango_layout_set_font_description(layout, desc);
    pango_layout_set_text(layout, txt, -1);
    int w, h;
    pango_layout_get_size(layout, &w, &h);
    double px = x - (w / PANGO_SCALE) / 2.0;
    double py = y - (h / PANGO_SCALE) / 2.0 + 2;
    cairo_set_source_rgb(cr, r, g, b);
    cairo_move_to(cr, px, py);
    pango_cairo_show_layout(cr, layout);
    g_object_unref(layout);
    pango_font_description_free(desc);
}

static gboolean on_draw(GtkWidget *w, cairo_t *cr, gpointer user_data) {
    App *a = (App*)user_data;
    cairo_set_source_rgb(cr, 0.98, 0.98, 0.98);
    cairo_paint(cr);

    for (int r = 0; r < 9; r++) {
        for (int c = 0; c < 9; c++) {
            int x = PAD + c*CELL + (c/3);
            int y = PAD + r*CELL + (r/3);
            cairo_set_source_rgb(cr, 1, 1, 1);
            cairo_rectangle(cr, x, y, CELL, CELL);
            cairo_fill(cr);
            if (a->sel_r == r && a->sel_c == c) {
                cairo_set_source_rgb(cr, 0.90, 0.90, 0.90);
                cairo_rectangle(cr, x, y, CELL, CELL);
                cairo_fill(cr);
            }
            int v = a->board[r][c];
            if (v) {
                char buf[3];
                snprintf(buf, sizeof buf, "%d", v);
                int conflict = isConflictAt(a->board, r, c);
                if (a->fixed[r][c])
                    draw_center_text(cr, x+CELL/2, y+CELL/2, buf, 0.12, 0.46, 0.91, TRUE);
                else if (conflict)
                    draw_center_text(cr, x+CELL/2, y+CELL/2, buf, 0.86, 0.13, 0.13, FALSE);
                else
                    draw_center_text(cr, x+CELL/2, y+CELL/2, buf, 0.15, 0.15, 0.15, FALSE);
            }
        }
    }

    cairo_set_source_rgb(cr, 0.75, 0.75, 0.75);
    cairo_set_line_width(cr, 1.0);
    for (int i = 0; i <= 9; i++) {
        double x = PAD + i*CELL + (i/3);
        double y = PAD + i*CELL + (i/3);
        cairo_move_to(cr, PAD, y);
        cairo_line_to(cr, PAD+CELL*9+8, y);
        cairo_move_to(cr, x, PAD);
        cairo_line_to(cr, x, PAD+CELL*9+8);
        cairo_stroke(cr);
    }

    cairo_set_source_rgb(cr, 0.10, 0.10, 0.10);
    cairo_set_line_width(cr, 2.4);
    for (int k = 0; k <= 3; k++) {
        double x = PAD + k*3*CELL + k;
        double y = PAD + k*3*CELL + k;
        cairo_move_to(cr, PAD, y);
        cairo_line_to(cr, PAD+CELL*9+8, y);
        cairo_move_to(cr, x, PAD);
        cairo_line_to(cr, x, PAD+CELL*9+8);
    }
    cairo_stroke(cr);
    cairo_rectangle(cr, PAD, PAD, CELL*9+8, CELL*9+8);
    cairo_stroke(cr);
    return FALSE;
}

static void ui_beep(GtkWidget *w) {
    gdk_display_beep(gtk_widget_get_display(w));
}

static void set_status(App *a, const char *txt) {
    gtk_label_set_use_markup(GTK_LABEL(a->status), TRUE);
    if (txt) {
        char *esc = g_markup_escape_text(txt, -1);
        char *markup = g_strdup_printf("<span size='x-large'>%s</span>", esc);
        gtk_label_set_markup(GTK_LABEL(a->status), markup);
        g_free(markup);
        g_free(esc);
    } else {
        gtk_label_set_markup(GTK_LABEL(a->status), "<span size='x-large'></span>");
    }
    gtk_widget_queue_draw(a->da);
}

static void do_autosave(App *a) {
    saveGame(SAVE_FILE, a->board, a->fixed, a->original, a->solution);
}

/* ===================== Game Over ===================== */

static void game_over_dialog(App *a) {
    GtkWidget *d = gtk_message_dialog_new(GTK_WINDOW(a->win),
        GTK_DIALOG_MODAL, GTK_MESSAGE_INFO, GTK_BUTTONS_NONE,
        "ohhhhh you lost :(((");
    gtk_message_dialog_format_secondary_text(GTK_MESSAGE_DIALOG(d),
        "Would you like to start again or quit the game?");
    gtk_dialog_add_button(GTK_DIALOG(d), "Start again", GTK_RESPONSE_ACCEPT);
    gtk_dialog_add_button(GTK_DIALOG(d), "Quit", GTK_RESPONSE_REJECT);
    int resp = gtk_dialog_run(GTK_DIALOG(d));
    gtk_widget_destroy(d);
    if (resp == GTK_RESPONSE_ACCEPT) {
        copyBoard(a->board, a->original);
        do_autosave(a);
        a->errors = 0;
        a->game_over = 0;
        set_status(a, "Restarted after 3 errors.");
    } else {
        gtk_window_close(GTK_WINDOW(a->win));
    }
}

static void end_game_win(App *a) {
    GtkWidget *d = gtk_message_dialog_new(GTK_WINDOW(a->win),
        GTK_DIALOG_MODAL, GTK_MESSAGE_INFO, GTK_BUTTONS_OK,
        "YOYYYYY  YOU WON !! :D");
    gtk_dialog_run(GTK_DIALOG(d));
    gtk_widget_destroy(d);
}

static void register_error(App *a) {
    a->errors++;
    char buf[64];
    snprintf(buf, sizeof buf, "Erreur %d/3.", a->errors);
    set_status(a, buf);
    ui_beep(a->win);
    if (a->errors >= 3) {
        a->game_over = 1;
        game_over_dialog(a);
    }
}

/* ===================== Actions ===================== */

static void action_place(App *a, int r, int c, int v) {
    if (a->game_over) { ui_beep(a->win); return; }
    if (r < 0 || r >= 9 || c < 0 || c >= 9) return;
    if (a->fixed[r][c]) {
        set_status(a, "Case fixe.");
        register_error(a);
        return;
    }
    int old = a->board[r][c];
    a->board[r][c] = 0;
    if (v != 0 && !isSafe(a->board, r, c, v)) {
        a->board[r][c] = old;
        set_status(a, "Regle violee.");
        register_error(a);
        return;
    }
    a->board[r][c] = v;
    do_autosave(a);
    if (v == 0) set_status(a, "Case effacee.");
    else set_status(a, "OK.");

    if (boardComplete(a->board) && isSolvedByRules(a->board)) {
        a->game_over = 1;
        end_game_win(a);
        deleteSave(SAVE_FILE);
        a->game_over = 0;
    }
}

static void action_hint(App *a) {
    if (a->game_over) { ui_beep(a->win); return; }
    int spots[81][2], cnt = 0;
    for (int r = 0; r < 9; r++)
        for (int c = 0; c < 9; c++)
            if (!a->fixed[r][c] && a->board[r][c] == 0) {
                spots[cnt][0] = r;
                spots[cnt][1] = c;
                cnt++;
            }
    if (!cnt) {
        set_status(a, "Aucune case vide modifiable.");
        ui_beep(a->win);
        return;
    }
    int pick = rand() % cnt;
    int r = spots[pick][0], c = spots[pick][1];
    a->board[r][c] = a->solution[r][c];
    do_autosave(a);
    a->sel_r = r;
    a->sel_c = c;
    set_status(a, "Indice place.");
}

static void action_check(App *a) {
    if (a->game_over) { ui_beep(a->win); return; }
    if (boardComplete(a->board)) {
        if (isSolvedByRules(a->board)) set_status(a, "Grille valide et complete.");
        else { set_status(a, "Grille remplie mais invalide."); ui_beep(a->win); }
    } else {
        int empty = 0;
        for (int r = 0; r < 9; r++)
            for (int c = 0; c < 9; c++)
                if (a->board[r][c] == 0) empty++;
        char buf[64];
        snprintf(buf, sizeof buf, "%d case(s) vide(s).", empty);
        set_status(a, buf);
    }
}

static void action_reset(App *a) {
    copyBoard(a->board, a->original);
    do_autosave(a);
    set_status(a, "Puzzle reinitialise.");
    a->errors = 0;
    a->game_over = 0;
}

static void action_save(App *a) {
    if (!saveGame(SAVE_FILE, a->board, a->fixed, a->original, a->solution))
        set_status(a, "Echec sauvegarde.");
    else
        set_status(a, "Sauvegarde ok.");
}

static void action_load(App *a) {
    if (loadGame(SAVE_FILE, a->board, a->fixed, a->original, a->solution))
        set_status(a, "Partie chargee.");
    else {
        set_status(a, "Aucune sauvegarde valide.");
        ui_beep(a->win);
    }
}

static void action_new(App *a) {
    int level = (int)gtk_range_get_value(GTK_RANGE(a->scale));
    if (!generateNewPuzzle(a->board, a->fixed, a->original, a->solution, level)) {
        set_status(a, "Echec generation.");
        ui_beep(a->win);
        return;
    }
    a->sel_r = a->sel_c = 0;
    a->errors = 0;
    a->game_over = 0;
    do_autosave(a);
    char buf[64];
    int clues = mapComplexityToClues(level);
    snprintf(buf, sizeof buf, "Nouvelle grille (~%d indices).", clues);
    set_status(a, buf);
}

/* ===================== Input Handling ===================== */

static gboolean on_button_press(GtkWidget *w, GdkEventButton *ev, gpointer user_data) {
    App *a = (App*)user_data;
    if (ev->button != 1) return FALSE;
    int x = (int)ev->x - PAD, y = (int)ev->y - PAD;
    if (x < 0 || y < 0) return FALSE;
    int cx = -1, cy = -1;
    for (int c = 0; c < 9; c++) {
        int left = c*CELL + (c/3);
        if (x >= left && x < left+CELL) { cx = c; break; }
    }
    for (int r = 0; r < 9; r++) {
        int top = r*CELL + (r/3);
        if (y >= top && y < top+CELL) { cy = r; break; }
    }
    if (cx >= 0 && cy >= 0) {
        a->sel_r = cy;
        a->sel_c = cx;
        gtk_widget_queue_draw(a->da);
    }
    return TRUE;
}

static gboolean on_key(GtkWidget *w, GdkEventKey *e, gpointer user_data) {
    App *a = (App*)user_data;
    if (a->game_over) { ui_beep(a->win); return TRUE; }
    int r = a->sel_r, c = a->sel_c;
    switch (e->keyval) {
        case GDK_KEY_Up:    if (a->sel_r > 0) a->sel_r--; break;
        case GDK_KEY_Down:  if (a->sel_r < 8) a->sel_r++; break;
        case GDK_KEY_Left:  if (a->sel_c > 0) a->sel_c--; break;
        case GDK_KEY_Right: if (a->sel_c < 8) a->sel_c++; break;
        case GDK_KEY_1: case GDK_KEY_KP_1: action_place(a, r, c, 1); break;
        case GDK_KEY_2: case GDK_KEY_KP_2: action_place(a, r, c, 2); break;
        case GDK_KEY_3: case GDK_KEY_KP_3: action_place(a, r, c, 3); break;
        case GDK_KEY_4: case GDK_KEY_KP_4: action_place(a, r, c, 4); break;
        case GDK_KEY_5: case GDK_KEY_KP_5: action_place(a, r, c, 5); break;
        case GDK_KEY_6: case GDK_KEY_KP_6: action_place(a, r, c, 6); break;
        case GDK_KEY_7: case GDK_KEY_KP_7: action_place(a, r, c, 7); break;
        case GDK_KEY_8: case GDK_KEY_KP_8: action_place(a, r, c, 8); break;
        case GDK_KEY_9: case GDK_KEY_KP_9: action_place(a, r, c, 9); break;
        case GDK_KEY_0: case GDK_KEY_KP_0:
        case GDK_KEY_BackSpace:
        case GDK_KEY_Delete: action_place(a, r, c, 0); break;
        case GDK_KEY_h: case GDK_KEY_H: action_hint(a); break;
        case GDK_KEY_r: case GDK_KEY_R: action_reset(a); break;
        case GDK_KEY_c: case GDK_KEY_C: action_check(a); break;
        case GDK_KEY_s: case GDK_KEY_S: action_save(a); break;
        case GDK_KEY_l: case GDK_KEY_L: action_load(a); break;
        case GDK_KEY_n: case GDK_KEY_N: action_new(a); break;
        case GDK_KEY_q: case GDK_KEY_Q: gtk_window_close(GTK_WINDOW(a->win)); return TRUE;
        default: break;
    }
    gtk_widget_queue_draw(a->da);
    return TRUE;
}

/* ===================== UI Setup ===================== */

static GtkWidget* make_toolbar(App *a) {
    GtkWidget *box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
    GtkWidget *btn_new = gtk_button_new_with_label("Nouveau");
    GtkWidget *btn_hint = gtk_button_new_with_label("Indice");
    GtkWidget *btn_reset = gtk_button_new_with_label("Reset");
    GtkWidget *btn_check = gtk_button_new_with_label("Verifier");
    GtkWidget *btn_save = gtk_button_new_with_label("Sauver");
    GtkWidget *btn_load = gtk_button_new_with_label("Charger");
    GtkWidget *btn_quit = gtk_button_new_with_label("Quitter");

    a->scale = gtk_scale_new_with_range(GTK_ORIENTATION_HORIZONTAL, 1, 10, 1);
    gtk_widget_set_hexpand(a->scale, TRUE);
    gtk_scale_set_value_pos(GTK_SCALE(a->scale), GTK_POS_RIGHT);
    gtk_range_set_value(GTK_RANGE(a->scale), 7);

    gtk_box_pack_start(GTK_BOX(box), btn_new, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(box), btn_hint, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(box), btn_reset, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(box), btn_check, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(box), btn_save, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(box), btn_load, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(box), a->scale, TRUE, TRUE, 6);
    gtk_box_pack_end(GTK_BOX(box), btn_quit, FALSE, FALSE, 0);

    g_signal_connect_swapped(btn_new, "clicked", G_CALLBACK(action_new), a);
    g_signal_connect_swapped(btn_hint, "clicked", G_CALLBACK(action_hint), a);
    g_signal_connect_swapped(btn_reset, "clicked", G_CALLBACK(action_reset), a);
    g_signal_connect_swapped(btn_check, "clicked", G_CALLBACK(action_check), a);
    g_signal_connect_swapped(btn_save, "clicked", G_CALLBACK(action_save), a);
    g_signal_connect_swapped(btn_load, "clicked", G_CALLBACK(action_load), a);
    g_signal_connect_swapped(btn_quit, "clicked", G_CALLBACK(gtk_window_close), a->win);

    return box;
}

/* ===================== Main ===================== */

int main(int argc, char **argv) {
    srand((unsigned)time(NULL));
    gtk_init(&argc, &argv);

    App a = {0};
    a.sel_r = a.sel_c = 0;
    a.errors = 0;
    a.game_over = 0;

    a.win = gtk_window_new(GTK_WINDOW_TOPLEVEL);
    gtk_window_set_title(GTK_WINDOW(a.win), "Sudoku GTK");
    gtk_window_set_default_size(GTK_WINDOW(a.win), 640, 740);
    g_signal_connect(a.win, "destroy", G_CALLBACK(gtk_main_quit), NULL);
    g_signal_connect(a.win, "key-press-event", G_CALLBACK(on_key), &a);

    GtkWidget *vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 8);
    gtk_container_add(GTK_CONTAINER(a.win), vbox);

    GtkWidget *toolbar = make_toolbar(&a);
    gtk_box_pack_start(GTK_BOX(vbox), toolbar, FALSE, FALSE, 6);

    a.da = gtk_drawing_area_new();
    int W = PAD*2 + CELL*9 + 8, H = PAD*2 + CELL*9 + 8;
    gtk_widget_set_size_request(a.da, W, H);
    gtk_box_pack_start(GTK_BOX(vbox), a.da, FALSE, FALSE, 0);
    gtk_widget_add_events(a.da, GDK_BUTTON_PRESS_MASK);
    g_signal_connect(a.da, "draw", G_CALLBACK(on_draw), &a);
    g_signal_connect(a.da, "button-press-event", G_CALLBACK(on_button_press), &a);

    a.status = gtk_label_new(NULL);
    gtk_label_set_use_markup(GTK_LABEL(a.status), TRUE);
    gtk_label_set_markup(GTK_LABEL(a.status),
        "<span size='x-large'>Bienvenue. Choisissez une difficulte puis \"Nouveau\".</span>");
    gtk_box_pack_start(GTK_BOX(vbox), a.status, FALSE, FALSE, 6);

    if (saveExists(SAVE_FILE) && loadGame(SAVE_FILE, a.board, a.fixed, a.original, a.solution)) {
        set_status(&a, "Partie chargee (reprendre avec S/L/N au besoin).");
    } else {
        if (!generateNewPuzzle(a.board, a.fixed, a.original, a.solution, 7)) {
            g_printerr("Failed to generate initial puzzle\n");
            return 1;
        }
        saveGame(SAVE_FILE, a.board, a.fixed, a.original, a.solution);
        int clues = mapComplexityToClues(7);
        char buf[64];
        snprintf(buf, sizeof buf, "Grille initiale (~%d indices).", clues);
        set_status(&a, buf);
    }

    gtk_widget_show_all(a.win);
    gtk_main();
    return 0;
}