/*
 * 01_snake/step3_linkedlist/main.c
 * --------------------------------------------------------------------------
 * 第 3 章 (Snake v2 / 連結リスト + malloc)
 *
 * v1 (配列) からの差分:
 *   - body は struct Node の連結リスト (head ←- tail)
 *   - 食料を 1 個出して食べさせ、 リスト先頭に Node を 1 つ malloc して伸ばす
 *   - tail は通常 free して捨てる (動かない tick では tail を保持)
 *
 * 学習材料:
 *   - malloc / free を実コードで使う
 *   - 二重 free を「わざと起こす」モード (--bug=double-free) を仕込む
 *     → 第 11 章で valgrind / gdb 実戦で見るため、 本章は valgrind の最初の出会い
 *
 * 新登場 C 機能 / ライブラリ:
 *   - 自己参照構造体: struct Node { ...; struct Node *next; };
 *     → 連結リストの基本パターン
 *   - malloc / free: 動的メモリ確保 / 解放
 *   - NULL: ヌルポインタ定数 ((void *)0)
 *   - perror: errno を文字列化して stderr へ
 *   - exit / EXIT_FAILURE: プログラム異常終了
 *   - srand / rand / time: 疑似乱数の種付けと取得
 */
#include "tty.h"

#include <stdint.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>   /* malloc, free, exit, EXIT_FAILURE, rand, srand */
#include <string.h>   /* memset, strcmp */
#include <time.h>     /* time (乱数の種に使う) */
#include <unistd.h>

#define ROWS    20
#define COLS    60

typedef enum { DIR_UP, DIR_DOWN, DIR_LEFT, DIR_RIGHT } Direction;

/* 自己参照構造体 (= 自分自身の型へのポインタをメンバに持つ):
     struct Node { ... struct Node *next; };
   C 言語では「型 typedef のないタグ名」 を中で参照しないと「不完全型」 と
   いうエラーになる。 ここでは `struct Node` というタグを使い、 typedef でも
   `Node` という名前を別途与える、 という二段構え。 */
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
static int  g_bug_double_free = 0;  /* --bug=double-free 指定時に立つフラグ */

/* ---- マップ初期化 (前章と同様) -------------------------------------- */

static void map_clear(void) {
    memset(map, ' ', sizeof(map));
    for (int c = 0; c < COLS; c++) { map[0][c] = '#'; map[ROWS - 1][c] = '#'; }
    for (int r = 0; r < ROWS; r++) { map[r][0] = '#'; map[r][COLS - 1] = '#'; }
}

/* ---- ノード生成 ----------------------------------------------------- */

/* malloc(size_t n):
     heap 上に n バイトの領域を確保し、 その先頭アドレスを返す。
     失敗時は NULL を返す。 確保された領域の中身は **未初期化**
     (任意の bit 並び)。 必ず明示的に初期化すること。
   sizeof(*n) はポインタ n の指す先 (= Node 構造体) のサイズ。
     `sizeof(Node)` と書くより、 後から型を変えてもバグらない安全な書き方。 */
static Node *node_new(uint8_t r, uint8_t c) {
    Node *n = malloc(sizeof(*n));
    if (!n) {                /* malloc 失敗 (= NULL 返却) チェック */
        perror("malloc");    /* errno の文字列を stderr に */
        exit(EXIT_FAILURE);  /* EXIT_FAILURE = 1 */
    }
    n->r = r; n->c = c;
    n->next = NULL;          /* NULL = ヌルポインタ。 リスト末尾の番兵 */
    return n;
}

/* ---- 全ノード解放 (リストを 1 個ずつ free) -------------------------- */

/* 「リストを舐めながら、 現在ノードを覚えて先に next を保持し、 free する」
   の典型ループ。 free(p) すると p の指す領域は使えなくなるので、 p->next を
   先に保存しておく必要がある。 */
static void snake_free(Snake *s) {
    Node *p = s->head;
    while (p) {
        Node *next = p->next;   /* 先に保存 (free 後は p->next は読めない) */
        free(p);                /* heap に返却 */
        p = next;
    }
    s->head = s->tail = NULL;
    s->len  = 0;
}

/* ---- 食料配置 ------------------------------------------------------- */

/* シンプルに繰り返しランダム。 蛇が画面を埋めると無限ループになるので注意 (演習)。
   rand() % N で 0..N-1 の値を取る (偏りが多少ある古典的方法)。 */
static void place_food(Snake *s) {
    for (;;) {
        int r = 1 + rand() % (ROWS - 2);
        int c = 1 + rand() % (COLS - 2);
        int hit = 0;
        /* p; p; p = p->next:
             for 文の典型形を リストの走査に流用したパターン。
             ;_; のような書き方も同等だが、 こちらが Linux カーネルの慣習。 */
        for (Node *p = s->head; p; p = p->next) {
            if (p->r == r && p->c == c) { hit = 1; break; }
        }
        if (!hit) { s->food_r = (uint8_t)r; s->food_c = (uint8_t)c; return; }
    }
}

