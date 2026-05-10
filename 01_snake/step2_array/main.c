/*
 * 01_snake/step2_array/main.c
 * --------------------------------------------------------------------------
 * 第 2 章 (Snake v1 / 配列で体を表現)
 *
 * 第 1 章で乗っ取ったキー入力を、 第 1 章のロジック (lib/tty) ごと使い回す。
 * 本章のテーマは「配列で蛇の体を持つ」「struct のサイズと padding を覗く」。
 *
 * 設計:
 *   - マップは char map[ROWS][COLS] の **スタック上** 2D 配列 (関数外定義のため厳密には BSS)
 *   - 体は Cell body[MAX_LEN] の固定長配列 + len で現在の長さを保持
 *   - tick で 頭を進め、 配列を後ろから前にずらす (素朴 O(N))
 *
 * 第 3 章では同じゲームを連結リスト + heap で書き直し、
 * 配列 vs リストのトレードオフを体感する。
 *
 * 新登場 C 機能 (本章で初めて出るもの):
 *   - <stdint.h>: 固定幅整数型 uint8_t / uint64_t など
 *   - <stddef.h>: offsetof マクロ、 size_t 型
 *   - typedef struct: 構造体に短い名前を付ける典型イディオム
 *   - enum: 列挙型。 整数を読みやすい名前で書ける
 *   - 2D 配列 char map[ROWS][COLS]: 行優先 (= 同じ行が連続) のメモリレイアウト
 *   - sizeof / offsetof: コンパイル時定数。 構造体のメモリレイアウトを覗ける
 *   - 文字列引数 (argc/argv): main(int argc, char **argv) でコマンドライン引数を取る
 */
#include "tty.h"

#include <stdint.h>   /* uint8_t, uint64_t など固定幅整数 */
#include <stddef.h>   /* offsetof(struct, member) マクロ */
#include <stdio.h>    /* printf, snprintf */
#include <stdlib.h>   /* exit (本ファイルでは未使用だが将来用) */
#include <string.h>   /* memset, strcmp */
#include <time.h>     /* nanosleep, struct timespec */
#include <unistd.h>   /* write, STDOUT_FILENO */

/* ---- 定数 (プリプロセッサマクロ) ------------------------------------- */
/* #define はテキスト置換。 ROWS が出てきたら 20 に置き換わる。
   const int でも同様に書けるが、 配列サイズに使うなら #define の方が
   古い C 規格でも通る (C90 で `int arr[ROWS][COLS]` の ROWS は定数式必須)。 */
#define ROWS    20
#define COLS    60
#define MAX_LEN 256

/* ---- 型定義 --------------------------------------------------------- */

/* enum:
     整数定数の集合に名前を付ける。 デフォルトでは 0, 1, 2, ... が順に割り当たる。
     `Direction` の正体は int 相当。 */
typedef enum { DIR_UP, DIR_DOWN, DIR_LEFT, DIR_RIGHT } Direction;

/* typedef struct { ... } Name;
     構造体定義と同時に typedef で別名を付けるイディオム。
     これにより `struct Cell c;` ではなく `Cell c;` と書ける。 */
