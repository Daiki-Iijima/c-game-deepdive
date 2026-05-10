/*
 * lib/tty.c
 * --------------------------------------------------------------------------
 * 第 1 章 (termios 解剖) と第 2 章以降の共有実装。
 * 第 1 章本文では、 ここの各行が何を意味するか順に解剖する。
 *
 * 使っている標準ライブラリ / システムコール (初出順):
 *   - <termios.h>     : 端末属性 struct termios と tcgetattr/tcsetattr
 *   - <unistd.h>      : POSIX 標準ヘッダ。 read / write / STDIN_FILENO 等
 *   - <signal.h>      : signal handling (sigaction, sigset_t, sig_atomic_t)
 *   - <sys/ioctl.h>   : ioctl() と TIOCGWINSZ (端末サイズ取得)
 *   - <fcntl.h>       : ファイル制御 (本ファイルでは将来用にだけ)
 *   - <errno.h>       : errno グローバル変数と EAGAIN/EINTR 定数
 */
#include "tty.h"

#include <errno.h>      /* errno 変数、 EAGAIN / EINTR 定数 */
#include <fcntl.h>      /* fcntl() ファイル制御 (現状は未使用、 拡張のため) */
#include <signal.h>     /* sigaction, sigset_t */
#include <stdio.h>      /* perror() — エラーメッセージ出力 */
#include <stdlib.h>     /* atexit(), exit(), EXIT_FAILURE */
#include <string.h>     /* memset() */
#include <sys/ioctl.h>  /* ioctl() と TIOCGWINSZ */
#include <termios.h>    /* struct termios と tc*attr() */
#include <unistd.h>     /* read(), write(), STDIN_FILENO, STDOUT_FILENO */

/* ファイルローカルな状態を保存。
   - g_orig_termios: raw mode に入る前の端末設定を退避するため。
     終了時にここから復元すれば 元の状態に戻せる。
   - g_raw_active : 多重呼び出し防止のフラグ。 0/1 で十分。
   "static" を付けるとそのファイル内だけで見える 「ファイルスコープ変数」。
   ヘッダから見えないので、 他のファイルが誤って書き換える事故を防げる。 */
static struct termios g_orig_termios;
static int            g_raw_active = 0;

/* atexit / signal の両方から呼ばれる。 複数回呼ばれても安全に。
   write(2) は async-signal-safe。 printf は不可なので使わない。
   注: tcsetattr は厳密には async-signal-safe ではない (POSIX signal-safety(7))。
   端末を救うための実用的な割り切りで、 第 8 章 (Roguelike signal) でこの判断を再訪する。 */
void tty_restore(void) {
    if (!g_raw_active) return;

    /* tcsetattr(fd, optional_action, termios_p):
       端末の属性を設定する POSIX 関数。
         - fd                : 対象ファイルディスクリプタ (STDIN_FILENO = 0)
         - TCSAFLUSH         : 既に入力バッファに溜まっている文字を破棄してから設定
                               (他に TCSANOW = 即時、 TCSADRAIN = 出力 flush 待ち)
         - &g_orig_termios   : 設定値の入った構造体へのポインタ */
    tcsetattr(STDIN_FILENO, TCSAFLUSH, &g_orig_termios);

    /* カーソル復活 + 通常画面に戻すための ANSI エスケープシーケンス。
       1 章で「なぜこのバイト列なのか」を解説する。
         \x1b[?25h = カーソル表示 ON
         \x1b[0m  = 文字属性リセット (色などをデフォルトに)
       sizeof(restore_seq) - 1 で末尾の '\0' を除いたバイト数を渡す。 */
    const char restore_seq[] = "\x1b[?25h\x1b[0m";
    (void)write(STDOUT_FILENO, restore_seq, sizeof(restore_seq) - 1);
    /* (void) キャストは戻り値を 「明示的に捨てる」 イディオム。
       コンパイラが -Wunused-result の警告を出さなくなる。 */

    g_raw_active = 0;
}

