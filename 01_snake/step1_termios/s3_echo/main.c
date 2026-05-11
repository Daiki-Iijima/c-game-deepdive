/*
 * 01_snake/step1_termios/s3_echo/main.c
 * --------------------------------------------------------------------------
 * Step 3/5: ICANON + ECHO を OFF にする。
 *
 * Step 2 との差分は ECHO ビットを落とすだけ。
 * 押したキーが画面に出なくなる (= 自分の出力だけが見える静かな世界に近づく)。
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
    /* Step 2 からの差分: ECHO も落とす。
       ECHO は line discipline の「入力をそのまま STDOUT にコピー」 機能。
       ON だとカーネルが勝手にキー入力を画面に書き戻すので、
       ゲーム画面で W/A/S/D を連打するとそのまま文字が表示されて荒れる。
       OFF にすれば「自分が write したものだけが画面に出る」 静かな世界になる。 */
    raw.c_lflag &= (tcflag_t)~(ICANON | ECHO);
    raw.c_cc[VMIN]  = 1;
    raw.c_cc[VTIME] = 0;
    if (tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw) == -1) {
        perror("tcsetattr"); return 1;
    }

    /* なぜ \r\n? \n だけだとダメ?
       Step 5 で OPOST を OFF にすると、 \n は \r\n に自動変換されなくなる。
       この step では OPOST がまだ ON なので \n だけでも動くが、
       「raw 寄りの世界では \r\n が必要」 という感覚を早めに刷り込むため
       明示的に \r\n を書いておく。 */
    printf("type keys (q to quit). nothing should echo to the screen:\r\n");
    fflush(stdout);   /* プロンプトは改行で終わるが、 後でループ中も明示 flush するので統一 */

    for (;;) {
        char c;
        /* VMIN=1 ブロッキング read = キーを押すまでここで待つ。
           60 fps メインループにしないので、 1 キー = 1 表示の同期的な対話で OK。 */
        ssize_t n = read(STDIN_FILENO, &c, 1);
        if (n != 1) break;
        if (c == 'q') break;
        /* キーを画面に echo するのは 「自分の write」 だけ。
           OS は ECHO OFF なので何も書かない。
           ここの printf が「自分の出力だけ見える」 ことを実演する役。 */
        printf("  got: '%c' (0x%02X)\r\n", c, (unsigned char)c);
        /* ループ内 flush の理由:
           stdout は行バッファだが、 \r\n を含むので普通は自動 flush される。
           ただし保険として明示。
           60 fps メインループ等ではここの fflush が重要になることが多い。 */
        fflush(stdout);
    }

    tcsetattr(STDIN_FILENO, TCSAFLUSH, &orig);
    printf("bye\n");
    return 0;
}