typedef struct {
    uint8_t r;  /* 行 (0-origin、 画面では +1 する) */
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

/* ---- マップ (BSS 領域) ---------------------------------------------- */

/* 関数外で宣言した変数は **static でなくとも** ファイルスコープ。
   さらに 初期値が無いので **BSS (.bss) セクション** に配置される。
   BSS = 「ゼロ初期化が保証された静的領域」 で、 ELF にはサイズだけ書かれ
   実体は持たない (ロード時にカーネルがゼロで埋める)。 */
static char map[ROWS][COLS];

/* ---- マップ初期化 --------------------------------------------------- */

static void map_clear(void) {
    /* memset(dest, byte_value, n): n バイトを byte_value で埋める。
       ここでは ' ' (= 0x20) で全マスを埋めて「空白」 状態に。 */
    memset(map, ' ', sizeof(map));
    /* 外周を壁 '#' に */
    for (int c = 0; c < COLS; c++) { map[0][c] = '#'; map[ROWS - 1][c] = '#'; }
    for (int r = 0; r < ROWS; r++) { map[r][0] = '#'; map[r][COLS - 1] = '#'; }
}

/* ---- 蛇初期化 ------------------------------------------------------- */

/* 引数 `Snake *s`:
     構造体は通常 ポインタで渡す (= 参照渡し相当)。 値渡しだと巨大なコピーが
     発生してコストが高い。 `s->field` で中身にアクセス。 (*s).field でも同じ。 */
static void snake_init(Snake *s) {
    s->len   = 5;
    s->dir   = DIR_RIGHT;
    s->alive = 1;
    s->score = 0;
    /* 中央付近に水平に置く */
    int r = ROWS / 2, c = COLS / 2;
    for (int i = 0; i < s->len; i++) {
        /* (uint8_t) は明示キャスト。 int → uint8_t の縮小変換を
           「意図的にやってる」 と コンパイラに伝える (-Wconversion 対応)。 */
        s->body[i].r = (uint8_t)r;
        s->body[i].c = (uint8_t)(c - i);
    }
}

/* ---- 衝突判定 ------------------------------------------------------- */

/* `const Snake *s`:
     ポインタ越しに読むだけ、 書き換えはしない、 という契約を const で明示。
     呼び出し側に対する保証であり、 コンパイラの最適化材料でもある。 */
static int snake_will_collide(const Snake *s, Cell next) {
    if (map[next.r][next.c] == '#') return 1;
    /* 自分の体チェック (頭が動く先に体があるか)。 tail (= body[len-1]) は
       直後にずれて空くので除外する。 */
    for (int i = 0; i < s->len - 1; i++) {
        if (s->body[i].r == next.r && s->body[i].c == next.c) return 1;
    }
    return 0;
}

/* ---- 頭の次の位置を計算 --------------------------------------------- */

/* Cell を値で返す。 構造体の値返しは C99 以降で普通に使える。 */
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

/* ---- 1 tick 進める -------------------------------------------------- */

static void snake_step(Snake *s) {
    Cell h = next_head(s);
    if (snake_will_collide(s, h)) { s->alive = 0; return; }
    /* 後ろから前へずらす: body[len-1] ← body[len-2] ← … ← body[0] ← new_head
       Cell 構造体の値コピー。 中身がポインタを持たないので浅いコピーで完結。 */
    for (int i = s->len - 1; i > 0; i--) s->body[i] = s->body[i - 1];
    s->body[0] = h;
}

/* ---- 描画 ----------------------------------------------------------- */

static void render(const Snake *s) {
    tty_clear_screen();
    for (int r = 0; r < ROWS; r++) {
        tty_move_cursor(r + 1, 1);
        /* &map[r][0] で 行の先頭アドレスを取り、 COLS バイトを write。
           2D 配列の各行はメモリ上で連続している (= 行優先レイアウト) ので、
           この 1 回の write で 1 行分を出せる。 */
        for (int c = 0; c < COLS; c++) (void)write(STDOUT_FILENO, &map[r][c], 1);
    }
    for (int i = 0; i < s->len; i++) {
        tty_move_cursor(s->body[i].r + 1, s->body[i].c + 1);
        /* 三項演算子 cond ? a : b: cond が真なら a、 偽なら b。 */
        char ch = (i == 0) ? '@' : 'o';
        (void)write(STDOUT_FILENO, &ch, 1);
    }
    /* HUD (Heads Up Display) = ゲーム下部のスコア表示。
       \x1b[<row>;1H で絶対カーソル位置、 そこに文字列を出力。
       `unsigned long long` キャストは printf 系の %llu フォーマットに合わせるため。 */
    char hud[80];
    int  n = snprintf(hud, sizeof(hud),
                      "\x1b[%d;1HSCORE %llu  LEN %d   q:quit",
                      ROWS + 1, (unsigned long long)s->score, s->len);
    if (n > 0) (void)write(STDOUT_FILENO, hud, (size_t)n);
}

/* ---- 入力 ----------------------------------------------------------- */

/* 戻り値:
     1 = 終了 (q が押された)
     0 = 続行 */
static int handle_input(Snake *s) {
    char c;
    int  r = tty_read_nonblock(&c);  /* 1=読めた / 0=なし / -1=エラー */
    if (r != 1) return 0;
    if (c == 'q') return 1;
    if (c != '\x1b') return 0;  /* ESC でなければ無視 */
    /* 矢印キーは ESC [ A/B/C/D。 VTIME=0 のためすぐ来ていなければ取りこぼす想定。 */
    char seq[2];
    if (tty_read_nonblock(&seq[0]) != 1) return 0;
    if (tty_read_nonblock(&seq[1]) != 1) return 0;
    if (seq[0] != '[') return 0;
    /* 逆方向への即時反転を防ぐ (蛇ゲームの定番ルール) */
    switch (seq[1]) {
        case 'A': if (s->dir != DIR_DOWN)  s->dir = DIR_UP;    break;
        case 'B': if (s->dir != DIR_UP)    s->dir = DIR_DOWN;  break;
        case 'C': if (s->dir != DIR_LEFT)  s->dir = DIR_RIGHT; break;
        case 'D': if (s->dir != DIR_RIGHT) s->dir = DIR_LEFT;  break;
    }
    return 0;
}

/* ---- スリープ ------------------------------------------------------- */

static void msleep(int ms) {
    struct timespec ts = { ms / 1000, (ms % 1000) * 1000000L };
    nanosleep(&ts, NULL);
}

/* ========================================================================
 * main: --inspect モード or ゲーム本体
 * ====================================================================== */

/* int argc, char **argv:
     argc = 引数の個数 (プログラム名自体も 1 個目)
     argv = 引数文字列の配列。 argv[0] = プログラム名、 argv[1] 以降が引数。
     char ** は「文字列の配列へのポインタ」。 */
int main(int argc, char **argv) {
    /* --inspect オプション: ゲームを起動せず、 構造体サイズを表示して終了する。
       第 2 章 §observe でこの出力を読む。
       strcmp(a, b) は文字列比較。 0 なら等しい、 負/正なら辞書順前後。 */
    if (argc == 2 && strcmp(argv[1], "--inspect") == 0) {
        /* %zu は size_t 用フォーマット (z = 「size_t 修飾子」)。
           sizeof / offsetof の戻り値は size_t。 */
        printf("sizeof(Cell)        = %zu\n", sizeof(Cell));
        printf("sizeof(Snake)       = %zu\n", sizeof(Snake));
        /* offsetof(構造体型, メンバ名) はマクロ。 そのメンバが構造体先頭から
           何バイト目にあるかを返す。 padding を可視化する黄金ツール。 */
        printf("offsetof(body)      = %zu\n", offsetof(Snake, body));
        printf("offsetof(len)       = %zu\n", offsetof(Snake, len));
        printf("offsetof(dir)       = %zu\n", offsetof(Snake, dir));
        printf("offsetof(alive)     = %zu\n", offsetof(Snake, alive));
        printf("offsetof(score)     = %zu\n", offsetof(Snake, score));
        printf("MAX_LEN * sizeof(Cell) = %zu (= body フィールドの素のサイズ)\n",
               MAX_LEN * sizeof(Cell));
        /* %p はポインタ用フォーマット。 void* にキャストする (規格上必須)。 */
        Snake s_on_stack;
        printf("&s_on_stack         = %p (stack)\n", (void *)&s_on_stack);
        printf("&map                = %p (BSS/data)\n", (void *)map);
        return 0;
    }

    /* ---- ゲーム本体 ---- */
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
    /* キー入力を 1 回待つ (busy-wait + 短いスリープ) */
    char dummy;
    while (tty_read_nonblock(&dummy) == 0) msleep(20);
    return 0;
}
