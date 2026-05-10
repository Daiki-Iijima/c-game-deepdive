---
title: "第0章 — 環境構築とゲームループ: なぜ Docker amd64 を踏むのか"
---

## つかみ

「C のゲームを書くだけなら gcc 一発でいいじゃん」と思うかもしれません。
それでもこの連載は **Docker amd64 強制** から始めます。理由はひとつ:

**第 12 章で `objdump -d ./game` を打ったとき、読者全員の画面に同じアセンブリが出てほしい**

Apple Silicon の M1/M2 で `gcc` を素で使うと ARM64 (aarch64) のアセンブリが出ます。x86_64 とはレジスタ名も呼び出し規約も違うので、本文の説明と画面が一致しなくなります。
連載の最終章で「あれ、私の `mov` どこ?」と立ち止まる読者を出さないために、**初日に環境を一本化**します。

## 今日の機構: 開発環境

- Linux Docker (`ubuntu:24.04`) を `--platform=linux/amd64` で固定
- gcc / clang / make / gdb / valgrind / strace / ltrace / binutils / elfutils / perf を一緒に入れる
- ホストの作業フォルダを `/workspace` にバインドマウント
- `SYS_PTRACE` と `seccomp:unconfined` を許可 (gdb / strace / perf に必要)

`docker/Dockerfile` と `docker/compose.yml` を覗いてください。コメントを追えば 1 行ずつ意味が分かります。

## 作る

```sh
git clone https://github.com/<you>/c-game-deepdive
cd c-game-deepdive
docker compose -f docker/compose.yml run --rm dev
```

コンテナの中で:

```sh
gcc --version
gdb --version
valgrind --version
readelf --version
```

全部通ったら準備完了です。

:::message
**`perf` だけは要注意**: 第 12 章で扱う `perf` は、コンテナの中から **ホスト Linux カーネル** を直接叩くツールです。Docker Desktop (mac/Windows) 経由だと、ホストが Linux ではなく Docker Desktop の VM kernel になるため、`perf stat` のいくつかの計測項目が `<not supported>` で返ってくることがあります。
**回避策**: 第 12 章は (a) 実 Linux マシンで直接 `apt install` する、または (b) GitHub Codespaces (実 Linux ホスト) で起動する、のいずれかが推奨です。第 11 章までの `gdb` / `valgrind` / `strace` は Docker Desktop でも完全動作します。
:::

### Hello World で動作確認

```c
// 00_intro/hello.c
#include <stdio.h>
int main(void) { puts("hello, deepdive"); return 0; }
```

```sh
cd 00_intro
gcc -Wall -Wextra -O0 -g hello.c -o hello
./hello
file ./hello
```

`file` の出力で **`ELF 64-bit LSB pie executable, x86-64`** と表示されたら勝ちです。
ARM64 で出ていたら Docker の platform 指定が効いていません。`docker compose` 経由で起動しているか確認してください。

## 覗く: コンパイルパイプライン

普段「コンパイルする」と一言で済ませている操作は、4 つに分解できます。

```
hello.c
   │  プリプロセス (gcc -E)
   ▼
hello.i   ← マクロ展開 / include 展開済み
   │  コンパイル (gcc -S)
   ▼
hello.s   ← x86_64 アセンブリ
   │  アセンブル (gcc -c)
   ▼
hello.o   ← ELF 再配置可能オブジェクト
   │  リンク (gcc)
   ▼
hello     ← ELF 実行可能ファイル
```

実際に止めて見てみましょう。

```sh
gcc -E hello.c -o hello.i
gcc -S hello.c -o hello.s
gcc -c hello.c -o hello.o
gcc hello.o -o hello
```

`hello.s` を開くと `main:` ラベル直下に `lea`, `call puts`, `xor eax,eax`, `ret` が並んでいます。第 12 章でここに戻ってきます。
`hello.o` は実行できません (`./hello.o` → `cannot execute binary file`)。再配置情報がまだ「どこに置かれるか分からない」状態だからです。`readelf -h hello.o` の `Type: REL` がそれを物語っています。リンク後は `Type: DYN` (PIE) になります。

## メンタルモデル更新

```
[ソース] -E→ [展開済み] -S→ [asm] -c→ [.o] -ld→ [実行ファイル]
            preprocess    compile     assemble    link
```

「コンパイラ」と一語で呼んでいたものは、**4 つの独立した変換器** の連なりです。それぞれの段で **何の情報が増えて何の情報が捨てられるか** を意識すると、第 12 章の ELF 解剖が「最後に残ったもの」に見えてきます。

## 演習

- **Easy**: `hello.c` の `puts` を `printf("%d\n", 42)` に変えて `hello.s` を再生成し、差分を眺める。
- **Med**: `gcc -O2` でコンパイルすると `hello.s` の行数がどう変わるか観察。 `main` の中身は何行になりましたか?
- **Hard**: `hello.o` を `objcopy --redefine-sym main=main2 hello.o hello2.o` で書き換え、リンクが壊れることを確認。`nm hello.o` と `nm hello2.o` の差を読む。

## 次回予告

第 1 章では **キーボードを CUI ゲーム用に乗っ取ります**。Enter を押さなくても 1 文字届くようにする「raw mode」の設定 — `termios` 構造体のフラグを 1 ビットずつ落としていきます。読み終えるころには、`ICANON` / `ECHO` / `ISIG` / `OPOST` が **どのアプリの不便さの正体だったか** が分かるようになります。
