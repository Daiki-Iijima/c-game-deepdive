/*
 * lib/tty.h
 * 章 01 で読者と一緒に解剖する termios + ANSI のラッパ。
 * 章 02 以降はこれを include して使い回す。中身は意図的に短く保ち、
 * "魔法の関数" にならないようにする。
 */
#ifndef CGD_TTY_H
#define CGD_TTY_H

#include <stddef.h>

/* raw mode に切り替える。終了時に必ず tty_restore() を呼ぶこと。
   atexit と SIGINT/SIGTERM ハンドラから呼ばれることを想定し、複数回呼んでも安全。 */
void tty_raw_mode(void);
void tty_restore(void);

/* ノンブロッキングで 1 文字読む。入力なしなら 0、エラーなら -1、成功なら 1 を返す。
   読めた文字は *out に格納される。VMIN=0/VTIME=0 設定前提。 */
int  tty_read_nonblock(char *out);

/* ANSI escape ヘルパ */
void tty_clear_screen(void);
void tty_move_cursor(int row, int col);  /* 1-origin */
void tty_hide_cursor(void);
void tty_show_cursor(void);

/* 端末サイズ取得 (TIOCGWINSZ)。失敗時は rows=cols=0。 */
void tty_size(int *rows, int *cols);

#endif /* CGD_TTY_H */
