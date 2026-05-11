---
title: "第0章 — 環境構築とゲームループ: なぜ Docker amd64 を踏むのか"
---

:::message
本連載で書くコード一式は **[GitHub: Daiki-Iijima/c-game-deepdive](https://github.com/Daiki-Iijima/c-game-deepdive)** にあります。本文中で `01_snake/step1_termios/s5_full/main.c` のように参照する path はすべてそのリポ内のファイルです。
:::


## はじめに
「C のゲームを書くだけなら gcc 一発でいいじゃん」と思うかもしれません。

それでもこの連載は **Docker amd64 強制** から始めます。
理由はひとつ:

**第 12 章で `objdump -d ./game` を打ったとき、読者全員の画面に同じアセンブリが出てほしい**

Apple Silicon の M1/M2 で `gcc` を素で使うと ARM64 (aarch64) のアセンブリが出ます。
x86_64 とはレジスタ名も呼び出し規約も違うので、本文の説明と画面が一致しなくなります。

連載の最終章で「あれ、私の `mov` どこ?」と立ち止まる読者を出さないために、**初日に環境を一本化**します。


## 本章のテーマ: 開発環境

- Linux Docker (`ubuntu:24.04`) を `--platform=linux/amd64` で固定
- gcc / clang / make / gdb / valgrind / strace / ltrace / binutils / elfutils / perf を一緒に入れる
- ホストの作業フォルダを `/workspace` にバインドマウント
- `SYS_PTRACE` と `seccomp:unconfined` を許可 (gdb / strace / perf に必要)

`docker/Dockerfile` と `docker/compose.yml` を覗いてください。
コメントを追えば 1 行ずつ意味が分かります。


## 実装する
```sh
git clone https://github.com/Daiki-Iijima/c-game-deepdive
cd c-game-deepdive
docker compose -f docker/compose.yml run --rm dev
```

:::message
**ホストの nvim/エディタを使い続けたい人へ**: リポジトリ直下に `./dx` というラッパスクリプトを置いてあります。 Mac の nvim から `./dx make -C 01_snake/step1_termios/s5_full` のように打つと、 中身は **Docker コンテナ内** で `make` が走ります。 章本文は `docker compose run --rm dev` で中に入って手で叩く前提で書いてありますが、 慣れてきたら `./dx` 経由の方が往復が速いです。 詳しくは [README](https://github.com/Daiki-Iijima/c-game-deepdive#ハイブリッド開発-mac-の-nvim--docker-でビルド) を。
:::

:::details `docker compose run --rm dev` の中身
- `compose -f docker/compose.yml`: 使う compose ファイルを明示。 デフォルト `docker-compose.yml` ではなく `docker/compose.yml` を指している。
- `run`: サービス (ここでは `dev`) を **その場限りで** 起動するサブコマンド。 長期常駐させる `up` とは違う。
- `--rm`: コンテナを抜けた瞬間に自動削除。 開発用途では「試したら綺麗に消える」を確保する定番フラグ。
- `dev`: `compose.yml` で定義したサービス名。 そこに書いた `image` / `volumes` / `command` が読まれてコンテナ起動。
- 結果: ホストの作業ディレクトリが `/workspace` にマウントされた状態で `bash` プロンプトに入る。 抜ければコンテナは消えるが、 ホスト側のファイルは残る。
:::

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

:::details gcc のフラグの意味
- `-Wall`: 一般的な警告を全部有効にする。 「all」と書いてあるが実際は **代表的なものだけ**。
- `-Wextra`: `-Wall` に含まれない追加の警告 (符号の比較、 未使用引数など)。
- `-O0`: 最適化レベル 0 = 最適化しない。 デバッガで変数名と行番号がそのまま見えるようにするため、 学習用のデフォルトはこれ。
- `-g`: デバッグ情報 (DWARF) を実行ファイルに含める。 `gdb` が変数名や行番号を引けるのはこの情報があるから。
- `-o hello`: 出力ファイル名を指定。 省略すると `a.out` になる。
:::

:::details `file` コマンドは何を見ている?
- `file` はファイルの先頭数バイト (= マジックナンバー) と既知のヘッダ構造を比較して、 ファイルの種類を当てる小さなツール。 拡張子は見ない。
- ELF ファイルの場合は最初の 4 バイトが `\x7f E L F` (= マジック)。 続くバイトでビット幅 (32/64)、 エンディアン (LSB/MSB)、 ABI、 バージョンが分かる。
- 出力例 `ELF 64-bit LSB pie executable, x86-64`: ELF 形式の 64bit リトルエンディアン、 PIE (位置独立) 実行ファイル、 ターゲットは x86_64 アーキテクチャ。
- ELF の中身は第 12 章で `readelf` を使って同じ情報をもっと詳細に覗きます。
:::

`file` の出力で **`ELF 64-bit LSB pie executable, x86-64`** と表示されたら勝ちです。

:::details ELF / DYN / PIE / REL / LSB ってなに?
本連載で頻出する用語をまとめておきます。 全部 ELF (実行ファイルの形式) の周りの単語です。

- **ELF (Executable and Linkable Format)**: Linux / FreeBSD / Solaris などで使われる **実行ファイル・オブジェクトファイル・共有ライブラリの統一フォーマット**。 ファイルの先頭 4 byte に `\x7f` `E` `L` `F` というマジック番号がある。 macOS は ELF ではなく Mach-O、 Windows は PE という別形式を使う。
- **64-bit / 32-bit**: アドレスやレジスタの幅。 本連載は x86_64 を前提にしているので 64-bit。
- **LSB (Least Significant Byte first) / MSB**: バイト順 (= エンディアン)。 LSB = リトルエンディアン (x86 / ARM の標準)、 MSB = ビッグエンディアン (古い PowerPC / SPARC)。 第 10 章のセーブファイル章でしっかり扱う。
- **`Type: REL` (Relocatable)**: `gcc -c` で出る `.o` ファイルがこれ。 機械語は入っているが、 シンボルが置かれる絶対アドレスが未確定。 リンクして初めて実行可能になる。
- **`Type: EXEC` (Executable, 古い形式)**: 昔の実行ファイル。 .text / .data などのアドレスがコンパイル時に固定。 攻撃者にアドレスを予測されるためセキュリティ的に弱い。
- **`Type: DYN` (Dynamic)**: 共有ライブラリ (`.so`) と **PIE 実行ファイル** の両方がこれ。 「自分自身の実行ファイルでも、 動的にロードされる」 設計。 アドレスは起動時にカーネルがランダム化 (= ASLR) して埋める。
- **PIE (Position Independent Executable)**: 「アドレスが固定されてない実行ファイル」。 全コードが相対アドレス (RIP-relative on x86_64) で動くようにコンパイルされており、 起動時にどこにロードされてもよい。 最近の Linux ディストリは `-fpie -pie` がデフォルト。 `Type: DYN` と表示される。
- **ABI (Application Binary Interface)**: バイナリレベルの取り決め。 関数引数をどのレジスタで渡すか (System V AMD64 ABI: rdi/rsi/rdx/rcx/r8/r9)、 構造体のアラインメントはどうか、 などをまとめたもの。 「同じ ELF でも ABI が違うマシンで動かすと壊れる」 のがクロスプラットフォーム互換性の正体。
- **Magic / マジック番号**: ファイル形式を識別する固定パターン。 ELF は `\x7f ELF`、 Mach-O は `\xCF\xFA\xED\xFE`、 ZIP は `PK\x03\x04`、 などなど。 `file` コマンドが見ているのはこれ。
:::

ARM64 で出ていたら Docker の platform 指定が効いていません。
`docker compose` 経由で起動しているか確認してください。


## 観察する: コンパイルパイプライン

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

:::details `gcc -E / -S / -c` をもう少し詳しく
- `gcc -E hello.c -o hello.i`: **プリプロセス段** で止める。 `#include` の展開、 `#define` のマクロ展開、 `#if` の評価が済んだ生 C ソース (拡張子 `.i`) が出る。 ヘッダがどこまで膨らむかを実感したいときはここで止めて行数を数えると良い。
- `gcc -S hello.c -o hello.s`: **コンパイル段** で止める。 内部でプリプロセスも走り、 結果は **アセンブリ言語** (`.s` テキスト)。 第 12 章で読みます。
- `gcc -c hello.c -o hello.o`: **アセンブル段** で止める。 出力は **ELF 再配置可能オブジェクト** (`.o`)。 機械語が入っているが、 シンボルのアドレスが未確定なので単独では実行できない。
- `gcc hello.o -o hello`: **リンク段** だけを実行。 内部で `ld` を呼んで他のオブジェクト (libc など) と結合し、 実行可能 ELF を作る。
:::

:::details `readelf -h` の出力ポイント
- `readelf` は ELF ファイルのメタ情報を読み出す GNU binutils のコマンド。 `objdump` と並ぶ ELF 解剖の二大武器。
- `-h` (= `--file-header`) は ELF ヘッダ (先頭 64 byte 程度) を人間に読める形で表示する。
- 重要フィールド:
  - `Class: ELF64` — 64bit
  - `Type: REL` (`.o` 段) / `Type: DYN` (PIE 実行ファイル) / `Type: EXEC` (古い non-PIE 実行ファイル)
  - `Machine: Advanced Micro Devices X86-64` — ターゲット ISA
  - `Entry point address` — プロセス起動時に最初に飛ぶ命令アドレス (実行ファイルのみ)
- セクションごとの詳細は `-S` (`--section-headers`) で見れます。 第 12 章でやります。
:::

`hello.s` を開くと `main:` ラベル直下に `lea`, `call puts`, `xor eax,eax`, `ret` が並んでいます。
第 12 章でここに戻ってきます。

`hello.o` は実行できません (`./hello.o` → `cannot execute binary file`)。
再配置情報がまだ「どこに置かれるか分からない」状態だからです。
`readelf -h hello.o` の `Type: REL` がそれを物語っています。
リンク後は `Type: DYN` (PIE) になります。


## メンタルモデルを整理する

```
[ソース] -E→ [展開済み] -S→ [asm] -c→ [.o] -ld→ [実行ファイル]
            preprocess    compile     assemble    link
```

「コンパイラ」と一語で呼んでいたものは、**4 つの独立した変換器** の連なりです。
それぞれの段で **何の情報が増えて何の情報が捨てられるか** を意識すると、第 12 章の ELF 解剖が「最後に残ったもの」に見えてきます。


## 演習

- **Easy**: `hello.c` の `puts` を `printf("%d\n", 42)` に変えて `hello.s` を再生成し、差分を眺める。

- **Med**: `gcc -O2` でコンパイルすると `hello.s` の行数がどう変わるか観察。
 `main` の中身は何行になりましたか?
- **Hard**: `hello.o` を `objcopy --redefine-sym main=main2 hello.o hello2.o` で書き換え、リンクが壊れることを確認。
`nm hello.o` と `nm hello2.o` の差を読む。


## 次章では
第 1 章では **キーボードを CUI ゲーム用に乗っ取ります**。
Enter を押さなくても 1 文字届くようにする「raw mode」の設定 — `termios` 構造体のフラグを 1 ビットずつ落としていきます。
読み終えるころには、`ICANON` / `ECHO` / `ISIG` / `OPOST` が **どのアプリの不便さの正体だったか** が分かるようになります。

