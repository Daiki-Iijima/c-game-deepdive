/*
 * 04_tools/bug_hunting/buggy.c
 * --------------------------------------------------------------------------
 * 第 11 章 (gdb + valgrind 実戦) の素材。
 *
 * 章本文で再訪する 4 種のバグを 1 ファイルに集めた小さな実験場。
 * ゲームではない。 学習用に最小限。
 *
 *   --bug=double-free   : 二重 free (heap-corruption)
 *   --bug=use-after-free: 解放後参照 (UAF)
 *   --bug=oob           : 配列外書き込み (stack buffer overflow)
 *   --bug=uninit        : 未初期化変数で分岐
 *
 * ねらい:
 *   - 各ツール (gdb / valgrind / ASan) が **同じバグでも違う形で叫ぶ** ことを実演
 *   - 「症状 → 原因」 を遡る訓練を最短コードで
 */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ---- バグ 1: 二重 free ---------------------------------------------- */

/* 同じアドレスに対して free を 2 回呼ぶ。 glibc malloc は最近のバージョンで
   検出して abort() するが、 古い実装や運次第では沈黙 → 後で別の場所で爆発する
   ことがある。 valgrind なら確実に Invalid free を報告する。 */
static void bug_double_free(void) {
    int *p = malloc(sizeof(int));
    *p = 42;
    free(p);
    free(p);   /* ← 2 度目: glibc が叫ぶ or valgrind が叫ぶ */
}

/* ---- バグ 2: 解放後参照 (use after free) ---------------------------- */

/* free 直後のアドレスは多くの場合「まだ読める」 (= 値が残っている) が、 そのうち
   別の malloc に再利用される → 突然データが化ける。 ハイゼンバグの典型。 */
static void bug_use_after_free(void) {
    int *p = malloc(sizeof(int));
    *p = 42;
    free(p);
    int x = *p;   /* ← 解放後 read。 valgrind の Invalid read of size 4 */
    printf("read after free: %d\n", x);
}

/* ---- バグ 3: 配列外書き込み (stack OOB) ----------------------------- */

/* 4 要素しかない配列に index 7 で書く = stack 上の隣接領域を破壊。
   ASan が stack-buffer-overflow を即時報告する。 valgrind の memcheck は
   stack には弱いので沈黙することもある。 */
static void bug_oob(void) {
    int arr[4] = {1, 2, 3, 4};
    arr[7] = 99;
    printf("arr[0..3] = %d %d %d %d\n", arr[0], arr[1], arr[2], arr[3]);
}

/* ---- バグ 4: 未初期化変数で分岐 ------------------------------------- */

/* 自動変数 (stack 上) は明示的に初期化しないとゴミ値。
   その値で if 分岐 → 「コードが正しいのに挙動が毎回違う」 という最悪のバグになる。
   valgrind --track-origins=yes が威力を発揮する場面。 */
static void bug_uninit(void) {
    int x;             /* ← 初期化忘れ。 stack 上のゴミ値が入る */
    if (x > 0) {       /* valgrind: "Conditional jump on uninitialised" */
        printf("positive\n");
    } else {
        printf("non-positive\n");
    }
}

static void usage(void) {
    fprintf(stderr,
            "usage: buggy --bug={double-free|use-after-free|oob|uninit}\n");
}

int main(int argc, char **argv) {
    if (argc != 2) { usage(); return 1; }
    if      (strcmp(argv[1], "--bug=double-free")    == 0) bug_double_free();
    else if (strcmp(argv[1], "--bug=use-after-free") == 0) bug_use_after_free();
    else if (strcmp(argv[1], "--bug=oob")            == 0) bug_oob();
    else if (strcmp(argv[1], "--bug=uninit")         == 0) bug_uninit();
    else { usage(); return 1; }
    return 0;
}
