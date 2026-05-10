/*
 * 02_tetris/step2_bitwise/main.c — 第 5 章 (Tetris v2 / bit パターン + 関数ポインタ)
 *
 * v1 (heap) からの差分:
 *   - Shape を uint16_t 1 個で表現する (4x4 grid = 16 bits)
 *   - 回転 4 状態は uint16_t 配列でテーブル化
 *   - 「次の状態」を返すディスパッチを **関数ポインタ配列** で書く
 *     (本章の主目的は bit と fn ptr。性能比較は第 12 章。)
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

/* 4x4 grid を行優先で 16-bit に詰める。
   bit 順: (i,j) → bit (i*4 + j)。 最上位 bit は使わない。
   例: I 横 → row 1 が全部 1 → bits 4-7 が 1 → 0b0000_0000_1111_0000 = 0x00F0

   C のビット表記:
     0xF0   = 16進リテラル (= 11110000 = 240)
     0b1010 = 2進リテラル (C23 標準。 gcc/clang は古くからの拡張で受け入れる)
     `<<`, `>>` = 左/右シフト。 `(1 << n)` で「n bit 目だけが立った値」 を作れる
     `&`        = ビット AND。 (x & mask) でマスクされたビットだけ残る
     `|`        = ビット OR。 (x | (1<<n)) で n bit 目を立てる
     `~`        = ビット NOT。 全 bit 反転
     `^`        = ビット XOR */
static const uint16_t PATTERNS[7][4] = {
    /* I */ { 0x00F0, 0x4444, 0x0F00, 0x2222 },
    /* O */ { 0x6600, 0x6600, 0x6600, 0x6600 },
    /* T */ { 0x4E00, 0x4640, 0x0E40, 0x4C40 },
    /* S */ { 0x6C00, 0x4620, 0x6C00, 0x4620 },
    /* Z */ { 0xC600, 0x2640, 0xC600, 0x2640 },
    /* J */ { 0x8E00, 0x6440, 0x0E20, 0x44C0 },
    /* L */ { 0x2E00, 0x4460, 0x0E80, 0xC440 },
};

/* BIT_AT(p, i, j):
     16-bit パターン p の (i, j) 位置の bit を取り出す関数マクロ。
     (i*4 + j) bit だけ右シフトしてから & 1u で最下位 bit だけ残す。
     カッコ () を全引数に付けるのは演算子優先順位事故を防ぐマクロの定石。
     1u は unsigned int リテラル (型推論を unsigned に揃えるため)。 */
#define BIT_AT(p, i, j) (((p) >> ((i)*4 + (j))) & 1u)

typedef struct PieceNode {
    int                kind;
    struct PieceNode  *next;
} PieceNode;

typedef struct {
    PieceNode *head, *tail;
    int        size;
} Queue;

static int      g_board[ROWS][COLS];
static int      g_cur_kind, g_cur_rot, g_cur_r, g_cur_c;
static uint64_t g_score = 0;
static int      g_alive = 1;

