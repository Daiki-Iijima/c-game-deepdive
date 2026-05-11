/*
 * 01_snake/step1_termios/s5_full/main.c
 * --------------------------------------------------------------------------
 * 第 1 章のゴール:
 *   termios を raw mode に切り替えて、 矢印キーで 1 文字 (@) を画面上で動かす。
 *   Snake の「頭」だけが先に動く状態。
 *
 * 章本文と対応するため、 ここでは敢えて lib/tty.h を使わず実装を main.c の中に
 * 全部展開している。 第 2 章以降は lib/tty を使う。
 *
 * 記事との対応 (zenn books/c-game-deepdive/termios.md Step 5/5):
 *   Step 5a (グローバル状態 + restore)  → 下記 "----- raw mode 復元 -----" ブロック
 *   Step 5b (signal handler on_signal)   → 下記 "----- signal handler -----" ブロック
 *   Step 5c (enter_raw 完全版)            → 下記 "----- raw mode 設定 -----" ブロック
 *   Step 5d (typedef enum + read_key)     → 下記 "----- キー入力 -----" ブロック
 *   Step 5e (draw_at + msleep + main)     → 下記 "----- 描画 / ms スリープ / メインループ -----"
 *
 * 使う C 機能 / 標準ライブラリ / syscall (初出):
 *   - struct, typedef enum   : 構造体定義 / 列挙型定義
 *   - static                 : ファイルスコープに閉じ込める
 *   - 関数ポインタ           : sa_handler に渡している
 *   - <termios.h>            : 端末属性 (struct termios, tcgetattr/setattr)
 *   - <unistd.h>             : read(2)/write(2) syscall ラッパと STDIN/OUT_FILENO
 *   - <signal.h>             : sigaction(2) と signal handler
 *   - <time.h>               : nanosleep(2) と struct timespec
 *   - <stdio.h>              : perror(), snprintf() (本ファイルでは snprintf)
 *   - <stdlib.h>             : atexit(), exit(), EXIT_FAILURE
 *   - <string.h>             : memset()
 *   - <errno.h>              : errno (タッチしないが将来用)
 */
#include <errno.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <termios.h>
#include <time.h>
#include <unistd.h>

/* ---- グローバル状態 (このファイル内でしか見えない) [記事 Step 5a] ---------- */

/* g_orig: raw mode に入る前の termios 設定を退避する場所。
   "g_" prefix は global の意味 (規則ではなく筆者の慣例)。 */
static struct termios g_orig;
static int            g_raw = 0;  /* raw mode 中なら 1 */

/* ---- raw mode 復元 [記事 Step 5a: restore] -------------------------- */

/* atexit と signal の両方から呼ばれる関数。 何度呼ばれても安全。 */
static void restore(void) {
    if (!g_raw) return;

    /* tcsetattr(STDIN_FILENO, TCSAFLUSH, &g_orig):
         端末を g_orig の設定に戻す。
         TCSAFLUSH = 「入力バッファをまずクリアしてから設定変更」 のフラグ。 */
    tcsetattr(STDIN_FILENO, TCSAFLUSH, &g_orig);

    /* ANSI エスケープシーケンスでカーソル表示 ON + 画面クリア + カーソル左上 */
    const char seq[] = "\x1b[?25h\x1b[2J\x1b[H";

    /* write(fd, buf, count):
         低レベル I/O syscall。 stdio (printf 等) と違ってバッファリング
         しないので、 signal handler や緊急時に向く。
         (void) で戻り値を捨てている = エラー処理を諦めるイディオム。
         sizeof(seq) - 1 で末尾の '\0' を除いたバイト数。 */
    (void)write(STDOUT_FILENO, seq, sizeof(seq) - 1);

    g_raw = 0;
}

