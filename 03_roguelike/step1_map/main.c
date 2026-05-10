/*
 * 03_roguelike/step1_map/main.c — 第 7 章 (Roguelike v1 / 動的ダンジョン生成)
 *
 * 学習材料:
 *   - 2 次元動的確保: malloc(N*M) を 1 本 vs malloc(N) して各行を malloc
 *     → 局所性とコードの読みやすさのトレードオフ
 *   - flexible array member: struct Map { int rows, cols; char tiles[]; };
 *   - BSP 分割を簡素化した「ランダム部屋 + 廊下」生成
 */
#include "tty.h"

#include <stdint.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

/* flexible array member: 構造体末尾に「サイズ未定の配列」を置ける C99 機能。
   sizeof(Map) は tiles を含まない。 malloc(sizeof(Map) + rows*cols) で 1 個の
   メモリブロックに収まるのが便利。 */
typedef struct {
    int  rows;
    int  cols;
    char tiles[];      /* '.' = 床, '#' = 壁, '+' = ドア */
} Map;

#define TILE(m, r, c) ((m)->tiles[(r) * (m)->cols + (c)])

typedef struct { int r, c; } Pt;

static Map *map_new(int rows, int cols) {
    Map *m = malloc(sizeof(*m) + (size_t)rows * (size_t)cols);
    if (!m) { perror("malloc"); exit(1); }
    m->rows = rows; m->cols = cols;
    memset(m->tiles, '#', (size_t)rows * (size_t)cols);
    return m;
}
static void map_free(Map *m) { free(m); }

/* 単純な部屋を 1 個切る */
static void carve_room(Map *m, int r0, int c0, int h, int w) {
    for (int r = r0; r < r0 + h && r < m->rows - 1; r++)
        for (int c = c0; c < c0 + w && c < m->cols - 1; c++)
            if (r > 0 && c > 0) TILE(m, r, c) = '.';
}
/* 2 点の間に L 字の廊下を掘る */
static void carve_corridor(Map *m, Pt a, Pt b) {
    int r = a.r, c = a.c;
    while (c != b.c) { TILE(m, r, c) = '.'; c += (b.c > c) ? 1 : -1; }
    while (r != b.r) { TILE(m, r, c) = '.'; r += (b.r > r) ? 1 : -1; }
    TILE(m, r, c) = '.';
}

/* BSP 風: 簡素化のため、 N 個のランダム部屋を作って中心同士を順に廊下でつなぐ */
static Pt generate(Map *m, int n_rooms) {
    Pt centers[32];
    if (n_rooms > 32) n_rooms = 32;
    for (int i = 0; i < n_rooms; i++) {
        int h  = 3 + rand() % 5;
        int w  = 4 + rand() % 8;
        int r0 = 1 + rand() % (m->rows - h - 2);
        int c0 = 1 + rand() % (m->cols - w - 2);
        carve_room(m, r0, c0, h, w);
        centers[i].r = r0 + h / 2;
        centers[i].c = c0 + w / 2;
        if (i > 0) carve_corridor(m, centers[i - 1], centers[i]);
    }
    return centers[0]; /* プレイヤ初期位置 */
}

static void render(const Map *m, Pt p) {
    tty_clear_screen();
    for (int r = 0; r < m->rows; r++) {
        tty_move_cursor(r + 1, 1);
        (void)write(STDOUT_FILENO, &TILE(m, r, 0), (size_t)m->cols);
    }
    tty_move_cursor(p.r + 1, p.c + 1);
    (void)write(STDOUT_FILENO, "@", 1);
    char hud[80];
    int  n = snprintf(hud, sizeof(hud),
                      "\x1b[%d;1Hhjkl=move  q=quit  pos=(%d,%d)",
                      m->rows + 1, p.r, p.c);
    if (n > 0) (void)write(STDOUT_FILENO, hud, (size_t)n);
}

static void msleep(int ms) {
    struct timespec ts = { ms / 1000, (ms % 1000) * 1000000L };
    nanosleep(&ts, NULL);
}

int main(int argc, char **argv) {
    int rows = 24, cols = 80;
    if (argc == 3) {
        rows = atoi(argv[1]);
        cols = atoi(argv[2]);
        if (rows < 8 || cols < 16) { fprintf(stderr, "too small\n"); return 1; }
    }
    srand((unsigned)time(NULL));
    Map *m = map_new(rows, cols);
    Pt   p = generate(m, 6);
    /* `Map` が flexible array で割り付けられているサイズを表示。 学習用 */
    fprintf(stderr, "Map header sizeof = %zu, tiles size = %d, total alloc = %zu\n",
            sizeof(Map), rows * cols, sizeof(Map) + (size_t)rows * (size_t)cols);

    tty_raw_mode(); tty_hide_cursor();
    render(m, p);

    for (;;) {
        char c;
        if (tty_read_nonblock(&c) != 1) { msleep(20); continue; }
        if (c == 'q') break;
        int dr = 0, dc = 0;
        switch (c) {
            case 'h': dc = -1; break;
            case 'l': dc =  1; break;
            case 'k': dr = -1; break;
            case 'j': dr =  1; break;
            default: continue;
        }
        int nr = p.r + dr, nc = p.c + dc;
        if (nr >= 0 && nr < m->rows && nc >= 0 && nc < m->cols
                && TILE(m, nr, nc) == '.') {
            p.r = nr; p.c = nc;
        }
        render(m, p);
    }

    map_free(m);
    return 0;
}
