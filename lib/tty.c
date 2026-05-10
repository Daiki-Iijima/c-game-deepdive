/*
 * lib/tty.c
 * 1 章 (termios 解剖) と 2 章以降の共有実装。
 * 1 章本文では、ここの各行が何を意味するか順に解剖する。
 */
#include "tty.h"

#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <termios.h>
#include <unistd.h>

static struct termios g_orig_termios;
static int            g_raw_active = 0;

/* atexit / signal の両方から呼ばれる。複数回呼ばれても安全に。
   write(2) は async-signal-safe。printf は不可なので使わない。
   注: tcsetattr は厳密には async-signal-safe ではない (POSIX signal-safety(7))。
   端末を救うための実用的な割り切りで、第 8 章 (Roguelike signal) でこの判断を再訪する。 */
void tty_restore(void) {
    if (!g_raw_active) return;
    tcsetattr(STDIN_FILENO, TCSAFLUSH, &g_orig_termios);
    /* カーソル復活 + 通常画面に戻すための ANSI sequence。
       1 章で「なぜこのバイト列なのか」を解説する。 */
    const char restore_seq[] = "\x1b[?25h\x1b[0m";
    (void)write(STDOUT_FILENO, restore_seq, sizeof(restore_seq) - 1);
    g_raw_active = 0;
}

static void on_signal(int sig) {
    tty_restore();
    /* デフォルトハンドラに戻して再送 → プロセスは正しい終了コードで死ぬ */
    signal(sig, SIG_DFL);
    raise(sig);
}

void tty_raw_mode(void) {
    if (g_raw_active) return;

    if (tcgetattr(STDIN_FILENO, &g_orig_termios) == -1) {
        perror("tcgetattr");
        exit(EXIT_FAILURE);
    }
    atexit(tty_restore);

    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = on_signal;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = SA_RESTART;
    sigaction(SIGINT,  &sa, NULL);
    sigaction(SIGTERM, &sa, NULL);
    sigaction(SIGQUIT, &sa, NULL);

    struct termios raw = g_orig_termios;
    /* ICANON=行単位入力 OFF / ECHO=入力エコー OFF / ISIG=Ctrl-C を SIGINT へ → こちらで処理 */
    raw.c_lflag &= (tcflag_t)~(ICANON | ECHO | ISIG);
    /* IXON=Ctrl-S/Q のフロー制御 OFF / ICRNL=CR→NL 変換 OFF / BRKINT, INPCK, ISTRIP は伝統的設定 */
    raw.c_iflag &= (tcflag_t)~(IXON | ICRNL | BRKINT | INPCK | ISTRIP);
    /* OPOST=出力後処理 OFF (\n を \r\n に変えない) */
    raw.c_oflag &= (tcflag_t)~(OPOST);
    /* 1 文字 1 byte (8bit クリーン) */
    raw.c_cflag |= (tcflag_t)CS8;
    /* VMIN=0 VTIME=0 → 即時 read。入力が無ければ 0 を返す (ノンブロッキング) */
    raw.c_cc[VMIN]  = 0;
    raw.c_cc[VTIME] = 0;

    if (tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw) == -1) {
        perror("tcsetattr");
        exit(EXIT_FAILURE);
    }
    g_raw_active = 1;
}

int tty_read_nonblock(char *out) {
    ssize_t n = read(STDIN_FILENO, out, 1);
    if (n == 1) return 1;
    if (n == 0) return 0;
    if (errno == EAGAIN || errno == EINTR) return 0;
    return -1;
}

void tty_clear_screen(void) {
    const char seq[] = "\x1b[2J\x1b[H";
    (void)write(STDOUT_FILENO, seq, sizeof(seq) - 1);
}

void tty_move_cursor(int row, int col) {
    char buf[32];
    int  len = snprintf(buf, sizeof(buf), "\x1b[%d;%dH", row, col);
    if (len > 0) (void)write(STDOUT_FILENO, buf, (size_t)len);
}

void tty_hide_cursor(void) {
    const char seq[] = "\x1b[?25l";
    (void)write(STDOUT_FILENO, seq, sizeof(seq) - 1);
}

void tty_show_cursor(void) {
    const char seq[] = "\x1b[?25h";
    (void)write(STDOUT_FILENO, seq, sizeof(seq) - 1);
}

void tty_size(int *rows, int *cols) {
    struct winsize ws;
    if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) == -1) {
        if (rows) *rows = 0;
        if (cols) *cols = 0;
        return;
    }
    if (rows) *rows = ws.ws_row;
    if (cols) *cols = ws.ws_col;
}