/* signal handler。 sig_atomic_t 以外を触らない、 と教科書には書いてあるが、
   端末を救うために特例で tcsetattr を呼ぶ — 詳細は第 8 章。 */
static void on_signal(int sig) {
    tty_restore();

    /* 「ハンドラ内で復元 → デフォルトハンドラに戻す → 自分自身に同じ signal を投げる」
       というイディオム。 こうするとプロセスは「signal で殺された」 という
       正しい終了コード (e.g. 128 + signum) で死ねる。 */
    signal(sig, SIG_DFL);  /* 古い signal(2) API。 簡易にハンドラ解除するなら便利 */
    raise(sig);            /* raise(sig) = 自分自身に signal を送る = kill(getpid(), sig) と同義 */
}

void tty_raw_mode(void) {
    if (g_raw_active) return;  /* 多重呼び出しガード */

    /* tcgetattr: 現在の端末設定を取得し、 後で復元できるよう退避。 */
    if (tcgetattr(STDIN_FILENO, &g_orig_termios) == -1) {
        perror("tcgetattr");  /* errno を文字列化して stderr に出すヘルパ */
        exit(EXIT_FAILURE);   /* EXIT_FAILURE = 1 (POSIX 標準終了コード) */
    }

    /* atexit: プロセスの正常終了時 (return from main, exit() 呼び出し時)
       に自動で呼ばれる関数を登録する標準 C 関数。 ここで tty_restore を仕込む。
       _exit(2) や signal で死ぬときは呼ばれないので、 signal handler 側でも保険として復元する。 */
    atexit(tty_restore);

    /* sigaction で signal handler を登録する。
       struct sigaction の主要メンバ:
         - sa_handler  : void (*)(int) のシンプルなハンドラ
         - sa_sigaction: void (*)(int, siginfo_t *, void *) 拡張ハンドラ
         - sa_mask     : ハンドラ実行中に追加でブロックする signal 集合
         - sa_flags    : SA_RESTART, SA_NOCLDSTOP などの動作フラグ
       memset で 0 クリアしてから必要なフィールドだけ埋めるのが定石。 */
    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = on_signal;
    sigemptyset(&sa.sa_mask);  /* マスク集合を空に初期化 (必須) */
    sa.sa_flags = SA_RESTART;  /* 中断された syscall を自動で再開させる */
    sigaction(SIGINT,  &sa, NULL);  /* Ctrl-C */
    sigaction(SIGTERM, &sa, NULL);  /* kill コマンドのデフォルト signal */
    sigaction(SIGQUIT, &sa, NULL);  /* Ctrl-\ */

    /* 退避済みの設定を「ベースライン」 にして、 そこから必要なビットだけ
       OFF にする。 元の値をコピーするのは「速度や文字コードなどの設定を
       維持する」 ため。 */
    struct termios raw = g_orig_termios;

    /* ビット演算で フラグを OFF にする。
         x &= ~MASK   <=>   x = x & (~MASK)
       MASK のビットだけが落ちる (他は保持)。
       (tcflag_t) のキャストは -Wconversion 対応 (uint で型がうるさい)。

       c_lflag (ローカルモード = line discipline 本体) で落とすもの:
         ICANON : 行編集モード。 OFF にすると Enter を待たず 1 byte 届く
         ECHO   : 入力文字エコー。 OFF にすると押したキーが画面に出ない
         ISIG   : Ctrl-C などを signal 変換。 OFF にすると 0x03 が生で届く */
    raw.c_lflag &= (tcflag_t)~(ICANON | ECHO | ISIG);

    /* c_iflag (入力モード) で落とすもの:
         IXON   : Ctrl-S/Q によるフロー制御
         ICRNL  : CR (\r) を NL (\n) に自動変換
         BRKINT : BREAK 受信時の signal 化
         INPCK  : パリティチェック
         ISTRIP : 上位 bit のストリップ */
    raw.c_iflag &= (tcflag_t)~(IXON | ICRNL | BRKINT | INPCK | ISTRIP);

    /* c_oflag (出力モード) で落とすもの:
         OPOST  : 出力後処理 (例: \n → \r\n 変換)。 OFF にすると raw な byte がそのまま */
    raw.c_oflag &= (tcflag_t)~(OPOST);

    /* c_cflag (制御モード) は CS8 (8-bit クリーン) を立てる。 OR で立てる。
         x |= MASK    <=>   x = x | MASK   */
    raw.c_cflag |= (tcflag_t)CS8;

    /* c_cc は特殊文字配列。 VMIN/VTIME は read のブロッキング挙動を制御:
         VMIN=0, VTIME=0  → 即時 return (ノンブロッキング)
         VMIN=1, VTIME=0  → 1 文字届くまでブロック
         VMIN=0, VTIME=N  → N×0.1 秒タイムアウト */
    raw.c_cc[VMIN]  = 0;
    raw.c_cc[VTIME] = 0;

    if (tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw) == -1) {
        perror("tcsetattr");
        exit(EXIT_FAILURE);
    }
    g_raw_active = 1;
}

