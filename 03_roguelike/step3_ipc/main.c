/*
 * 03_roguelike/step3_ipc/main.c
 * --------------------------------------------------------------------------
 * 第 9 章 (fork + pipe で AI 別プロセス)
 *
 * 学習材料:
 *   - fork()      : 自分自身のクローンを作る syscall。 戻り値で親/子を識別
 *   - pipe()      : カーネル内に「単方向のバイトの流れ」 を 1 本作る
 *   - dup2()      : ファイルディスクリプタの番号を付け替える (= リダイレクト)
 *   - waitpid()   : 子プロセスを reap (= 終了状態を回収) する
 *   - SIGCHLD     : 子が死んだとき親に飛んでくる signal
 *
 * 構成:
 *   parent ──── p2c[1] ──→ pipe ──→ p2c[0] ──── child stdin
 *   parent ←── c2p[0] ←── pipe ←── c2p[1] ──── child stdout
 *
 * メッセージは行ベースのテキストプロトコル (学習用に読みやすく):
 *   親→子: "tick <pr> <pc> <mr> <mc>\n"
 *   子→親: "move <dr> <dc>\n"
 *
 * 本実装は学習用の最小骨格。 fork/pipe/dup2/SIGCHLD の流れを 1 ファイルで
 * 読み通せる事を優先しているため、 本格的なエラー処理は端折ってある。
 */
#include "tty.h"

#include <errno.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

#define ROWS 16
#define COLS 50

static char     g_map[ROWS][COLS];
static int      g_player_r = 1, g_player_c = 1;
static int      g_monster_r = 0, g_monster_c = 0;
static volatile sig_atomic_t g_child_dead = 0;

/* SIGCHLD handler:
   子プロセスの状態変化 (主に終了) を親が拾うための signal。
   ハンドラ内では async-signal-safe な関数しか呼べないが、 waitpid は OK。

   waitpid(pid, status, options):
     pid = -1  : どの子でもいい (= 一番先に変化があった子)
     status   : 終了状態を書き戻す先 (今回は使わない)
     WNOHANG  : 「変化があれば返す、 無ければ即時 0」 のノンブロッキングモード
   while で連続呼び出ししているのは、 同時に複数の子が死んだ場合に
   1 回の signal で複数 reap するため (= シグナルは合成される)。 */
static void on_sigchld(int sig) {
    (void)sig;  /* 引数を使わない警告抑制の定番イディオム */
    int status;
    while (waitpid(-1, &status, WNOHANG) > 0) g_child_dead = 1;
}

/* ----- マップ生成 (固定 1 部屋 + プレイヤと敵を配置) ----- */
static void map_init(void) {
    memset(g_map, ' ', sizeof(g_map));
    for (int c = 0; c < COLS; c++) { g_map[0][c] = '#'; g_map[ROWS - 1][c] = '#'; }
    for (int r = 0; r < ROWS; r++) { g_map[r][0] = '#'; g_map[r][COLS - 1] = '#'; }
    g_player_r = 1;             g_player_c = 1;
    g_monster_r = ROWS - 2;     g_monster_c = COLS - 2;
}

/* ----- 子プロセス (AI): stdin に "tick pr pc mr mc\n" を読み、
   stdout に "move dr dc\n" を返す。 単純な 1 マス追跡 AI。 ----- */
static void run_ai_child(void) {
    char buf[256];
    while (fgets(buf, sizeof(buf), stdin)) {
        int pr, pc, mr, mc;
        if (sscanf(buf, "tick %d %d %d %d", &pr, &pc, &mr, &mc) != 4) continue;
        int dr = 0, dc = 0;
        if      (pr < mr) dr = -1;
        else if (pr > mr) dr =  1;
        else if (pc < mc) dc = -1;
        else if (pc > mc) dc =  1;
        printf("move %d %d\n", dr, dc);
        fflush(stdout);
    }
    _exit(0);  /* parent から pipe を閉じられたら正常終了 */
}

/* 親が AI に「世界の状態」を送って答えを待つ */
static int ask_ai(int wfd, int rfd, int *out_dr, int *out_dc) {
    char buf[64];
    int  n = snprintf(buf, sizeof(buf), "tick %d %d %d %d\n",
                      g_player_r, g_player_c, g_monster_r, g_monster_c);
    if (write(wfd, buf, (size_t)n) != n) return -1;

    /* 行を 1 つ受け取る (簡素化のためノンブロッキングではない) */
    char inb[64]; size_t got = 0;
    while (got < sizeof(inb) - 1) {
        ssize_t r = read(rfd, inb + got, 1);
        if (r <= 0) {
            if (r < 0 && errno == EINTR) continue;
            return -1;
        }
        if (inb[got] == '\n') { inb[got] = 0; break; }
        got++;
    }
    return sscanf(inb, "move %d %d", out_dr, out_dc) == 2 ? 0 : -1;
}

