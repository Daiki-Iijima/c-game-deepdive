/*
 * 02_tetris/step3_collision/main.c — 第 6 章 (Tetris v3 / 衝突 + ASan)
 *
 * v2 (bitwise) からの差分:
 *   - 引数 --bug=oob を仕込み、 try_place の境界チェックを意図的に外す
 *     → 上方向のオーバラップで配列外読み (g_board[-1][...]) が発生
 *   - スコアの計算で配列外書きをわざと発生させる小さなバグも仕込む
 *   - make asan で ASan ビルドし、 即時に bug を捕まえる
 *   - valgrind との比較は章本文で
 */
#include "tty.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#define COLS 10
#define ROWS 20

static const uint16_t PATTERNS[7][4] = {
    { 0x00F0, 0x4444, 0x0F00, 0x2222 },
    { 0x6600, 0x6600, 0x6600, 0x6600 },
    { 0x4E00, 0x4640, 0x0E40, 0x4C40 },
    { 0x6C00, 0x4620, 0x6C00, 0x4620 },
    { 0xC600, 0x2640, 0xC600, 0x2640 },
    { 0x8E00, 0x6440, 0x0E20, 0x44C0 },
    { 0x2E00, 0x4460, 0x0E80, 0xC440 },
};
#define BIT_AT(p, i, j) (((p) >> ((i)*4 + (j))) & 1u)

typedef struct PieceNode { int kind; struct PieceNode *next; } PieceNode;
typedef struct { PieceNode *head, *tail; int size; } Queue;

static int      g_board[ROWS][COLS];
static int      g_cur_kind, g_cur_rot, g_cur_r, g_cur_c;
static uint64_t g_score = 0;
static int      g_alive = 1;
static int      g_bug_oob = 0;

/* score_history は 16 個分だけ確保。 g_bug_oob 時に範囲外に書いてしまう罠を仕込む。 */
static uint64_t g_score_history[16];
static int      g_score_history_idx = 0;

static void q_push(Queue *q, int kind) {
    PieceNode *n = malloc(sizeof(*n));
    if (!n) { perror("malloc"); exit(1); }
    n->kind = kind; n->next = NULL;
    if (q->tail) q->tail->next = n; else q->head = n;
    q->tail = n; q->size++;
}
static int q_pop(Queue *q) {
    PieceNode *n = q->head; if (!n) return -1;
    int k = n->kind;
    q->head = n->next; if (!q->head) q->tail = NULL;
    free(n); q->size--; return k;
}
static void q_free(Queue *q) { while (q->head) (void)q_pop(q); }
static void refill_bag(Queue *q) {
    int bag[7] = {0,1,2,3,4,5,6};
    for (int i = 6; i > 0; i--) {
        int j = rand() % (i + 1);
        int t = bag[i]; bag[i] = bag[j]; bag[j] = t;
    }
    for (int i = 0; i < 7; i++) q_push(q, bag[i]);
}

/* 健全版: 範囲を最初に必ずチェック */
static int try_place_safe(int kind, int rot, int r, int c) {
    uint16_t p = PATTERNS[kind][rot & 3];
    for (int i = 0; i < 4; i++) for (int j = 0; j < 4; j++) {
        if (!BIT_AT(p, i, j)) continue;
        int nr = r + i, nc = c + j;
        if (nr < 0 || nr >= ROWS || nc < 0 || nc >= COLS) return 0;
        if (g_board[nr][nc])                              return 0;
    }
    return 1;
}
/* バグ版: 範囲チェックの順序を変え、 g_board[nr][nc] を先に読む。
   nr < 0 のとき g_board[-1][nc] = stack 上の隣接領域を読み出す。
   ASan/valgrind が即座に検出する。 */
