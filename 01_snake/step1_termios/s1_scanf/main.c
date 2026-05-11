/*
 * 01_snake/step1_termios/s1_scanf/main.c
 * --------------------------------------------------------------------------
 * Step 1/5: scanf の限界を体感する。
 *
 * これは「壊れた版」 ではなく 「素朴な版」。 termios も raw mode も登場しない。
 * scanf は 行編集モード (ICANON ON) のターミナルから 1 文字を取るので、
 * Enter キーを押すまで何も帰ってこない。 ゲームには明らかに不適切。
 *
 * 「何が痛いか」 を体感したうえで Step 2 へ進む。
 */
#include <stdio.h>

int main(void) {
    printf("press a key then Enter: ");
    fflush(stdout);                 /* stdout は line-buffered なので flush 推奨 */

    char c;
    if (scanf(" %c", &c) != 1) {    /* " %c" の先頭スペースで空白文字を読み飛ばす */
        fprintf(stderr, "scanf failed\n");
        return 1;
    }
    printf("you pressed: '%c' (0x%02X)\n", c, (unsigned char)c);
    return 0;
}
