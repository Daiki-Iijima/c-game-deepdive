/*
 * 04_tools/bug_hunting/buggy.c — 第 11 章 (gdb + valgrind 実戦)
 *
 * 章本文で再訪する 4 種のバグを 1 ファイルに集めた小さな実験場。
 * ゲームではない。 学習用に最小限。
 *   - --bug=double-free   : 二重 free
 *   - --bug=use-after-free: 解放後参照
 *   - --bug=oob           : 配列外書き
 *   - --bug=uninit        : 未初期化変数で分岐
 */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void bug_double_free(void) {
    int *p = malloc(sizeof(int));
    *p = 42;
    free(p);
    free(p);   /* ← 2 度目: glibc が叫ぶ or valgrind が叫ぶ */
}
static void bug_use_after_free(void) {
    int *p = malloc(sizeof(int));
    *p = 42;
    free(p);
    int x = *p;   /* ← 解放後 read */
    printf("read after free: %d\n", x);
}
static void bug_oob(void) {
    int arr[4] = {1, 2, 3, 4};
    /* arr[7] = 99; ← stack OOB write、 ASan が叫ぶ。 valgrind は弱い */
    arr[7] = 99;
    printf("arr[0..3] = %d %d %d %d\n", arr[0], arr[1], arr[2], arr[3]);
}
static void bug_uninit(void) {
    int x;             /* ← 初期化忘れ */
    if (x > 0) {       /* valgrind が "Conditional jump on uninitialised" */
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
