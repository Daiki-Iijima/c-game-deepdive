/*
 * 02_tetris/step1_heap/main.c
 * --------------------------------------------------------------------------
 * 第 4 章 (Tetris v1 / heap でピースキュー)
 *
 * Tetris の最小プレイアブル版。
 *   - 10x20 のプレイフィールド (関数外 int 配列 = BSS)
 *   - ピース 7 種 (I/O/T/S/Z/J/L)、 回転 4 状態、 形状は switch ベース (第 5 章で bit に置換)
 *   - 「次のピース」を保持する **キュー** を heap (連結リスト) で管理
 *   - 7-bag 方式: 7 種を Fisher-Yates でシャッフルし、 キューの末尾に push
 *
 * 学習材料:
 *   - 連結リスト + malloc/free を「使う動機があるデータ構造」 (= FIFO キュー) で再訪
 *   - valgrind --tool=massif で heap 使用量を時系列で見る
 *
 * 新登場の C 機能:
 *   - 多次元配列の集約初期化子: Shape SHAPES[7][4] = { {{{...}}}, ... };
 *   - 関数ポインタ風の関数列挙: q_push / q_pop / q_free で「Queue ADT」を作る
 *   - Fisher-Yates シャッフル: O(N) で偏りなく配列を並び替える古典アルゴリズム
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

/* 7 種 × 4 回転 を 4x4 グリッドで表現。値 1 がブロック有り。
   この章では switch / 配列リテラル で持つ。第 5 章で 16-bit ビット表現に置換する。 */
typedef struct { uint8_t cells[4][4]; } Shape;

/* I, O, T, S, Z, J, L */
static const Shape SHAPES[7][4] = {
    /* I */ {
        {{{0,0,0,0},{1,1,1,1},{0,0,0,0},{0,0,0,0}}},
        {{{0,0,1,0},{0,0,1,0},{0,0,1,0},{0,0,1,0}}},
        {{{0,0,0,0},{0,0,0,0},{1,1,1,1},{0,0,0,0}}},
        {{{0,1,0,0},{0,1,0,0},{0,1,0,0},{0,1,0,0}}},
    },
    /* O */ {
        {{{0,1,1,0},{0,1,1,0},{0,0,0,0},{0,0,0,0}}},
        {{{0,1,1,0},{0,1,1,0},{0,0,0,0},{0,0,0,0}}},
        {{{0,1,1,0},{0,1,1,0},{0,0,0,0},{0,0,0,0}}},
        {{{0,1,1,0},{0,1,1,0},{0,0,0,0},{0,0,0,0}}},
    },
    /* T */ {
        {{{0,1,0,0},{1,1,1,0},{0,0,0,0},{0,0,0,0}}},
        {{{0,1,0,0},{0,1,1,0},{0,1,0,0},{0,0,0,0}}},
        {{{0,0,0,0},{1,1,1,0},{0,1,0,0},{0,0,0,0}}},
        {{{0,1,0,0},{1,1,0,0},{0,1,0,0},{0,0,0,0}}},
    },
    /* S */ {
        {{{0,1,1,0},{1,1,0,0},{0,0,0,0},{0,0,0,0}}},
        {{{1,0,0,0},{1,1,0,0},{0,1,0,0},{0,0,0,0}}},
        {{{0,1,1,0},{1,1,0,0},{0,0,0,0},{0,0,0,0}}},
        {{{1,0,0,0},{1,1,0,0},{0,1,0,0},{0,0,0,0}}},
    },
    /* Z */ {
        {{{1,1,0,0},{0,1,1,0},{0,0,0,0},{0,0,0,0}}},
        {{{0,1,0,0},{1,1,0,0},{1,0,0,0},{0,0,0,0}}},
        {{{1,1,0,0},{0,1,1,0},{0,0,0,0},{0,0,0,0}}},
        {{{0,1,0,0},{1,1,0,0},{1,0,0,0},{0,0,0,0}}},
    },
    /* J */ {
        {{{1,0,0,0},{1,1,1,0},{0,0,0,0},{0,0,0,0}}},
        {{{0,1,1,0},{0,1,0,0},{0,1,0,0},{0,0,0,0}}},
        {{{0,0,0,0},{1,1,1,0},{0,0,1,0},{0,0,0,0}}},
        {{{0,1,0,0},{0,1,0,0},{1,1,0,0},{0,0,0,0}}},
    },
    /* L */ {
        {{{0,0,1,0},{1,1,1,0},{0,0,0,0},{0,0,0,0}}},
        {{{0,1,0,0},{0,1,0,0},{0,1,1,0},{0,0,0,0}}},
        {{{0,0,0,0},{1,1,1,0},{1,0,0,0},{0,0,0,0}}},
        {{{1,1,0,0},{0,1,0,0},{0,1,0,0},{0,0,0,0}}},
    },
};

typedef struct PieceNode {
    int                kind;  /* 0..6 */
    struct PieceNode  *next;
} PieceNode;

typedef struct {
    PieceNode *head;
    PieceNode *tail;
    int        size;
} Queue;

