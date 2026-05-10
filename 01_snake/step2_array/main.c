/*
 * 01_snake/step2_array/main.c — 第 2 章 (Snake v1 / 配列で体を表現)
 *
 * 第 1 章で乗っ取ったキー入力を、第 1 章のロジック (lib/tty) ごと使い回す。
 * 本章のテーマは「配列で蛇の体を持つ」「struct のサイズと padding を覗く」。
 *
 * 設計:
 *   - マップは char map[ROWS][COLS] の **スタック上** 2D 配列
 *   - 体は Cell body[MAX_LEN] の固定長配列 + len で現在の長さを保持
 *   - tick で 頭を進め、配列を後ろから前にずらす (素朴 O(N))
 *
 * 第 3 章では同じゲームを連結リスト + heap で書き直し、
 * 配列 vs リストのトレードオフ (アクセス局所性 vs 挿入削除コスト) を体感する。
 */
#include "tty.h"

#include <stdint.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#define ROWS    20
#define COLS    60
#define MAX_LEN 256

typedef enum { DIR_UP, DIR_DOWN, DIR_LEFT, DIR_RIGHT } Direction;

typedef struct {
    uint8_t r;  /* 行 (0-origin、画面では +1 する) */
    uint8_t c;  /* 列 */
} Cell;

/* メタ情報を意図的に「型が混ざった」構造体にしておく。
   §inspect で sizeof と offsetof を覗いた時に padding が見える。 */
typedef struct {
    Cell      body[MAX_LEN];  /* body[0] = 頭 */
    int       len;
    Direction dir;
    uint8_t   alive;
    uint64_t  score;          /* わざと 8byte align を要求して padding を発生させる */
} Snake;

/* マップは静的に確保 (BSS / data セグメント) */
static char map[ROWS][COLS];

static void map_clear(void) {
    memset(map, ' ', sizeof(map));
    /* 外周を壁にする */
    for (int c = 0; c < COLS; c++) { map[0][c] = '#'; map[ROWS - 1][c] = '#'; }
    for (int r = 0; r < ROWS; r++) { map[r][0] = '#'; map[r][COLS - 1] = '#'; }
}

static void snake_init(Snake *s) {
    s->len   = 5;
    s->dir   = DIR_RIGHT;
    s->alive = 1;
    s->score = 0;
    /* 中央付近に水平に置く */
    int r = ROWS / 2, c = COLS / 2;
    for (int i = 0; i < s->len; i++) {
        s->body[i].r = (uint8_t)r;
        s->body[i].c = (uint8_t)(c - i);
    }
}

static int snake_will_collide(const Snake *s, Cell next) {
    if (map[next.r][next.c] == '#') return 1;
    /* 自分の体チェック (頭が動く先に体があるか)。tail (= body[len-1]) は
       直後にずれて空くので除外する。*/
    for (int i = 0; i < s->len - 1; i++) {
        if (s->body[i].r == next.r && s->body[i].c == next.c) return 1;
    }
    return 0;
}

static Cell next_head(const Snake *s) {
    Cell h = s->body[0];
    switch (s->dir) {
        case DIR_UP:    if (h.r > 0)         h.r--; break;
        case DIR_DOWN:  if (h.r < ROWS - 1)  h.r++; break;
        case DIR_LEFT:  if (h.c > 0)         h.c--; break;
        case DIR_RIGHT: if (h.c < COLS - 1)  h.c++; break;
    }
    return h;
}

static void snake_step(Snake *s) {
    Cell h = next_head(s);
    if (snake_will_collide(s, h)) { s->alive = 0; return; }
    /* 後ろから前へずらす: body[len-1] ← body[len-2] ← … ← body[0] ← new_head */
    for (int i = s->len - 1; i > 0; i--) s->body[i] = s->body[i - 1];
    s->body[0] = h;
}

