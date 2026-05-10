/*
 * 04_tools/binary_anatomy/sample.c
 * --------------------------------------------------------------------------
 * 第 12 章 (バイナリ解剖の素材)
 *
 * 関数ポインタ間接呼び出し / 単純ループ / 配列アクセスの 3 パターンを
 * 1 ファイルに置き、 readelf / objdump / perf の素材にする。
 *
 * 新登場の C 機能:
 *   - __attribute__((noinline)): gcc/clang 拡張。 「この関数はインライン
 *     展開しないでくれ」 とコンパイラに伝える。 -O2 で消えると逆アセンブル
 *     できなくなるので、 解剖したい関数に明示的に付ける。
 *   - strtoull: 文字列を unsigned long long に変換する (atoi/atol の上位)
 *   - <stdint.h> の固定幅型: uint64_t を使い、 環境差をなくす
 */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* BinOp 型: 「int 2 つを取って int を返す関数」 へのポインタ型。
   関数ポインタ配列 OPS[] にどの op_xxx を入れるかで挙動が決まる。 */
typedef int (*BinOp)(int, int);
static int op_add(int a, int b) { return a + b; }
static int op_sub(int a, int b) { return a - b; }
static int op_mul(int a, int b) { return a * b; }
static int op_xor(int a, int b) { return a ^ b; }
static const BinOp OPS[4] = { op_add, op_sub, op_mul, op_xor };

/* 関数ポインタ呼び出し: objdump で `call *...(.., %rax)` のような間接 call を期待。
   __attribute__((noinline)): gcc/clang の拡張。 -O2 でこの関数が呼び出し元に
   インライン展開されないよう強制する (= 関数として残す)。 */
__attribute__((noinline))
int dispatch(int idx, int a, int b) {
    /* idx & 3 で範囲を 0..3 に制限してから配列を引く。
       OPS[k] は関数ポインタ。 続く (a, b) で実際に関数として呼ぶ。 */
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
