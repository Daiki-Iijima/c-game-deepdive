/*
 * 01_snake/step1_termios/s3_echo/main.c
 * --------------------------------------------------------------------------
 * Step 3/5: ICANON + ECHO を OFF にする。
 *
 * Step 2 との差分は ECHO ビットを落とすだけ。 押したキーが画面に出なくなる
 * (= 自分の出力だけが見える静かな世界に近づく)。
 *
 * ループにして 'q' で抜けるようにしたので、 「キーを連打しても画面が荒れない」
 * 体験を実感できる。
 *
 * 残る痛み:
 *   - Ctrl-C を押すと ISIG が SIGINT に変換し、 端末が raw のまま死ぬ
 *   - そもそも異常終了したら復元されない
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
    /* Step 2 からの差分: ECHO も落とす */
    raw.c_lflag &= (tcflag_t)~(ICANON | ECHO);
    raw.c_cc[VMIN]  = 1;
    raw.c_cc[VTIME] = 0;
    if (tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw) == -1) {
        perror("tcsetattr"); return 1;
    }

    printf("type keys (q to quit). nothing should echo to the screen:\r\n");
    fflush(stdout);

    for (;;) {
        char c;
        ssize_t n = read(STDIN_FILENO, &c, 1);
        if (n != 1) break;
        if (c == 'q') break;
        /* キーを画面に echo するのは 「自分の write」 だけ。 OS は echo しない */
        printf("  got: '%c' (0x%02X)\r\n", c, (unsigned char)c);
        fflush(stdout);
    }

    tcsetattr(STDIN_FILENO, TCSAFLUSH, &orig);
    printf("bye\n");
    return 0;
}
