/*
 * 01_snake/step1_termios/main.c
 *
 * 第 1 章のゴール: termios を raw mode にして、キー入力を取り、矢印キーで
 * 1 文字 (今回は @ ) を画面上で動かす。Snake の "頭" だけが先に動く状態。
 *
 * 章本文と対応するため、ここでは敢えて lib/tty を使わず実装を main.c の中に
 * 全部展開している。第 2 章以降は lib/tty を使う。
 */
#include <errno.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <termios.h>
#include <time.h>
#include <unistd.h>

static struct termios g_orig;
static int            g_raw = 0;

static void restore(void) {
    if (!g_raw) return;
    tcsetattr(STDIN_FILENO, TCSAFLUSH, &g_orig);
    /* カーソル戻す + 画面消去 */
    const char seq[] = "\x1b[?25h\x1b[2J\x1b[H";
    (void)write(STDOUT_FILENO, seq, sizeof(seq) - 1);
    g_raw = 0;
}

/* 注: tcsetattr / write は厳密には async-signal-safe ではない (POSIX signal-safety(7))。
   緊急復元としては実用上動くが、第 8 章 (Roguelike signal) で正しい signal-safe な書き方を扱う。
   ここでは「終了処理を取り戻すための割り切り」として目を瞑る。 */
static void on_signal(int sig) {
    restore();
    signal(sig, SIG_DFL);
    raise(sig);
}

static void enter_raw(void) {
    if (tcgetattr(STDIN_FILENO, &g_orig) == -1) { perror("tcgetattr"); exit(1); }
    atexit(restore);
    struct sigaction sa = {0};
    sa.sa_handler = on_signal;
    /* SA_RESTART: ハンドラ復帰後 read 等を自動再開させる作法。
       本章は VMIN=0/VTIME=0 のため EINTR 実害は無いが、教材として明示する。 */
    sa.sa_flags = SA_RESTART;
    sigemptyset(&sa.sa_mask);
    sigaction(SIGINT,  &sa, NULL);
    sigaction(SIGTERM, &sa, NULL);

    struct termios raw = g_orig;
    raw.c_lflag &= (tcflag_t)~(ICANON | ECHO | ISIG);
    raw.c_iflag &= (tcflag_t)~(IXON | ICRNL);
    raw.c_oflag &= (tcflag_t)~(OPOST);
    raw.c_cc[VMIN]  = 0;
    raw.c_cc[VTIME] = 0;
    if (tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw) == -1) { perror("tcsetattr"); exit(1); }
    g_raw = 1;

    const char hide[] = "\x1b[?25l\x1b[2J\x1b[H";
    (void)write(STDOUT_FILENO, hide, sizeof(hide) - 1);
}

/* 矢印キーは ESC [ A/B/C/D の 3 byte シーケンス。raw mode で 1 byte ずつ読む。 */
typedef enum { KEY_NONE, KEY_UP, KEY_DOWN, KEY_LEFT, KEY_RIGHT, KEY_QUIT } Key;

static Key read_key(void) {
    char c;
    ssize_t n = read(STDIN_FILENO, &c, 1);
    if (n <= 0) return KEY_NONE;
    if (c == 'q') return KEY_QUIT;
    if (c != '\x1b') return KEY_NONE;

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

static void draw_at(int row, int col, char ch) {
    char buf[32];
    int  n = snprintf(buf, sizeof(buf), "\x1b[%d;%dH%c", row, col, ch);
    if (n > 0) (void)write(STDOUT_FILENO, buf, (size_t)n);
}

static void msleep(int ms) {
    struct timespec ts = { ms / 1000, (ms % 1000) * 1000000L };
    nanosleep(&ts, NULL);
}

int main(void) {
    enter_raw();

    int row = 10, col = 20;
    int prev_row = row, prev_col = col;
    draw_at(row, col, '@');
    fflush(stdout);

    for (;;) {
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
        msleep(16); /* 60fps 相当 */
    }
    return 0;
}
