/*
 * 01_snake/step1_termios/s4_isig/main.c
 * --------------------------------------------------------------------------
 * Step 4/5: ICANON + ECHO + ISIG を OFF にする。
 *
 * Step 3 との差分は ISIG ビットを落とすだけ。
 * すると:
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
    /* Step 3 からの差分: ISIG も落とす。
       ISIG は line discipline の「特定のバイトを signal に変換する」 機能:
         0x03 (Ctrl-C) → SIGINT
         0x1A (Ctrl-Z) → SIGTSTP
         0x1C (Ctrl-\) → SIGQUIT
       OFF にした瞬間、 これらは「ただのバイト」 として read に届く。
       ゲームで Ctrl-C をポーズに割り当てたいときの土台になる。 */
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

        /* なぜ 0x03 を文字定数ではなく数値で書く?
           Ctrl-C を「'\3'」 と書いてもよいが、 ASCII の制御文字は数値で書く方が
           「これは制御コードのバイト」 と読み手に伝わりやすい慣習。
           ISIG が OFF だから SIGINT にならず 1 byte 入力として届く。 */
        if (c == 'q' || c == 0x03) {
            printf("\r\nquit by %s\r\n",
                   (c == 0x03) ? "Ctrl-C (0x03)" : "'q'");
            break;
        }
        /* 表示可能な ASCII (32-126) はそのまま、 制御文字は '?' に置換。
           矢印キー (0x1B '[' 'A') を押すと '?' '[' 'A' の 3 行が一気に出る。
           それが Step 5 でパースする「3 byte ESC シーケンス」 の正体。 */
        printf("  got: '%c' (0x%02X)\r\n",
               (c >= 32 && c < 127) ? c : '?',
               (unsigned char)c);
        fflush(stdout);
    }

    /* 正常終了経路の復元。
       'q' か Ctrl-C で抜けるとここに来て端末が戻る。
       ただし別端末から `kill -9 PID` で殺されると、 ここに辿り着けないので
       端末が ICANON/ECHO/ISIG すべて OFF のまま放置される事故になる。
       Step 5 で atexit + sigaction を入れて塞ぐ。 */
    tcsetattr(STDIN_FILENO, TCSAFLUSH, &orig);
    return 0;
}