static int try_place_buggy(int kind, int rot, int r, int c) {
    uint16_t p = PATTERNS[kind][rot & 3];
    for (int i = 0; i < 4; i++) for (int j = 0; j < 4; j++) {
        if (!BIT_AT(p, i, j)) continue;
        int nr = r + i, nc = c + j;
        if (g_board[nr][nc]) return 0;             /* ← ここで OOB を踏む */
        if (nr < 0 || nr >= ROWS || nc < 0 || nc >= COLS) return 0;
    }
    return 1;
}
static int try_place(int kind, int rot, int r, int c) {
    return g_bug_oob ? try_place_buggy(kind, rot, r, c)
                     : try_place_safe (kind, rot, r, c);
}
static void lock_piece(void) {
    uint16_t p = PATTERNS[g_cur_kind][g_cur_rot];
    for (int i = 0; i < 4; i++) for (int j = 0; j < 4; j++)
        if (BIT_AT(p, i, j)) g_board[g_cur_r + i][g_cur_c + j] = 1;
}
static int clear_lines(void) {
    int cleared = 0;
    for (int r = ROWS - 1; r >= 0; ) {
        int full = 1;
        for (int c = 0; c < COLS; c++) if (!g_board[r][c]) { full = 0; break; }
        if (full) {
            for (int rr = r; rr > 0; rr--)
                memcpy(g_board[rr], g_board[rr - 1], sizeof(g_board[rr]));
            memset(g_board[0], 0, sizeof(g_board[0]));
            cleared++;
        } else r--;
    }
    return cleared;
}
static void note_score(uint64_t s) {
    /* バグ: idx を mod 16 せずに書く。 100 ピース置くと範囲外。
       ASan が `global-buffer-overflow` を即報告する。 */
    int idx = g_bug_oob ? g_score_history_idx++ : (g_score_history_idx++ % 16);
    g_score_history[idx] = s;
}
static int spawn(Queue *q) {
    if (q->size < 7) refill_bag(q);
    g_cur_kind = q_pop(q);
    g_cur_rot  = 0;
    g_cur_r    = 0;
    g_cur_c    = COLS / 2 - 2;
    return try_place(g_cur_kind, g_cur_rot, g_cur_r, g_cur_c);
}
static void render(const Queue *q) {
    tty_clear_screen();
    for (int r = 0; r < ROWS; r++) {
        tty_move_cursor(r + 1, 1);
        (void)write(STDOUT_FILENO, "|", 1);
        for (int c = 0; c < COLS; c++) {
            char ch = g_board[r][c] ? '#' : ' ';
            (void)write(STDOUT_FILENO, &ch, 1);
        }
        (void)write(STDOUT_FILENO, "|", 1);
    }
    tty_move_cursor(ROWS + 1, 1);
    char floor[COLS + 3];
    floor[0] = '+'; for (int i = 1; i <= COLS; i++) floor[i] = '-';
    floor[COLS + 1] = '+'; floor[COLS + 2] = '\0';
    (void)write(STDOUT_FILENO, floor, (size_t)(COLS + 2));

    uint16_t p = PATTERNS[g_cur_kind][g_cur_rot];
    for (int i = 0; i < 4; i++) for (int j = 0; j < 4; j++) {
        if (!BIT_AT(p, i, j)) continue;
        int rr = g_cur_r + i, cc = g_cur_c + j;
        if (rr >= 0 && rr < ROWS && cc >= 0 && cc < COLS) {
            tty_move_cursor(rr + 1, cc + 2);
            (void)write(STDOUT_FILENO, "@", 1);
        }
    }
    char hud[160];
    int  n = snprintf(hud, sizeof(hud),
                      "\x1b[1;%dHSCORE %llu  Q=%d  bug=%s",
                      COLS + 5, (unsigned long long)g_score, q->size,
                      g_bug_oob ? "ON" : "off");
    if (n > 0) (void)write(STDOUT_FILENO, hud, (size_t)n);
}
static void try_move(int dr, int dc) {
    if (try_place(g_cur_kind, g_cur_rot, g_cur_r + dr, g_cur_c + dc)) {
        g_cur_r += dr; g_cur_c += dc;
    }
}
static void try_rotate(void) {
    int nr = (g_cur_rot + 1) & 3;
    if (try_place(g_cur_kind, nr, g_cur_r, g_cur_c)) g_cur_rot = nr;
}
static int handle_input(Queue *q) {
    char c;
    if (tty_read_nonblock(&c) != 1) return 0;
    if (c == 'q') return 1;
    if (c == ' ') {
        while (try_place(g_cur_kind, g_cur_rot, g_cur_r + 1, g_cur_c)) g_cur_r++;
        lock_piece();
        int cleared = clear_lines();
        g_score += (uint64_t)(10 * cleared);
        note_score(g_score);
        if (!spawn(q)) g_alive = 0;
        return 0;
    }
    if (c != '\x1b') return 0;
    char seq[2];
    if (tty_read_nonblock(&seq[0]) != 1) return 0;
    if (tty_read_nonblock(&seq[1]) != 1) return 0;
    if (seq[0] != '[') return 0;
    switch (seq[1]) {
        case 'A': try_rotate(); break;
        case 'B': try_move(1, 0); break;
        case 'C': try_move(0, 1); break;
        case 'D': try_move(0, -1); break;
    }
    return 0;
}
static void msleep(int ms) {
    struct timespec ts = { ms / 1000, (ms % 1000) * 1000000L };
    nanosleep(&ts, NULL);
}
int main(int argc, char **argv) {
    for (int i = 1; i < argc; i++)
        if (strcmp(argv[i], "--bug=oob") == 0) g_bug_oob = 1;

    srand((unsigned)time(NULL));
    tty_raw_mode(); tty_hide_cursor();

    Queue queue = {0};
    refill_bag(&queue);
    if (!spawn(&queue)) g_alive = 0;

    long acc = 0;
    while (g_alive) {
        if (handle_input(&queue)) break;
        acc += 16;
        if (acc >= 500) {
            acc = 0;
            if (try_place(g_cur_kind, g_cur_rot, g_cur_r + 1, g_cur_c)) g_cur_r++;
            else {
                lock_piece();
                int cleared = clear_lines();
                g_score += (uint64_t)(10 * cleared);
                note_score(g_score);
                if (!spawn(&queue)) g_alive = 0;
            }
        }
        render(&queue);
        msleep(16);
    }

    render(&queue);
    tty_move_cursor(ROWS + 3, 1);
    const char msg[] = "GAME OVER. press any key.\r\n";
    (void)write(STDOUT_FILENO, msg, sizeof(msg) - 1);
    char dummy;
    while (tty_read_nonblock(&dummy) == 0) msleep(20);

    q_free(&queue);
    return 0;
}
