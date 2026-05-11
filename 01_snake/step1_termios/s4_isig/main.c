/*
 * 01_snake/step1_termios/s4_isig/main.c
 * --------------------------------------------------------------------------
 * Step 4/5: ICANON + ECHO + ISIG を OFF にする。
 *
 * Step 3 との差分は ISIG ビットを落とすだけ。 すると:
 *   - Ctrl-C は SIGINT に変換されなくなり、 0x03 のバイトとして read に届く
 *   - Ctrl-Z (SIGTSTP), Ctrl-\ (SIGQUIT) も同様
 *   = キーボード入力がカーネル line discipline に奪われない
 *
 * 「Ctrl-C を自前のクイットキーにする」 という支配感をここで体感する。
 *
 * 残る痛み:
 *   - kill コマンドで殺されたり segfault したりすると端末が raw のまま残る
 *   - これを Step 5 で atexit + sigaction で塞ぐ
 */
#include <stdio.h>
#include <termios.h>
#include <unistd.h>

int main(void) {
    struct termios orig;
    if (tcgetattr(STDIN_FILENO, &orig) == -1) {
        perror("tcgetattr"); return 1;
    }

    struct termios raw = orig;
    /* Step 3 からの差分: ISIG も落とす */
    raw.c_lflag &= (tcflag_t)~(ICANON | ECHO | ISIG);
    raw.c_cc[VMIN]  = 1;
    raw.c_cc[VTIME] = 0;
    if (tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw) == -1) {
        perror("tcsetattr"); return 1;
    }

    printf("type keys. q OR Ctrl-C to quit. Ctrl-C will NOT signal.\r\n");
    fflush(stdout);

    for (;;) {
        char c;
        ssize_t n = read(STDIN_FILENO, &c, 1);
        if (n != 1) break;

        /* 0x03 = Ctrl-C のバイト。 ISIG が OFF なので SIGINT にならず素のバイトで届く */
        if (c == 'q' || c == 0x03) {
            printf("\r\nquit by %s\r\n",
                   (c == 0x03) ? "Ctrl-C (0x03)" : "'q'");
            break;
        }
        printf("  got: '%c' (0x%02X)\r\n",
               (c >= 32 && c < 127) ? c : '?',
               (unsigned char)c);
        fflush(stdout);
    }

    tcsetattr(STDIN_FILENO, TCSAFLUSH, &orig);
    return 0;
}