static int g_board[ROWS][COLS]; /* 0 = 空, 1 = 固定済み */
static int g_cur_kind, g_cur_rot, g_cur_r, g_cur_c;
static uint64_t g_score = 0;
static int      g_alive = 1;

/* ----- Queue (heap) -----
   片方向連結リスト + tail ポインタの「キュー」 実装。
   push (末尾追加) / pop (先頭取出) が両方とも O(1) になるのが tail ポインタの効能。 */
static void q_push(Queue *q, int kind) {
    /* malloc(sizeof(*n)): 「n の指す先と同じサイズ」 を確保する書き方。
       sizeof(PieceNode) と書くより、 後でリネームしてもバグらない。 */
    PieceNode *n = malloc(sizeof(*n));
    if (!n) { perror("malloc"); exit(1); }
    n->kind = kind; n->next = NULL;
    /* 末尾に繋ぐ:
         空キュー → head にもセット
         非空 → 現 tail の next にぶら下げる */
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

static void q_free(Queue *q) {
    while (q->head) (void)q_pop(q);
}

/* 7-bag: 0..6 を Fisher-Yates でシャッフルしてキューに 7 個 push。
   Fisher-Yates のキモ:
     for i = N-1 downto 1:
       j = random(0..i)        // 含む両端
       swap(arr[i], arr[j])
   各置換が等確率なので、 結果として N! 通りの並びが全て等確率で得られる。
   1 ループ 1 swap で O(N)、 追加メモリ不要。 */
static void refill_bag(Queue *q) {
    int bag[7] = {0,1,2,3,4,5,6};
    for (int i = 6; i > 0; i--) {
        int j = rand() % (i + 1);   /* 0..i の整数 (rand の周期/偏りはこの場では許容) */
        int t = bag[i]; bag[i] = bag[j]; bag[j] = t;  /* 3 行 swap */
    }
    for (int i = 0; i < 7; i++) q_push(q, bag[i]);
}

/* ----- ピース配置 ----- */
static int try_place(int kind, int rot, int r, int c) {
    const Shape *sh = &SHAPES[kind][rot];
    for (int i = 0; i < 4; i++) for (int j = 0; j < 4; j++) {
        if (!sh->cells[i][j]) continue;
        int nr = r + i, nc = c + j;
        if (nr < 0 || nr >= ROWS || nc < 0 || nc >= COLS) return 0;
        if (g_board[nr][nc])                              return 0;
    }
    return 1;
}

static void lock_piece(void) {
    const Shape *sh = &SHAPES[g_cur_kind][g_cur_rot];
    for (int i = 0; i < 4; i++) for (int j = 0; j < 4; j++)
        if (sh->cells[i][j]) g_board[g_cur_r + i][g_cur_c + j] = 1;
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
        } else {
            r--;
        }
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

/* ----- 描画 ----- */
static void render(const Queue *q) {
    tty_clear_screen();
    /* 枠 */
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
    floor[0] = '+';
    for (int i = 1; i <= COLS; i++) floor[i] = '-';
    floor[COLS + 1] = '+'; floor[COLS + 2] = '\0';
    (void)write(STDOUT_FILENO, floor, (size_t)(COLS + 2));

    /* 現在ピース */
    const Shape *sh = &SHAPES[g_cur_kind][g_cur_rot];
    for (int i = 0; i < 4; i++) for (int j = 0; j < 4; j++) {
        if (!sh->cells[i][j]) continue;
        int rr = g_cur_r + i, cc = g_cur_c + j;
        if (rr >= 0 && rr < ROWS && cc >= 0 && cc < COLS) {
            tty_move_cursor(rr + 1, cc + 2);
            (void)write(STDOUT_FILENO, "@", 1);
        }
    }

    /* HUD + Next 表示 */
    char hud[120];
    int  n = snprintf(hud, sizeof(hud),
                      "\x1b[1;%dHSCORE %llu  Q=%d  q:quit  ←→:move ↑:rotate ↓:soft drop  space:hard drop",
                      COLS + 5, (unsigned long long)g_score, q->size);
    if (n > 0) (void)write(STDOUT_FILENO, hud, (size_t)n);
}

/* ----- 入力 ----- */
static void try_move(int dr, int dc) {
    if (try_place(g_cur_kind, g_cur_rot, g_cur_r + dr, g_cur_c + dc)) {
        g_cur_r += dr; g_cur_c += dc;
    }
}
static void try_rotate(void) {
    int nr = (g_cur_rot + 1) % 4;
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

int main(void) {
    srand((unsigned)time(NULL));
    tty_raw_mode(); tty_hide_cursor();

    Queue queue = {0};
    refill_bag(&queue);
    if (!spawn(&queue)) { g_alive = 0; }

    long acc = 0;
    while (g_alive) {
        if (handle_input(&queue)) break;
        acc += 16;
        if (acc >= 500) { /* ~0.5 秒で 1 マス自然落下 */
            acc = 0;
            if (try_place(g_cur_kind, g_cur_rot, g_cur_r + 1, g_cur_c)) {
                g_cur_r++;
            } else {
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