/* ---- signal handler [記事 Step 5b: on_signal] ----------------------- */
/* 注: tcsetattr / write は厳密には async-signal-safe ではない (POSIX signal-safety(7))。
   緊急復元としては実用上動くが、 第 8 章 (Roguelike signal) で正しい signal-safe な書き方を扱う。
   ここでは「終了処理を取り戻すための割り切り」として目を瞑る。 */
static void on_signal(int sig) {
    restore();
    /* 「signal の挙動をデフォルトに戻して、 自分自身に同じ signal を投げる」 イディオム。
       こうすることで、 プロセスは正しい終了コード (e.g. 130 = 128 + SIGINT) で死ねる。 */
    signal(sig, SIG_DFL);   /* 古い signal(2) API。 簡易にハンドラ解除するだけ */
    raise(sig);             /* 自分自身に signal を再送 = kill(getpid(), sig) と等価 */
}

/* ---- raw mode 設定 [記事 Step 5c: enter_raw] ------------------------ */

static void enter_raw(void) {
    /* 現在の端末設定を g_orig に退避。 失敗したら即座に exit。 */
    if (tcgetattr(STDIN_FILENO, &g_orig) == -1) {
        perror("tcgetattr");   /* errno を読みやすい文字列にして stderr に出す */
        exit(1);
    }
    /* atexit: 正常終了 (return from main, exit()) 時に呼ぶ関数を登録。 */
    atexit(restore);

    /* sigaction で signal handler を登録。 古い signal() より制御が細かい。
       struct sigaction を {0} で初期化 → 余分なフィールドを 0 クリア。
       (C99 以降の指定初期化子と一緒に使える書き方。) */
    struct sigaction sa = {0};
    sa.sa_handler = on_signal;

    /* SA_RESTART: ハンドラ復帰後 read 等を自動再開させる作法。
       本章は VMIN=0/VTIME=0 のため EINTR 実害は無いが、 教材として明示する。 */
    sa.sa_flags = SA_RESTART;
    sigemptyset(&sa.sa_mask);  /* マスク集合を空に。 必ず初期化すること */
    sigaction(SIGINT,  &sa, NULL);  /* Ctrl-C 受信時 */
    sigaction(SIGTERM, &sa, NULL);  /* kill のデフォルト signal */

    /* 退避済み設定をベースに、 必要なビットだけ落とす */
    struct termios raw = g_orig;
    /* ICANON: 行編集モード (Enter まで溜める) - OFF にして 1 byte 即時取得
       ECHO:   入力文字エコー - OFF にしてキーが画面に出ないように
       ISIG:   Ctrl-C などを signal 変換 - OFF にして 0x03 を生で取得 */
    raw.c_lflag &= (tcflag_t)~(ICANON | ECHO | ISIG);
    /* IXON: Ctrl-S/Q によるフロー制御 - OFF
       ICRNL: CR → NL 変換 - OFF (Enter を \r のまま取得) */
    raw.c_iflag &= (tcflag_t)~(IXON | ICRNL);
    /* OPOST: 出力後処理 (\n → \r\n 等) - OFF にして write のバイトをそのまま出す */
    raw.c_oflag &= (tcflag_t)~(OPOST);
    /* VMIN/VTIME: 即時 read (ノンブロッキング相当) */
    raw.c_cc[VMIN]  = 0;
    raw.c_cc[VTIME] = 0;
    if (tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw) == -1) {
        perror("tcsetattr"); exit(1);
    }
    g_raw = 1;

    /* カーソル非表示 + 画面クリア + カーソル左上。 ゲームらしい初期状態に。 */
    const char hide[] = "\x1b[?25l\x1b[2J\x1b[H";
    (void)write(STDOUT_FILENO, hide, sizeof(hide) - 1);
}

/* ---- キー入力 [記事 Step 5d: typedef enum Key + read_key] ----------- */

/* 矢印キーは ESC `[` `A/B/C/D` の 3 byte シーケンス。
   raw mode では「上矢印」 というキーは無く、 ターミナルが 3 byte を順に送る。 */