/* ----- Queue ----- */
static void q_push(Queue *q, int kind) {
    PieceNode *n = malloc(sizeof(*n));
    if (!n) { perror("malloc"); exit(1); }
    n->kind = kind; n->next = NULL;
    if (q->tail) q->tail->next = n;
    else         q->head       = n;
    q->tail = n;
    q->size++;
}
static int q_pop(Queue *q) {
    PieceNode *n = q->head;
    if (!n) return -1;
    int k = n->kind;
    q->head = n->next;
    if (!q->head) q->tail = NULL;
    free(n);
    q->size--;
    return k;
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

/* ----- 形状ヘルパ ----- */
static uint16_t shape_at(int kind, int rot) { return PATTERNS[kind][rot & 3]; }

/* ----- 関数ポインタディスパッチ -----
   「現在の回転状態 → 次の回転状態」 を 4 通り用意し、
   テーブルから引いて呼び出す。 switch 文の代替として教材に。

   関数ポインタ型の宣言:
     int (*RotateFn)(int)
       ↑    ↑          ↑
       戻値型 ポインタ名  引数型 (列挙)
     これは「int を 1 つ取って int を返す関数へのポインタ」 という型。
     typedef を付けると型名 (RotateFn) として再利用できる。
     呼び出すときは ROT_FN[idx](cur) のように関数として書ける (普通の関数と同じ構文)。 */
typedef int (*RotateFn)(int cur);
static int rot_cw  (int cur) { return (cur + 1) & 3; }
static int rot_ccw (int cur) { return (cur + 3) & 3; }
static int rot_180 (int cur) { return (cur + 2) & 3; }
static int rot_id  (int cur) { return cur; }
static const RotateFn ROT_FN[4] = { rot_id, rot_cw, rot_180, rot_ccw };
/*  ROT_FN[0] = identity (回転しない、操作キャンセル時の noop に使う)
    ROT_FN[1] = 時計回り (↑ キーで呼ぶ)
    ROT_FN[2] = 180 度  (演習で割り当て)
    ROT_FN[3] = 反時計回り (演習)                                          */

/* ----- 配置判定 ----- */
static int try_place(int kind, int rot, int r, int c) {
    uint16_t p = shape_at(kind, rot);
    for (int i = 0; i < 4; i++) for (int j = 0; j < 4; j++) {
        if (!BIT_AT(p, i, j)) continue;
        int nr = r + i, nc = c + j;
        if (nr < 0 || nr >= ROWS || nc < 0 || nc >= COLS) return 0;
        if (g_board[nr][nc])                              return 0;
    }
    return 1;
}
static void lock_piece(void) {
    uint16_t p = shape_at(g_cur_kind, g_cur_rot);
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
        } else { r--; }
    }
    return cleared;
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

    uint16_t p = shape_at(g_cur_kind, g_cur_rot);
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
                      "\x1b[1;%dHSCORE %llu  Q=%d  shape=0x%04X",
                      COLS + 5, (unsigned long long)g_score, q->size, p);
    if (n > 0) (void)write(STDOUT_FILENO, hud, (size_t)n);
}

static void try_move(int dr, int dc) {
    if (try_place(g_cur_kind, g_cur_rot, g_cur_r + dr, g_cur_c + dc)) {
        g_cur_r += dr; g_cur_c += dc;
    }
}
/* 関数ポインタテーブルを引いて回転 */
static void try_rotate(int idx) {
    int nr = ROT_FN[idx & 3](g_cur_rot);
    if (try_place(g_cur_kind, nr, g_cur_r, g_cur_c)) g_cur_rot = nr;
}

static int handle_input(Queue *q) {
    char c;
    if (tty_read_nonblock(&c) != 1) return 0;
    if (c == 'q') return 1;
    if (c == ' ') {
        while (try_place(g_cur_kind, g_cur_rot, g_cur_r + 1, g_cur_c)) g_cur_r++;
        lock_piece();
        g_score += (uint64_t)(10 * clear_lines());
        if (!spawn(q)) g_alive = 0;
        return 0;
    }
    if (c == 'z') { try_rotate(3); return 0; } /* 反時計回り */
    if (c == 'x') { try_rotate(2); return 0; } /* 180 */
    if (c != '\x1b') return 0;
    char seq[2];
    if (tty_read_nonblock(&seq[0]) != 1) return 0;
    if (tty_read_nonblock(&seq[1]) != 1) return 0;
    if (seq[0] != '[') return 0;
    switch (seq[1]) {
        case 'A': try_rotate(1); break;        /* CW */
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
    if (argc == 2 && strcmp(argv[1], "--dump-shapes") == 0) {
        const char *names = "IOTSZJL";
        for (int k = 0; k < 7; k++) {
            for (int r = 0; r < 4; r++) {
                printf("%c rot=%d  bits=0x%04X\n", names[k], r, PATTERNS[k][r]);
                for (int i = 0; i < 4; i++) {
                    for (int j = 0; j < 4; j++)
                        printf("%c", BIT_AT(PATTERNS[k][r], i, j) ? '#' : '.');
                    printf("\n");
                }
                printf("\n");
            }
        }
        return 0;
    }

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
                g_score += (uint64_t)(10 * clear_lines());
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
