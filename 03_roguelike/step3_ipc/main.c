/*
 * 03_roguelike/step3_ipc/main.c — 第 9 章 (fork + pipe で AI 別プロセス)
 *
 * 学習材料:
 *   - fork() で親子に分岐
 *   - pipe() を 2 本張って双方向通信 (parent→child: world snapshot / child→parent: move)
 *   - 子の stdin/stdout は pipe に dup2 で繋ぎ、 親は端末を独占
 *   - SIGCHLD で子の異常終了を検知し、 ゲームループは継続
 *   - メッセージは「行ベースのテキストプロトコル」(学習用に読みやすく)
 *
 * 本実装は学習用の最小骨格。 realistic に書くと数倍の行数になるが、
 * fork/pipe/dup2/SIGCHLD の流れを 1 ファイルで読み通せる事を優先。
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

static void on_sigchld(int sig) {
    (void)sig;
    /* WNOHANG で reap (handler 内では waitpid のみが async-signal-safe 認定) */
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

    /* parent → child の pipe (p2c) と child → parent の pipe (c2p) */
    int p2c[2], c2p[2];
    if (pipe(p2c) == -1 || pipe(c2p) == -1) { perror("pipe"); return 1; }

    /* SIGCHLD で reap */
    struct sigaction sa = {0};
    sa.sa_handler = on_sigchld; sa.sa_flags = SA_RESTART | SA_NOCLDSTOP;
    sigemptyset(&sa.sa_mask);
    sigaction(SIGCHLD, &sa, NULL);

    pid_t pid = fork();
    if (pid < 0) { perror("fork"); return 1; }
    if (pid == 0) {
        /* 子: stdin = p2c[0], stdout = c2p[1]、 ttyは触らない */
        dup2(p2c[0], STDIN_FILENO);
        dup2(c2p[1], STDOUT_FILENO);
        /* 全部閉じる (dup2 で残ったコピー以外は不要) */
        close(p2c[0]); close(p2c[1]); close(c2p[0]); close(c2p[1]);
        run_ai_child();   /* never returns */
    }
    /* 親: 使わない端を閉じる。 これを忘れると子が終わっても read が EOF にならない */
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