typedef enum {
    KEY_NONE, KEY_UP, KEY_DOWN, KEY_LEFT, KEY_RIGHT, KEY_QUIT
} Key;

/* read_key: 1 文字 (= ESC シーケンスなら 3 byte) を取って Key 列挙値で返す。
   VMIN=0/VTIME=0 のため、 入力なしなら read は即時 0 を返す → KEY_NONE。 */
static Key read_key(void) {
    char c;
    /* read(fd, buf, n): n byte 読む。 戻り値が 1 なら成功、 0 なら入力なし、
       負ならエラー (errno に詳細)。 ssize_t は signed size_t (負を表せる)。 */
    ssize_t n = read(STDIN_FILENO, &c, 1);
    if (n <= 0) return KEY_NONE;
    if (c == 'q') return KEY_QUIT;
    if (c != '\x1b') return KEY_NONE;  /* ESC でなければ無視 */

    /* ESC を受けたら続く 2 byte を取って種類を見分ける */
    char seq[2];
    if (read(STDIN_FILENO, &seq[0], 1) != 1) return KEY_NONE;
    if (read(STDIN_FILENO, &seq[1], 1) != 1) return KEY_NONE;
    if (seq[0] != '[') return KEY_NONE;
    switch (seq[1]) {
        case 'A': return KEY_UP;
        case 'B': return KEY_DOWN;
        case 'C': return KEY_RIGHT;
        case 'D': return KEY_LEFT;
        default:  return KEY_NONE;
    }
}

/* ---- 描画 [記事 Step 5e: draw_at] ----------------------------------- */

/* 指定位置に 1 文字書く。
   snprintf(buf, size, fmt, ...):
     printf 系。 buf に最大 size バイトまで書き込む安全版。
     戻り値は「もし size 制限が無ければ書き込んだはずのバイト数」。 */
static void draw_at(int row, int col, char ch) {
    char buf[32];
    int  n = snprintf(buf, sizeof(buf), "\x1b[%d;%dH%c", row, col, ch);
    if (n > 0) (void)write(STDOUT_FILENO, buf, (size_t)n);
}

/* ---- ms スリープ [記事 Step 5e: msleep] ----------------------------- */

/* nanosleep(req, rem):
     ナノ秒精度のスリープ syscall。
     struct timespec { tv_sec, tv_nsec }; tv_nsec の単位は 1/10億 秒。
     第 2 引数は割り込まれた時の残り時間を書き戻す先 (今回は不要なので NULL)。 */
static void msleep(int ms) {
    struct timespec ts = { ms / 1000, (ms % 1000) * 1000000L };
    nanosleep(&ts, NULL);
}

/* ---- メインループ [記事 Step 5e: main] ------------------------------ */

int main(void) {
    enter_raw();

    /* 蛇の頭の位置を 1-origin の (row, col) で持つ。 prev は前フレームの位置で、
       「前回の位置を空白で消し → 新しい位置に @ を書く」 という差分描画に使う。 */
    int row = 10, col = 20;
    int prev_row = row, prev_col = col;
    draw_at(row, col, '@');
    fflush(stdout);  /* stdio バッファを flush。 write 直書きと混在する場合の保険 */

    for (;;) {  /* 永久ループ。 break で抜ける */
        Key k = read_key();
        if (k == KEY_QUIT) break;
        switch (k) {
            case KEY_UP:    if (row > 1)  row--; break;
            case KEY_DOWN:  if (row < 24) row++; break;
            case KEY_LEFT:  if (col > 1)  col--; break;
            case KEY_RIGHT: if (col < 80) col++; break;
            default: break;
        }
        if (row != prev_row || col != prev_col) {
            draw_at(prev_row, prev_col, ' ');
            draw_at(row, col, '@');
            prev_row = row; prev_col = col;
        }
        msleep(16);  /* 約 60fps 相当 */
    }
    /* return すると atexit(restore) が走り端末復帰。 端末を救うフェイルセーフ。 */
    return 0;
}
