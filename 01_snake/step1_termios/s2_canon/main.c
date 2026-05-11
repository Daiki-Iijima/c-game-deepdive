/*
 * 01_snake/step1_termios/s2_canon/main.c
 * --------------------------------------------------------------------------
 * Step 2/5: ICANON だけ OFF にする (= 行編集を解除)。
 *
 * 退避 → ビット落とす → 設定 → read → 手で復元、 という最小骨格。
 * Enter 不要で 1 byte 即時取得できる。
 *
 * 残る痛み:
 *   - 押したキーが画面にエコーされる (ECHO がまだ ON)
 *   - 異常終了したら復元されない (atexit/sigaction はまだ無い)
 */
#include <stdio.h>
#include <termios.h>
#include <unistd.h>

int main(void) {
    /* 現状の端末属性を取って退避 */
    struct termios orig;
    if (tcgetattr(STDIN_FILENO, &orig) == -1) {
        perror("tcgetattr"); return 1;
    }

    /* コピーから ICANON だけ落とす */
    struct termios raw = orig;
    raw.c_lflag &= (tcflag_t)~ICANON;   /* 行編集 OFF (= 1 byte で read が返る) */
    raw.c_cc[VMIN]  = 1;                 /* 最低 1 byte 来るまでブロック */
    raw.c_cc[VTIME] = 0;                 /* タイムアウト無し */
    if (tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw) == -1) {
        perror("tcsetattr"); return 1;
    }

    printf("press a key (no Enter needed): ");
    fflush(stdout);

    /* read syscall: 1 byte 来るまでブロック */
    char c;
    ssize_t n = read(STDIN_FILENO, &c, 1);

    /* 復元。 ここに来る前に exit / Ctrl-C すると端末が ICANON OFF のまま残る */
    tcsetattr(STDIN_FILENO, TCSAFLUSH, &orig);

    if (n != 1) {
        fprintf(stderr, "\nread failed\n");
        return 1;
    }
    printf("\nyou pressed: '%c' (0x%02X)\n", c, (unsigned char)c);
    return 0;
}