static void render(const Snake *s) {
    tty_clear_screen();
    for (int r = 0; r < ROWS; r++) {
        tty_move_cursor(r + 1, 1);
        for (int c = 0; c < COLS; c++) (void)write(STDOUT_FILENO, &map[r][c], 1);
    }
    for (int i = 0; i < s->len; i++) {
        tty_move_cursor(s->body[i].r + 1, s->body[i].c + 1);
        char ch = (i == 0) ? '@' : 'o';
        (void)write(STDOUT_FILENO, &ch, 1);
    }
    char hud[80];
    int  n = snprintf(hud, sizeof(hud),
                      "\x1b[%d;1HSCORE %llu  LEN %d   q:quit",
                      ROWS + 1, (unsigned long long)s->score, s->len);
    if (n > 0) (void)write(STDOUT_FILENO, hud, (size_t)n);
}

static int handle_input(Snake *s) {
    char c;
    int  r = tty_read_nonblock(&c);
    if (r != 1) return 0;
    if (c == 'q') return 1;
    if (c != '\x1b') return 0;
    /* 矢印キーは ESC [ A/B/C/D。VTIME=0 のためすぐ来ていなければ取りこぼす想定。 */
    char seq[2];
    if (tty_read_nonblock(&seq[0]) != 1) return 0;
    if (tty_read_nonblock(&seq[1]) != 1) return 0;
    if (seq[0] != '[') return 0;
    switch (seq[1]) {
        case 'A': if (s->dir != DIR_DOWN)  s->dir = DIR_UP;    break;
        case 'B': if (s->dir != DIR_UP)    s->dir = DIR_DOWN;  break;
        case 'C': if (s->dir != DIR_LEFT)  s->dir = DIR_RIGHT; break;
        case 'D': if (s->dir != DIR_RIGHT) s->dir = DIR_LEFT;  break;
    }
    return 0;
}

static void msleep(int ms) {
    struct timespec ts = { ms / 1000, (ms % 1000) * 1000000L };
    nanosleep(&ts, NULL);
}

int main(int argc, char **argv) {
    /* --inspect オプション: ゲームを起動せず、構造体サイズを表示して終了する。
       第 2 章 §inspect でこの出力を読む。 */
    if (argc == 2 && strcmp(argv[1], "--inspect") == 0) {
        printf("sizeof(Cell)        = %zu\n", sizeof(Cell));
        printf("sizeof(Snake)       = %zu\n", sizeof(Snake));
        printf("offsetof(body)      = %zu\n", offsetof(Snake, body));
        printf("offsetof(len)       = %zu\n", offsetof(Snake, len));
        printf("offsetof(dir)       = %zu\n", offsetof(Snake, dir));
        printf("offsetof(alive)     = %zu\n", offsetof(Snake, alive));
        printf("offsetof(score)     = %zu\n", offsetof(Snake, score));
        printf("MAX_LEN * sizeof(Cell) = %zu (= body フィールドの素のサイズ)\n",
               MAX_LEN * sizeof(Cell));
        Snake s_on_stack;
        printf("&s_on_stack         = %p (stack)\n", (void *)&s_on_stack);
        printf("&map                = %p (BSS/data)\n", (void *)map);
        return 0;
    }

    tty_raw_mode();
    tty_hide_cursor();
    map_clear();

    Snake s;
    snake_init(&s);

    while (s.alive) {
        if (handle_input(&s)) break;
        snake_step(&s);
        render(&s);
        msleep(120);
    }
    /* render を最後にもう一度 (ゲームオーバ表示) */
    render(&s);
    tty_move_cursor(ROWS + 2, 1);
    const char msg[] = "GAME OVER. press any key to quit.\r\n";
    (void)write(STDOUT_FILENO, msg, sizeof(msg) - 1);
    /* キー入力を 1 回待つ */
    char dummy;
    while (tty_read_nonblock(&dummy) == 0) msleep(20);
    return 0;
}