static void render(void) {
    tty_clear_screen();
    for (int r = 0; r < ROWS; r++) {
        tty_move_cursor(r + 1, 1);
        (void)write(STDOUT_FILENO, g_map[r], (size_t)COLS);
    }
    tty_move_cursor(g_player_r + 1, g_player_c + 1);
    (void)write(STDOUT_FILENO, "@", 1);
    tty_move_cursor(g_monster_r + 1, g_monster_c + 1);
    (void)write(STDOUT_FILENO, "M", 1);
    char hud[120];
    int  n = snprintf(hud, sizeof(hud),
                      "\x1b[%d;1Hhjkl=move q=quit  ai_dead=%s",
                      ROWS + 1, g_child_dead ? "yes" : "no");
    if (n > 0) (void)write(STDOUT_FILENO, hud, (size_t)n);
}

static void msleep(int ms) {
    struct timespec ts = { ms / 1000, (ms % 1000) * 1000000L };
    nanosleep(&ts, NULL);
}

int main(void) {
    map_init();

    /* pipe(int fd[2]):
         カーネルにバッファを 1 個と、 そこを読み書きする 2 つの fd を作る。
           fd[0] = 読み口 (read end)
           fd[1] = 書き口 (write end)
         pipe は **単方向**。 双方向通信したいなら 2 本作る必要がある。
       p2c (parent → child): 親が書き、 子が読む
       c2p (child  → parent): 子が書き、 親が読む */
    int p2c[2], c2p[2];
    if (pipe(p2c) == -1 || pipe(c2p) == -1) { perror("pipe"); return 1; }

    /* SIGCHLD で reap */
    struct sigaction sa = {0};
    sa.sa_handler = on_sigchld; sa.sa_flags = SA_RESTART | SA_NOCLDSTOP;
    sigemptyset(&sa.sa_mask);
    sigaction(SIGCHLD, &sa, NULL);

    /* fork(): 呼び出したプロセスを **そっくり複製** する syscall。
       戻り値で親/子を識別:
         > 0  : 親側に返る。 値は子の PID
         == 0 : 子側に返る (= 「自分は子だ」)
         < 0  : 失敗 (リソース不足など)
       fork 直後、 親と子は全く同じメモリ状態だが、 以降は別プロセスとして
       独立に動く (Copy-on-Write でメモリは共有される → 書き込み時にコピー)。 */
    pid_t pid = fork();
    if (pid < 0) { perror("fork"); return 1; }
    if (pid == 0) {
        /* === ここから子プロセス === */
        /* dup2(oldfd, newfd):
             newfd を一度 close してから、 oldfd の指す先を newfd に複製する。
             ここで子の stdin (= fd 0) を pipe の読み口に、 stdout (= fd 1) を
             pipe の書き口に、 すり替えている。 こうすると子の標準 I/O 関数
             (fgets, printf, ...) が自動的に pipe と会話するようになる。 */
        dup2(p2c[0], STDIN_FILENO);    /* 子の stdin ← parent → child の read 端 */
        dup2(c2p[1], STDOUT_FILENO);   /* 子の stdout → child → parent の write 端 */
        /* dup2 で番号 0/1 として残るので、 元の番号 (p2c[0] = 3 等) はもう不要。
           pipe の全 fd を閉じる (リーク防止 + デッドロック防止)。 */
        close(p2c[0]); close(p2c[1]); close(c2p[0]); close(c2p[1]);
        run_ai_child();   /* 子はこの中で _exit(0) する。 ここには戻らない */
    }
    /* === ここから親プロセス === */
    /* 親: 使わない端を閉じる (重要):
         p2c[0] は子だけが読む側 → 親では不要
         c2p[1] は子だけが書く側 → 親では不要
       これを忘れると、 子が終わっても親側に書き口/読み口が残っていて
       「自分自身に書き込めるから EOF が来ない」 という典型デッドロックになる。 */
    close(p2c[0]); close(c2p[1]);

    tty_raw_mode(); tty_hide_cursor();

    long acc = 0;
    for (;;) {
        char c;
        if (tty_read_nonblock(&c) == 1) {
            if (c == 'q') break;
            int dr = 0, dc = 0;
            switch (c) {
                case 'h': dc = -1; break;
                case 'l': dc =  1; break;
                case 'k': dr = -1; break;
                case 'j': dr =  1; break;
            }
            int nr = g_player_r + dr, nc = g_player_c + dc;
            if (g_map[nr][nc] != '#') { g_player_r = nr; g_player_c = nc; }
            render();
        }
        acc += 16;
        if (acc >= 300) {
            acc = 0;
            int dr = 0, dc = 0;
            if (!g_child_dead && ask_ai(p2c[1], c2p[0], &dr, &dc) == 0) {
                int nr = g_monster_r + dr, nc = g_monster_c + dc;
                if (g_map[nr][nc] != '#') { g_monster_r = nr; g_monster_c = nc; }
            }
            render();
            if (g_player_r == g_monster_r && g_player_c == g_monster_c) {
                tty_move_cursor(ROWS + 2, 1);
                const char msg[] = "caught by AI! press q.\r\n";
                (void)write(STDOUT_FILENO, msg, sizeof(msg) - 1);
            }
        }
        msleep(16);
    }

    /* 親が pipe を閉じれば、 子は fgets で EOF を受けて _exit。 SIGCHLD で reap される */
    close(p2c[1]); close(c2p[0]);
    waitpid(pid, NULL, 0);
    return 0;
}
