/*
 * 04_tools/binary_anatomy/sample.c — 第 12 章 (バイナリ解剖の素材)
 *
 * 関数ポインタ間接呼び出し / 単純ループ / 配列アクセスの 3 パターンを
 * 1 ファイルに置き、 readelf / objdump / perf の素材にする。
 */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef int (*BinOp)(int, int);
static int op_add(int a, int b) { return a + b; }
static int op_sub(int a, int b) { return a - b; }
static int op_mul(int a, int b) { return a * b; }
static int op_xor(int a, int b) { return a ^ b; }
static const BinOp OPS[4] = { op_add, op_sub, op_mul, op_xor };

/* 関数ポインタ呼び出し: objdump で `call *...(.., %rax)` のような間接 call を期待 */
__attribute__((noinline))
int dispatch(int idx, int a, int b) {
    return OPS[idx & 3](a, b);
}

/* 単純な heat ループ: perf stat で IPC や cache miss を見る素材 */
__attribute__((noinline))
uint64_t hot_loop(const int *arr, size_t n) {
    uint64_t s = 0;
    for (size_t i = 0; i < n; i++) s += (uint64_t)arr[i];
    return s;
}

/* ストライド変えで cache miss 影響を出す版 */
__attribute__((noinline))
uint64_t hot_loop_stride(const int *arr, size_t n, size_t stride) {
    uint64_t s = 0;
    for (size_t i = 0; i < n; i += stride) s += (uint64_t)arr[i];
    return s;
}

int main(int argc, char **argv) {
    if (argc != 2) {
        fprintf(stderr, "usage: sample <count>\n");
        return 1;
    }
    size_t n = (size_t)strtoull(argv[1], NULL, 10);
    int *arr = malloc(n * sizeof(int));
    if (!arr) { perror("malloc"); return 1; }
    for (size_t i = 0; i < n; i++) arr[i] = (int)(i & 0xFF);

    /* 関数ポインタ経由のディスパッチを n 回 */
    int64_t acc = 0;
    for (size_t i = 0; i < n; i++) acc += dispatch((int)i, (int)i, (int)(i + 1));

    /* hot loop 連続版 */
    uint64_t s1 = hot_loop(arr, n);
    /* hot loop ストライド版 (stride = 16 で cache miss 増) */
    uint64_t s2 = hot_loop_stride(arr, n, 16);

    printf("acc=%lld s1=%llu s2=%llu\n",
           (long long)acc, (unsigned long long)s1, (unsigned long long)s2);

    free(arr);
    return 0;
}
