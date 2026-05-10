/*
 * 01_snake/step3_linkedlist/main.c — 第 3 章 (Snake v2 / 連結リスト + malloc)
 *
 * v1 (配列) からの差分:
 *   - body は struct Node の連結リスト (head ←- tail)
 *   - 食料を 1 個出して食べさせ、リスト先頭に Node を 1 つ malloc して伸ばす
 *   - tail は通常 free して捨てる (動かない tick では tail を保持)
 *
 * 学習材料:
 *   - malloc / free を実コードで使う
 *   - 二重 free を「わざと起こす」モード (--bug=double-free) を仕込む
 *     → 第 11 章で valgrind / gdb 実戦で見るため、本章は valgrind の最初の出会い
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

typedef enum { DIR_UP, DIR_DOWN, DIR_LEFT, DIR_RIGHT } Direction;

typedef struct Node {
    uint8_t       r, c;
    struct Node  *next;   /* head → ... → tail (next == NULL) */
} Node;

typedef struct {
    Node     *head;
    Node     *tail;
    int       len;
    Direction dir;
    int       alive;
    uint64_t  score;
    uint8_t   food_r, food_c;
} Snake;

static char map[ROWS][COLS];
static int  g_bug_double_free = 0;

static void map_clear(void) {
    memset(map, ' ', sizeof(map));
    for (int c = 0; c < COLS; c++) { map[0][c] = '#'; map[ROWS - 1][c] = '#'; }
    for (int r = 0; r < ROWS; r++) { map[r][0] = '#'; map[r][COLS - 1] = '#'; }
}

static Node *node_new(uint8_t r, uint8_t c) {
    Node *n = malloc(sizeof(*n));
    if (!n) { perror("malloc"); exit(EXIT_FAILURE); }
    n->r = r; n->c = c; n->next = NULL;
    return n;
}

static void snake_free(Snake *s) {
    Node *p = s->head;
    while (p) {
        Node *next = p->next;
        free(p);
        p = next;
    }
    s->head = s->tail = NULL;
    s->len  = 0;
}

static void place_food(Snake *s) {
    /* シンプルに繰り返しランダム。蛇が画面を埋めると無限ループするので注意 (演習)。 */
    for (;;) {
        int r = 1 + rand() % (ROWS - 2);
        int c = 1 + rand() % (COLS - 2);
        int hit = 0;
        for (Node *p = s->head; p; p = p->next) {
            if (p->r == r && p->c == c) { hit = 1; break; }
        }
        if (!hit) { s->food_r = (uint8_t)r; s->food_c = (uint8_t)c; return; }
    }
}

static void snake_init(Snake *s) {
    s->head = s->tail = NULL;
    s->len = 0; s->dir = DIR_RIGHT; s->alive = 1; s->score = 0;
    int r = ROWS / 2, c = COLS / 2;
    /* head から tail に向かって 5 個 push */
    for (int i = 0; i < 5; i++) {
        Node *n = node_new((uint8_t)r, (uint8_t)(c - i));
        if (!s->head) s->head = n;
        else          s->tail->next = n;
        s->tail = n;
        s->len++;
    }
    place_food(s);
}

static void next_head_pos(const Snake *s, uint8_t *out_r, uint8_t *out_c) {
    uint8_t r = s->head->r, c = s->head->c;
    switch (s->dir) {
        case DIR_UP:    if (r > 0)         r--; break;
        case DIR_DOWN:  if (r < ROWS - 1)  r++; break;
        case DIR_LEFT:  if (c > 0)         c--; break;
        case DIR_RIGHT: if (c < COLS - 1)  c++; break;
    }
    *out_r = r; *out_c = c;
}

static int will_collide(const Snake *s, uint8_t r, uint8_t c) {
    if (map[r][c] == '#') return 1;
    /* tail は今回 tick で剥がれるので除外 */
    for (Node *p = s->head; p && p != s->tail; p = p->next) {
        if (p->r == r && p->c == c) return 1;
    }
    return 0;
}

static void snake_step(Snake *s) {
    uint8_t nr, nc;
    next_head_pos(s, &nr, &nc);
    int eat = (nr == s->food_r && nc == s->food_c);
    if (will_collide(s, nr, nc)) { s->alive = 0; return; }

    /* 新しい head ノードを malloc して付ける */
    Node *new_head = node_new(nr, nc);
    new_head->next = s->head;
    s->head        = new_head;

    if (eat) {
        s->score += 10;
        s->len++;
        place_food(s);
        /* tail を残す (蛇が伸びる) */
    } else {
        /* tail を 1 個 free して縮める。
           --bug=double-free 指定時は 2 回 free する → valgrind 出番。 */
        Node *old_tail = s->tail;
        Node *prev     = s->head;
        while (prev && prev->next != old_tail) prev = prev->next;
        if (prev) prev->next = NULL;
        s->tail = prev;
        free(old_tail);
        if (g_bug_double_free) {
            free(old_tail); /* ← 意図的な二重 free。valgrind が叫ぶ。 */
        }
    }
}

static void render(const Snake *s) {
    tty_clear_screen();
    for (int r = 0; r < ROWS; r++) {
        tty_move_cursor(r + 1, 1);
        for (int c = 0; c < COLS; c++) (void)write(STDOUT_FILENO, &map[r][c], 1);
    }
    /* 食料 */
    tty_move_cursor(s->food_r + 1, s->food_c + 1);
    (void)write(STDOUT_FILENO, "*", 1);
    /* 蛇 */
    int i = 0;
    for (Node *p = s->head; p; p = p->next, i++) {
        tty_move_cursor(p->r + 1, p->c + 1);
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
    if (tty_read_nonblock(&c) != 1) return 0;
    if (c == 'q') return 1;
    if (c != '\x1b') return 0;
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
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--bug=double-free") == 0) g_bug_double_free = 1;
    }
    srand((unsigned)time(NULL));

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
    render(&s);
    tty_move_cursor(ROWS + 2, 1);
    const char msg[] = "GAME OVER. press any key to quit.\r\n";
    (void)write(STDOUT_FILENO, msg, sizeof(msg) - 1);
    char dummy;
    while (tty_read_nonblock(&dummy) == 0) msleep(20);

    snake_free(&s);  /* 終了前に必ず free。これを抜くと valgrind が leak を報告。 */
    return 0;
}
