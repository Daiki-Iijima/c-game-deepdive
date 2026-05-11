/*
 * 01_snake/step1_termios/s1_scanf/main.c
 * --------------------------------------------------------------------------
 * Step 1/5: scanf の限界を体感する。
 *
 * これは「壊れた版」 ではなく 「素朴な版」。
 * termios も raw mode も登場しない。
 * scanf は 行編集モード (ICANON ON) のターミナルから 1 文字を取るので、
 * Enter キーを押すまで何も帰ってこない。
 * ゲームには明らかに不適切。
 *
 * 「何が痛いか」 を体感したうえで Step 2 へ進む。
 */
#include <stdio.h>

int main(void) {
    printf("press a key then Enter: ");
    /* なぜ fflush(stdout)?
       stdout は端末に繋がっているとき "行バッファ" モード = 改行 \n が来るまで
       実際の write(2) syscall は呼ばれず、 stdio 内部のバッファに溜まる。
       上のプロンプトは末尾に改行が無いので、 flush しないと画面に出ない。
       fflush(stdout) で強制的に write(2) を発火させて表示させる。 */
    fflush(stdout);

    char c;
    /* なぜ " %c" の先頭にスペース?
       書式指定子 " " は「空白文字 (改行/タブ/スペース) を読み飛ばせ」 という指示。
       これを書かないと、 前回の入力の末尾に残った \n を拾って scanf が
       「空白文字 1 個」 を返してきてしまい、 ユーザーが何も押していないのに
       即時 c = '\n' で進んでしまう罠がある。 */
    if (scanf(" %c", &c) != 1) {
        /* なぜ stderr に書く?
           エラーメッセージは慣習で stderr に出す。
           シェルで `./prog 2>err.log` のように標準出力と分離できるよう。
           また stderr は端末では無バッファなので即時表示される。 */
        fprintf(stderr, "scanf failed\n");
        return 1;
    }
    printf("you pressed: '%c' (0x%02X)\n", c, (unsigned char)c);
    return 0;
}
