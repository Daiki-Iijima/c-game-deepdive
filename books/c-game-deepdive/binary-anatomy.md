---
title: "第12章 — バイナリを解剖する: ELF / objdump / asm / perf と卒業課題"
---

:::message
本連載で書くコード一式は **[GitHub: Daiki-Iijima/c-game-deepdive](https://github.com/Daiki-Iijima/c-game-deepdive)** にあります。本文中で `01_snake/step1_termios/main.c` のように参照する path はすべてそのリポ内のファイルです。
:::


![demo placeholder](https://placehold.co/800x300?text=readelf+%2B+objdump+%2B+perf)

## つかみ

最終章です。 第 0 章で 「`hello.c` → `hello.i` → `hello.s` → `hello.o` → `hello`」 の 4 段変換を見ました。 いま私たちは、 連載を通して書いた **数千行の C コード** を 1 個の ELF バイナリに焼いた状態にいます。

本章は、 そのバイナリを **3 つの解像度** で解剖します。

1. **ELF のセクション構造** — `readelf`
2. **関数の機械語** — `objdump -d`
3. **動かしているときの中身** — `perf stat / record`

連載中に書いた構造 (関数ポインタ・ASan の毒チェック・heap への触り方) が、 機械の側からどう見えるかを 「目で確かめる」 のがゴール。

## 今日の機構: 解剖用の小さな素材

`04_tools/binary_anatomy/sample.c` は連載のゲームではなく、 解剖に都合のよい 3 機能を 1 ファイルにまとめた素材です:

```c
typedef int (*BinOp)(int, int);
static const BinOp OPS[4] = { op_add, op_sub, op_mul, op_xor };

__attribute__((noinline))
int dispatch(int idx, int a, int b) {
    return OPS[idx & 3](a, b);   /* ← 関数ポインタ間接呼び出し */
}

__attribute__((noinline))
uint64_t hot_loop(const int *arr, size_t n) {
    uint64_t s = 0;
    for (size_t i = 0; i < n; i++) s += (uint64_t)arr[i];
    return s;
}
```

`__attribute__((noinline))` を付けたのは、 -O2 で **インライン展開されて消えてしまわない** ようにするため。 解剖したい関数を意図的に残します。

## 覗く 1: readelf でセクション構造

```sh
cd 04_tools/binary_anatomy
make
make readelf
```

代表的なセクションが並びます。

```
.text          ← 実行可能な機械語 (main, dispatch, hot_loop, ...)
.rodata        ← 読み出し専用 (OPS テーブルや printf の書式文字列)
.data          ← 初期値ありの static 変数
.bss           ← ゼロ初期化の static (= 第 2 章の `char map[ROWS][COLS]` の正体)
.dynamic       ← 動的リンカ用の情報
.dynsym        ← 動的シンボル表
```

第 2 章で 「`map[][]` は BSS にいる」 と書いたのが、 ここの `.bss` 行に対応します。 `OPS[4]` (関数ポインタテーブル) は `.rodata` に居ます。 これらは **OS が ELF を mmap した瞬間に各セクションが配置される** わけではなく、 **複数の section が 1 つの segment にまとめられて mmap される** (ELF の二重表現)。 第 10 章の save file で見た 「メモリ表現」 と 「ファイル表現」 の話と同じ構造です。

## 覗く 2: objdump で `dispatch` を逆アセンブル

```sh
make disas
```

`-O2` 最適化版で `dispatch` を見ると、 `OPS[idx & 3](...)` 部分がこんな asm に変身します (x86_64):

```
000000000040....:
   ...
   and    $0x3,%edi              ; idx & 3
   mov    $0x4040....,%rax       ; OPS テーブル先頭
   call   *(%rax,%rdi,8)         ; ← 間接 call
   ret
```

`call *...` の `*` が **間接呼び出し**。 第 5 章で書いた `ROT_FN[idx & 3](g_cur_rot)` も、 同じく `call *...` に化けています。 関数ポインタとは **テーブル先頭 + idx*8 の番地に飛ぶ命令** にすぎなかった、 というのが asm レベルで見えます。

`hot_loop` は単純なので、 `-O2` で **SIMD 化 (SSE / AVX)** されることもあります。 `vpmovsxdq` のような命令が並んだら、 それが SIMD。 連載で出てきた `for (i = 0; i < n; i++) s += arr[i]` が一回のベクトル命令で 4-8 要素まとめて処理されています。

## 覗く 3: perf stat で実行コストを測る

```sh
make perf
# = perf stat -e instructions,cycles,L1-dcache-load-misses ./sample_O2 1000000
```

実 Linux マシン (or Codespaces) ならこんな出力:

```
   123,456,789      instructions
    50,000,000      cycles
     1,234,567      L1-dcache-load-misses
   2.45 IPC, 1.0 % L1 miss
```

第 0 章の注記の通り、 Docker Desktop 経由だと `<not supported>` が混ざることがあります。 その場合は GitHub Codespaces や直 Linux で。

`hot_loop_stride(arr, n, 16)` (= 64 byte ストライド = 1 cache line ずつ別) と `hot_loop(arr, n)` を比べれば、 `L1-dcache-load-misses` の桁が変わるのが見えます。 第 3 章で 「配列はキャッシュ局所性が強く、 連結リストは弱い」 と書いた話の **数値的裏付け** です。

## 第 6 章 ASan の毒チェックを覗く

第 6 章で投入した `-fsanitize=address` でビルドした `tetris_step3` を `objdump -d` すると、 メモリアクセスの前後に **`__asan_load1` / `__asan_load4` / `__asan_store1`** の呼び出しが大量に挟まっているのが見えます。

```
   ...
   call   __asan_load4@plt
   mov    %eax,%edi
   call   __asan_report_load4@plt
   ...
```

「**コンパイル時計装** とは要するにメモリアクセスごとに小さな関数呼び出しを差し込むこと」 という抽象的な説明が、 ここで具体物として手に入ります。

## メンタルモデル更新: コンパイル → 実行 の通し図

```
hello.c          [Cソース]
   |   gcc -E      preprocessor
   v
hello.i          [展開済み C]
   |   gcc -S      compiler
   v
hello.s          [asm]
   |   gcc -c      assembler
   v
hello.o          [ELF .o (REL)]
   |   ld          linker
   v
hello            [ELF (DYN/PIE)]   ←── readelf / objdump はここを読む
   |   exec()      kernel: ELF parser
   v
[mmap segments] → [.text RX / .data RW / .rodata R / heap RW (after first malloc)]
   |   process running
   v
[CPU が .text を実行]
   |   perf stat / perf record
   v
[instructions / cycles / cache misses] ← ここで初めて速度の話になる
```

この流れを 1 周まわせるエンジニアは、 多くのトラブルを 「どの段で何が起きたか」 で切り分けられるようになります。

## 卒業課題 3 つ

連載の終わりに、 全 12 章の総力戦を 3 段階の難易度で。

### 卒業課題 A (Easy)
**第 5 章の Tetris (`02_tetris/step2_bitwise/`) の `try_place` を `-O0` と `-O2` でビルドし、 `objdump -d --disassemble=try_place` を比較する**。 -O2 で何が消えて何が残ったか、 簡潔な diff レポートを書く。 教材 PR のレビュー目線で。

### 卒業課題 B (Medium)
**第 9 章の Roguelike fork+pipe を `select(2)` ベースのノンブロッキング設計に書き直し**、 `perf stat -e cycles,context-switches` で 旧版と新版を比較する。 「I/O 待ちで何 cycle 浪費していたか」 を数字で示す。

### 卒業課題 C (Hard)
**第 4 章の Tetris のキューを heap (連結リスト) と arena (1 個の `malloc` + index) の 2 通りで実装** し、 `valgrind --tool=callgrind` で関数ごとのコストを比較。 `kcachegrind` で flame graph を出す。 「**malloc 呼び出しの回数を減らすこと** の効き目」 を数字で確認。

## 連載のまとめ

| | 章 | 学んだこと |
|---|---|---|
| メモリ | 2-3 | stack / heap / BSS、 連結リスト、 二重 free と valgrind |
| ヒープ | 4-6 | 動的キュー、 bit/関数ポインタ、 ASan で配列外 |
| 動的サイズ | 7 | flexible array member、 大きな heap 1 ブロック |
| OS 連携 | 8-9 | sigaction、 SIGWINCH、 fork+pipe+dup2、 SIGCHLD |
| バイナリ表現 | 10-12 | endian/padding、 gdb、 ELF/asm/perf |

「C を書ける」 と 「OS と CPU が C をどう扱うか分かる」 は別の能力です。 ターミナルゲームを通して両方を獲りに行く本連載の旅は、 ここまで。 自作のゲームを `objdump` で開いて遊ぶ、 そんな読み方をされる読者が増えれば成功です。
