/*
 * 03_roguelike/step2_signal/main.c — 第 8 章 (SIGWINCH と async-signal-safe)
 *
 * 学習材料:
 *   - sigaction(SIGWINCH, ...) でターミナルリサイズを受信
 *   - signal handler で **flag を立てるだけ** (volatile sig_atomic_t)
 *   - メインループが flag を見て再描画する設計 = "self-pipe" 風の作法
 *   - signal handler 内では printf/malloc を呼ばない (async-signal-safe しか触らない)
 */
#include "tty.h"

#include <signal.h>
#include <stdint.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <time.h>
#include <unistd.h>

typedef struct {
    int  rows, cols;
    char tiles[];
} Map;
#define TILE(m, r, c) ((m)->tiles[(r) * (m)->cols + (c)])

typedef struct { int r, c; } Pt;

/* signal handler とメインスレッドの両方から触れる変数の型は厳密に決まっている:
     `volatile sig_atomic_t` のみが規格保証。

   volatile:
     コンパイラに「この変数の値は外から (= signal/別スレッド/MMIO) 変わる可能性がある」
     と伝える。 これを付けないと「ループ中で値が変わらない」 と勝手に判断され、
     レジスタにキャッシュされて flag を見落とすことがある。

   sig_atomic_t:
     「途中で signal が割り込んでも一貫性が壊れない、 アトミックに read/write
      できる整数型」 として処理系が定義する型 (通常は int)。 */
static volatile sig_atomic_t g_resize_flag = 0;

/* SIGWINCH ハンドラ:
   端末ウィンドウサイズが変わった時にカーネルが投げてくる signal。
   ハンドラの中で複雑な事はしない (= async-signal-safe な関数しか呼べない)。
   flag を立てるだけにして、 メインループで安全に処理する。 */
static void on_winch(int sig) {
    (void)sig;            /* 引数不使用警告抑制の定番 */
    g_resize_flag = 1;    /* これしかしない。 read/write/malloc/printf も呼ばない */
}

static Map *map_new(int rows, int cols) {
    Map *m = malloc(sizeof(*m) + (size_t)rows * (size_t)cols);
    if (!m) { perror("malloc"); exit(1); }
    m->rows = rows; m->cols = cols;
    memset(m->tiles, '#', (size_t)rows * (size_t)cols);
    return m;
}

static void carve_room(Map *m, int r0, int c0, int h, int w) {
    for (int r = r0; r < r0 + h && r < m->rows - 1; r++)
        for (int c = c0; c < c0 + w && c < m->cols - 1; c++)
            if (r > 0 && c > 0) TILE(m, r, c) = '.';
}
static void carve_corridor(Map *m, Pt a, Pt b) {
    int r = a.r, c = a.c;
    while (c != b.c) { TILE(m, r, c) = '.'; c += (b.c > c) ? 1 : -1; }
    while (r != b.r) { TILE(m, r, c) = '.'; r += (b.r > r) ? 1 : -1; }
    TILE(m, r, c) = '.';
}
static Pt generate(Map *m, int n_rooms) {
    Pt centers[32];
    if (n_rooms > 32) n_rooms = 32;
    for (int i = 0; i < n_rooms; i++) {
        int h  = 3 + rand() % 5;
        int w  = 4 + rand() % 8;
        if (m->rows - h - 2 <= 0 || m->cols - w - 2 <= 0) continue;
        int r0 = 1 + rand() % (m->rows - h - 2);
        int c0 = 1 + rand() % (m->cols - w - 2);
        carve_room(m, r0, c0, h, w);
        centers[i].r = r0 + h / 2; centers[i].c = c0 + w / 2;
        if (i > 0) carve_corridor(m, centers[i - 1], centers[i]);
    }
    return centers[0];
}

static void render(const Map *m, Pt p) {
    tty_clear_screen();
    for (int r = 0; r < m->rows; r++) {
        tty_move_cursor(r + 1, 1);
        (void)write(STDOUT_FILENO, &TILE(m, r, 0), (size_t)m->cols);
    }
    tty_move_cursor(p.r + 1, p.c + 1);
    (void)write(STDOUT_FILENO, "@", 1);
    int rows, cols;
    tty_size(&rows, &cols);
    char hud[120];
    int  n = snprintf(hud, sizeof(hud),
                      "\x1b[%d;1Hhjkl=move q=quit  map=%dx%d  term=%dx%d",
                      m->rows + 1, m->rows, m->cols, rows, cols);
    if (n > 0) (void)write(STDOUT_FILENO, hud, (size_t)n);
}

static void msleep(int ms) {
    struct timespec ts = { ms / 1000, (ms % 1000) * 1000000L };
    nanosleep(&ts, NULL);
}

int main(void) {
    /* 起動時の端末サイズに合わせてマップを作る */
    int rows, cols;
    tty_size(&rows, &cols);
    if (rows < 8) rows = 24;
    if (cols < 16) cols = 80;
    /* HUD 行のため 1 行余す */
    Map *m = map_new(rows - 1, cols);
    srand((unsigned)time(NULL));
    Pt p = generate(m, 6);

    /* SIGWINCH を捕まえる。 SA_RESTART は付けない:
       signal で read を中断させて即時にリサイズを反映するため。 */
    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = on_winch;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;
    sigaction(SIGWINCH, &sa, NULL);

    tty_raw_mode(); tty_hide_cursor();
    render(m, p);

    for (;;) {
        if (g_resize_flag) {
            g_resize_flag = 0;
            int nr, nc;
            tty_size(&nr, &nc);
            if (nr < 8) nr = 24;
            if (nc < 16) nc = 80;
            free(m);
            m = map_new(nr - 1, nc);
            p = generate(m, 6);
            render(m, p);
        }
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

    free(m);
    return 0;
}
