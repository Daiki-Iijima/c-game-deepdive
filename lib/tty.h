/*
 * lib/tty.h
 * --------------------------------------------------------------------------
 * 章 01 (termios 解剖) で読者と一緒に書いた termios + ANSI のラッパ。
 * 章 02 以降はこれを include して使い回す。中身は意図的に短く保ち、
 * "魔法の関数" にならないようにする。
 *
 * このファイルはヘッダ。 実体は lib/tty.c。
 * 中で使う C 機能の解説:
 *   - #ifndef / #define / #endif:
 *       「インクルードガード」。 同じヘッダが複数の翻訳単位で重複
 *       展開されてエラーになるのを防ぐ古典的イディオム。
 *   - #include <stddef.h>:
 *       size_t などの標準型定義を取り込む。 ヘッダで size_t を引数に
 *       使うなら必ず必要。 今回は宣言だけなので軽量ヘッダを 1 つ。
 *   - void *戻り値 / void 引数:
 *       void = 「値が無い」 を表す型。 関数の戻り値が void なら
 *       何も返さない。 引数 (void) は「引数を取らない」 ことを明示。
 *       C では (void) と書かずに () だけにすると「不定」 とみなされ
 *       気付かないバグになりやすいため、 明示することが推奨される。
 *   - 関数プロトタイプ宣言:
 *       「型 関数名(引数の型一覧);」 の形で関数の存在をコンパイラに
 *       教える宣言。 実体は別ファイル (tty.c) にある。
 */
#ifndef CGD_TTY_H
#define CGD_TTY_H

#include <stddef.h>  /* size_t (今回は引数で直接は使わないが、 将来拡張のため) */

/* raw mode に切り替える。 終了時に必ず tty_restore() を呼ぶこと。
   atexit と SIGINT/SIGTERM ハンドラから呼ばれることを想定し、 複数回
   呼んでも安全に作ってある (= idempotent な実装)。 */
void tty_raw_mode(void);

/* raw mode を元に戻す。 通常終了時は atexit で自動呼び出しされる。 */
void tty_restore(void);

/* ノンブロッキングで 1 文字読む。
     戻り値 1  = 1 文字読めた (*out に格納)
     戻り値 0  = 入力なし (タイムアウト)
     戻り値 -1 = エラー
   VMIN=0 / VTIME=0 設定前提 (= read が即時 return)。 */
int  tty_read_nonblock(char *out);

/* --- ANSI エスケープシーケンスのヘルパ --- */
void tty_clear_screen(void);             /* 画面消去 + カーソルを左上 */
void tty_move_cursor(int row, int col);  /* (1-origin) でカーソル移動 */
void tty_hide_cursor(void);              /* カーソル非表示 (ゲーム中の見栄え用) */
void tty_show_cursor(void);              /* カーソル再表示 */

/* 端末サイズ取得 (TIOCGWINSZ ioctl)。 失敗時は rows = cols = 0。
   ポインタ経由で 2 つの値を返す古典 C パターン。 */
void tty_size(int *rows, int *cols);

#endif /* CGD_TTY_H */
