#ifndef SUDOKU_H
#define SUDOKU_H

#include <gtk/gtk.h>
#include <gdk/gdkkeysyms.h>
#include <pango/pangocairo.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <time.h>

#define N 9
#define SAVE_FILE "sudoku_save.bin"
#define SAVE_MAGIC 0x53554431u
#define SAVE_VERSION 1

/* UI sizing */
#define CELL 54
#define PAD 18

/* DLX Algorithm X constants */
#define DLX_COLS 324
#define DLX_ROWS 729
#define DLX_NODES (DLX_ROWS * 4 + DLX_COLS + 5)

typedef struct {
    int board[N][N];
    int fixed[N][N];
    int original[N][N];
    int solution[N][N];
    int sel_r, sel_c;
    GtkWidget *win, *da, *status, *scale;
    int errors;
    int game_over;
} App;

typedef struct {
    uint32_t magic;
    int32_t version;
    int32_t board[N][N];
    int32_t fixed[N][N];
    int32_t original[N][N];
    int32_t solution[N][N];
} SaveData;

/* Core functions */
void copyBoard(int dst[N][N], const int src[N][N]);
int isSafe(const int board[N][N], int r, int c, int val);
int boardComplete(const int board[N][N]);
int isSolvedByRules(const int b[N][N]);
int generateNewPuzzle(int board[N][N], int fixed[N][N], int original[N][N], int solution[N][N], int level);

/* Save/Load functions */
int saveExists(const char *path);
int saveGame(const char *path, const int board[N][N], const int fixed[N][N],
             const int original[N][N], const int solution[N][N]);
int loadGame(const char *path, int board[N][N], int fixed[N][N],
             int original[N][N], int solution[N][N]);
int deleteSave(const char *path);

#endif