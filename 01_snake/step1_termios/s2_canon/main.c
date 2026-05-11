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
    /* なぜ「退避」?
       後で必ず元に戻すため。
       戻し忘れるとシェルに帰ったあと端末が ICANON OFF のままになって
       入力がおかしくなる事故になる。 */
    struct termios orig;
    if (tcgetattr(STDIN_FILENO, &orig) == -1) {
        /* perror は直前の errno を strerror() で文字列化して stderr に出す。
           失敗時は早期 return で安全側に倒す。 */
        perror("tcgetattr"); return 1;
    }

    /* なぜ「コピーを編集」?
       orig はそのまま保管しておきたい (後で復元に使う)。
       破壊的編集は raw 側にだけ施す。
       struct の代入は memcpy 相当なので OK。 */
    struct termios raw = orig;
    /* なぜ ICANON だけ落とす?
       ICANON が ON だとカーネル line discipline が「行が完成するまで」 入力を
       バッファに溜める = scanf も read も Enter まで返ってこない。
       OFF にすれば 1 byte 来た瞬間に read が返るようになる。
       ビットマスクは ~ICANON で 1 bit だけ 0 にする慣用句。
       (tcflag_t) キャストは符号変換警告を黙らせるため。 */
    raw.c_lflag &= (tcflag_t)~ICANON;
    /* VMIN/VTIME の意味:
       VMIN=1, VTIME=0 → 「最低 1 byte 来るまでブロック」
       Step 5 では 60 fps メインループのため VMIN=0 (即時 return) に切り替える。
       この step は「キーを押すまで待つ」 ことを示したいのでブロック側を選ぶ。 */
    raw.c_cc[VMIN]  = 1;
    raw.c_cc[VTIME] = 0;
    /* なぜ TCSAFLUSH?
       第 2 引数は適用タイミングの選択:
         TCSANOW    = 即時
         TCSADRAIN  = 出力キューが捌けてから
         TCSAFLUSH  = 出力キュー捌き + 入力キュー破棄してから ← これを選ぶ
       退避前にキューに残っていたバイトを raw mode で拾ってしまう事故を防ぐ。 */
    if (tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw) == -1) {
        perror("tcsetattr"); return 1;
    }

    printf("press a key (no Enter needed): ");
    fflush(stdout);   /* 行バッファ stdout を強制 flush しないとプロンプトが画面に出ない */

    /* read(2) を直接呼ぶ理由:
       stdio (getc/fgets) はバッファリングするので、 ICANON OFF の効果が
       見えにくくなる場合がある。
       低レベルの read を直接叩くことで、 line discipline → read という
       一対一の対応が strace で観察しやすくなる。 */
    char c;
    ssize_t n = read(STDIN_FILENO, &c, 1);

    /* 復元。
       この行に辿り着けば端末は元通り。
       ただしこの step は atexit も signal handler も無いので、
       ここに来る前に exit や Ctrl-C で死ぬと ICANON OFF のまま放置される。
       Step 5 でこれを塞ぐ。 */
    tcsetattr(STDIN_FILENO, TCSAFLUSH, &orig);

    if (n != 1) {
        fprintf(stderr, "\nread failed\n");
        return 1;
    }
    printf("\nyou pressed: '%c' (0x%02X)\n", c, (unsigned char)c);
    return 0;
}