/* read(2) syscall で 1 byte だけ取りに行く。
   戻り値:
     n > 0 = 読めた byte 数 (今回は 1 byte 期待)
     n = 0 = 入力なし (VMIN=0/VTIME=0 のとき)
     n < 0 = エラー、 errno に詳細
   EAGAIN / EINTR は「リトライしてよい」 扱いの一過性エラー。 */
int tty_read_nonblock(char *out) {
    ssize_t n = read(STDIN_FILENO, out, 1);
    if (n == 1) return 1;
    if (n == 0) return 0;
    if (errno == EAGAIN || errno == EINTR) return 0;
    return -1;
}

/* ANSI エスケープ:
     \x1b[2J = 画面消去
     \x1b[H  = カーソルを (1,1) (= 左上) へ */
void tty_clear_screen(void) {
    const char seq[] = "\x1b[2J\x1b[H";
    (void)write(STDOUT_FILENO, seq, sizeof(seq) - 1);
}

/* ANSI エスケープ: \x1b[<row>;<col>H で絶対カーソル位置。 1-origin。
   snprintf で安全に文字列組み立て。 */
void tty_move_cursor(int row, int col) {
    char buf[32];
    int  len = snprintf(buf, sizeof(buf), "\x1b[%d;%dH", row, col);
    if (len > 0) (void)write(STDOUT_FILENO, buf, (size_t)len);
}

void tty_hide_cursor(void) {
    const char seq[] = "\x1b[?25l";  /* DECTCEM カーソル非表示 */
    (void)write(STDOUT_FILENO, seq, sizeof(seq) - 1);
}
void tty_show_cursor(void) {
    const char seq[] = "\x1b[?25h";  /* DECTCEM カーソル表示 */
    (void)write(STDOUT_FILENO, seq, sizeof(seq) - 1);
}

/* ioctl(fd, TIOCGWINSZ, &ws) で端末ウィンドウサイズを取得。
   ioctl は「ファイルディスクリプタ経由でデバイス固有の制御を呼ぶ」 汎用 syscall。
   引数 2 つ目のリクエスト番号で挙動が変わる。 TIOCGWINSZ は「ウィンドウサイズを GET」。 */
void tty_size(int *rows, int *cols) {
    struct winsize ws;
    if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) == -1) {
        if (rows) *rows = 0;
        if (cols) *cols = 0;
        return;
    }
    /* ポインタ越し代入。 呼び出し側は &my_rows のようにアドレスを渡す。
       NULL チェックを入れているのは安全のため (呼び出し側のミス防御)。 */
    if (rows) *rows = ws.ws_row;
    if (cols) *cols = ws.ws_col;
}