/* ---- 蛇初期化 ------------------------------------------------------- */

static void snake_init(Snake *s) {
    s->head = s->tail = NULL;
    s->len = 0; s->dir = DIR_RIGHT; s->alive = 1; s->score = 0;
    int r = ROWS / 2, c = COLS / 2;
    /* 5 個のノードを head → tail の順に malloc して繋ぐ */
    for (int i = 0; i < 5; i++) {
        Node *n = node_new((uint8_t)r, (uint8_t)(c - i));
        if (!s->head) s->head = n;    /* 最初のノードは head にも記録 */
        else          s->tail->next = n;  /* それ以外は前の tail->next に繋ぐ */
        s->tail = n;
        s->len++;
    }
    place_food(s);
}

/* ---- 次の頭位置を計算 ----------------------------------------------- */

/* 出力をポインタで返す (C で「2 つの値を返したい」 ときの定番)。
   `uint8_t *` のように `*` を付けると 「uint8_t へのポインタ」。 */
static void next_head_pos(const Snake *s, uint8_t *out_r, uint8_t *out_c) {
    uint8_t r = s->head->r, c = s->head->c;
    switch (s->dir) {
        case DIR_UP:    if (r > 0)         r--; break;
        case DIR_DOWN:  if (r < ROWS - 1)  r++; break;
        case DIR_LEFT:  if (c > 0)         c--; break;
        case DIR_RIGHT: if (c < COLS - 1)  c++; break;
    }
    *out_r = r; *out_c = c;   /* ポインタ経由で書き戻す */
}

/* ---- 衝突判定 ------------------------------------------------------- */

static int will_collide(const Snake *s, uint8_t r, uint8_t c) {
    if (map[r][c] == '#') return 1;
    /* tail は今回 tick で剥がれるので除外。
       `p && p != s->tail` で「NULL でない かつ tail でもない」 を表現。 */
    for (Node *p = s->head; p && p != s->tail; p = p->next) {
        if (p->r == r && p->c == c) return 1;
    }
    return 0;
}

/* ---- 1 tick 進める -------------------------------------------------- */

static void snake_step(Snake *s) {
    uint8_t nr, nc;
    next_head_pos(s, &nr, &nc);
    /* eat 判定: 次の頭位置が食料の位置と一致するか */
    int eat = (nr == s->food_r && nc == s->food_c);
    if (will_collide(s, nr, nc)) { s->alive = 0; return; }

    /* 新しい head ノードを malloc して付ける。
       実演:
         new_head → old_head → … → tail
       というリストの先頭挿入。 */
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
           --bug=double-free 指定時は 2 回 free する → valgrind 出番。

           片方向リストでは「tail の 1 個前を見つける」 のは線形時間。
           尾の追加削除を高速化したいなら 双方向リスト (prev も持つ) に進化させる。 */
        Node *old_tail = s->tail;
        Node *prev     = s->head;
        while (prev && prev->next != old_tail) prev = prev->next;
        if (prev) prev->next = NULL;
        s->tail = prev;
        free(old_tail);
        if (g_bug_double_free) {
            free(old_tail); /* ← 意図的な二重 free。 valgrind が叫ぶ。 */
        }
    }
}

/* ---- 描画 ----------------------------------------------------------- */

static void render(const Snake *s) {
    tty_clear_screen();
    for (int r = 0; r < ROWS; r++) {
        tty_move_cursor(r + 1, 1);
        for (int c = 0; c < COLS; c++) (void)write(STDOUT_FILENO, &map[r][c], 1);
    }
    /* 食料 */
    tty_move_cursor(s->food_r + 1, s->food_c + 1);
    (void)write(STDOUT_FILENO, "*", 1);
    /* 蛇 (頭は @、 体は o) */
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

/* ---- 入力 ----------------------------------------------------------- */

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

/* ========================================================================
 * main
 * ====================================================================== */

int main(int argc, char **argv) {
    /* コマンドライン引数で --bug=double-free を探す */
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--bug=double-free") == 0) g_bug_double_free = 1;
    }
    /* srand(seed): 疑似乱数の種を設定。 time(NULL) で「現在の Unix 時刻 (秒)」 を得て
       種に使う = 毎回違う乱数列。 unsigned へのキャストは API シグニチャに合わせて。 */
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
    /* ゲームオーバ表示 */
    render(&s);
    tty_move_cursor(ROWS + 2, 1);
    const char msg[] = "GAME OVER. press any key to quit.\r\n";
    (void)write(STDOUT_FILENO, msg, sizeof(msg) - 1);
    char dummy;
    while (tty_read_nonblock(&dummy) == 0) msleep(20);

    snake_free(&s);  /* 終了前に必ず free。 これを抜くと valgrind が leak を報告。 */
    return 0;
}
