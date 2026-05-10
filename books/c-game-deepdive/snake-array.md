---
title: "第2章 — 蛇の体を表現する: 配列・struct・padding を覗く"
---

:::message
本連載で書くコード一式は **[GitHub: Daiki-Iijima/c-game-deepdive](https://github.com/Daiki-Iijima/c-game-deepdive)** にあります。本文中で `01_snake/step1_termios/main.c` のように参照する path はすべてそのリポ内のファイルです。
:::


![demo placeholder](https://placehold.co/800x300?text=snake+v1+%28array%29+demo+%28asciinema%29)

## はじめに
第 1 章では `@` を 1 つ動かしました。本章はこれを **蛇の体** に育てます。
体を持つには「いま体がどこにあるか」を覚えておく場所が必要です。
最初に思いつく素朴な答えは **配列** です。本章はその直感を最後まで実装し、

- 配列で持つと何が嬉しくて何が痛いのか
- `struct` を 1 個追加すると **メモリ上で何 byte 増えるのか**
- 同じ struct なのに **書く順番** で size が変わるのはなぜか

を、自分の蛇のメモリレイアウトを `--inspect` で覗きながら掴みます。

## 本章のテーマ: 配列で蛇を持つ

```c
typedef enum { DIR_UP, DIR_DOWN, DIR_LEFT, DIR_RIGHT } Direction;

typedef struct {
    uint8_t r;  /* 行 */
    uint8_t c;  /* 列 */
} Cell;

typedef struct {
    Cell      body[256];  /* body[0] = 頭 */
    int       len;
    Direction dir;
    uint8_t   alive;
    uint64_t  score;
} Snake;
```

ゲームの 1 tick で頭を進め、配列を **後ろから前へ** ずらせば「動く蛇」になります。

```c
static void snake_step(Snake *s) {
    Cell h = next_head(s);
    if (snake_will_collide(s, h)) { s->alive = 0; return; }
    for (int i = s->len - 1; i > 0; i--) s->body[i] = s->body[i - 1];
    s->body[0] = h;
}
```

`s->body[i] = s->body[i - 1]` は `Cell` 構造体の **値コピー** です。中に隠れたポインタが無いからこれで完結します。 第 3 章で連結リストに置き換えると、この 1 行が `node->next` の付け替えに化けます。

## 実装する
第 1 章で書いた termios 一式を `lib/tty.c` に移しました。第 2 章からはそれを `#include "tty.h"` で再利用します。本章のソース全文は `01_snake/step2_array/main.c` です。

```sh
cd 01_snake/step2_array
make
./snake_step2          # 矢印キーで蛇が動く / q で終了
```

蛇の頭が壁にぶつかると `GAME OVER` が出るところまで実装してあります。
壁を貫通させる挑戦は演習で。

:::message
**MAX_LEN について**: コードでは `MAX_LEN = 256` 固定にしてあります。20×60 のマップで全マスを蛇が埋め尽くすと最大 1200 マス必要なので、長く伸ばす遊び方を試すなら定数を増やしてください。配列の弱点 — **想定される最大長を最初に決めなければならない** — がここで顔を出します。次章で `malloc` を使うと、この決め打ちから解放されます。
:::

:::message
**`score` フィールドが 0 のまま?** いまの実装ではまだ食料を出していないので加点機構がありません。次章で蛇に食料を食べさせます。本章は **データの形** に集中します。
:::

## 観察する: `sizeof` と `offsetof` で蛇のメモリを実測

`make` した同じバイナリには **インスペクトモード** を仕込んであります。

```sh
./snake_step2 --inspect
```

筆者の Linux x86_64 環境ではこう出ます。

```
sizeof(Cell)        = 2
sizeof(Snake)       = 536
offsetof(body)      = 0
offsetof(len)       = 512
offsetof(dir)       = 516
offsetof(alive)     = 520
offsetof(score)     = 528
MAX_LEN * sizeof(Cell) = 512 (= body フィールドの素のサイズ)
&s_on_stack         = 0x7ffd...  (stack)
&map                = 0x55..00  (BSS/data)
```

ここから何が読めるか、1 行ずつ追います。

### 1. `sizeof(Cell) = 2`

`Cell` は `uint8_t` (= 1 byte) が 2 個。詰めて 2 byte。アラインメントは 1 byte で済みます。

### 2. `body` は素のままなら 512 byte

`MAX_LEN(256) * sizeof(Cell)(2) = 512`。 `offsetof(body)=0` は、`Snake` 構造体の **先頭** に `body` が来ていることを示します。
`body[i]` のアドレス計算は `(&snake) + i * 2 byte`。配列の住所計算は **掛け算 1 回** で終わります。これが配列の最大の武器です (連結リストでは 1 ノードごとに次のアドレスを「読みに行く」必要がある)。

※ `len` (`int`) と `dir` (`enum`) はそれぞれ **4 byte** を占めています。 C の `enum` は通常 `int` と同じ 4 byte 扱いです (規格上は処理系定義ですが、x86_64 + gcc の組み合わせでは安定して 4 byte)。だから `offsetof(dir)=516` の直前の `len` (offset 512) は 516 で終わり、`dir` がそのまま 516 から始まれる、という具合に padding 無しで詰まっています。

### 3. ここからが本題: 7 byte の謎の隙間

```
offsetof(alive) = 520
offsetof(score) = 528
```

`alive` は `uint8_t` (1 byte) なので、論理的には次のフィールド `score` は 521 から始まれば足ります。なのに `score` は 528 スタート。**521〜527 の 7 byte が空いている** わけです。

これが **padding** (詰め物) です。`uint64_t` は **自分のアドレスが 8 の倍数であってほしい** と CPU 側が要求します (= 8-byte alignment)。コンパイラはその要求を満たすため、`alive` の直後に 7 byte の不可視な隙間を入れます。`alive` が 1 byte でも、構造体は 1 byte 増ではなく **8 の倍数の境界まで膨らむ**。

### 4. 末尾の落とし穴

`score` (8 byte) の終端は 528 + 8 = 536。これが `sizeof(Snake) = 536` の正体です。仮に `score` を消して struct を終わらせても、`sizeof` は 8 の倍数に切り上げられます。`Snake` の **次に置かれた変数** が再び 8-byte alignment を要求するときに、配列で並べられるようにするためです。

```c
Snake arr[3];   // arr[1] のアドレスも 8-byte align であってほしい
                // → sizeof(Snake) は 8 の倍数でなければならない
```

### 5. スタックと BSS の住所

`&s_on_stack = 0x7ffd...` と `&map = 0x55..00` は何桁も離れた番地に置かれています。前者がスタック (関数ローカルの自動変数)、後者が **BSS** (静的に確保されたゼロ初期化領域) です。
両者は同じ仮想アドレス空間の中にいますが、用途別に区切られています。第 3 章で `malloc` を導入すると、ここに **heap** が増えます。

`/proc/self/maps` を覗くと、その区切りが目で見えます (Docker コンテナの中で):

```sh
cat /proc/self/maps | grep -E '\[stack\]|\[heap\]'
```

`[stack]` 行と `[heap]` 行のアドレス範囲を、上で出た `&s_on_stack` / `&map` と照らし合わせてみてください。

:::details `/proc/self/maps` の中身
Linux の擬似ファイルシステム `/proc` の中で、 各プロセスの仮想アドレス空間を 1 行 1 領域で出してくれるテキストファイル。 `self` は呼び出し元プロセス自身を指す symlink。

各行のフォーマット:

```
55c19f7e2000-55c19f7e3000 r--p 00000000 fd:00 1234567 /usr/bin/cat
↑開始 - 終了            ↑   ↑       ↑     ↑       ↑
                       perm offset  dev   inode   pathname
```

- **アドレス範囲**: その mapping の仮想アドレス開始・終了 (16進)
- **perm**: 読み (r) 書き (w) 実行 (x) と private/shared (p/s) の組合せ
  - `r-xp`: 共有ライブラリのコード segment 等
  - `rw-p`: heap や stack
  - `r--p`: ELF の `.rodata`
- **offset**: バックエンドファイルのどこから mapping したか
- **dev / inode**: デバイス番号と inode (anonymous mapping は 00:00 0)
- **pathname**: マップ元ファイル、 もしくは特殊ラベル
  - `[heap]`: brk/sbrk による heap
  - `[stack]`: メインスレッドのスタック
  - `[vdso]`: vDSO (高速 syscall 用の特殊 mapping)
  - `[anon]` or 空: anonymous (= ファイル裏付け無し、 大きな malloc などで使われる)

別プロセスは `/proc/<pid>/maps` で同様に見える (= `pmap` の出力源)。
:::

## メンタルモデルを整理する

```
低位アドレス
 ┌──────────────┐
 │ text (.text) │ ← 機械語 (main, snake_step, ...)
 ├──────────────┤
 │ data         │ ← 初期値あり static
 ├──────────────┤
 │ BSS          │ ← ゼロ初期化 static (← map[][] はここ)
 ├──────────────┤
 │ heap ↓       │ ← malloc (第 3 章で登場)
 │              │
 │              │
 │ stack ↑      │ ← 関数の自動変数 (← Snake s; はここ)
 ├──────────────┤
 │ kernel       │ ← ユーザは触れない
 └──────────────┘
高位アドレス
```

`Snake s;` は **書いた時点では** どこに置かれるか決まっていません。
- 関数内の自動変数として宣言すれば → **スタック**
- ファイルスコープ (関数の外) に書けば → **BSS** (ゼロ初期化)
- `malloc(sizeof(Snake))` で取れば → **heap** (第 3 章)

「変数の住所」は **変数の名前ではなく書いた場所** で決まる、というのが C の最初の山です。

## 演習

- **Easy**: `ROWS` を 30 に増やしてビルド。蛇が壁にぶつからずに広い場所を動けるか確認 (描画が崩れるなら理由を考える)。 *ヒント: いま使っているターミナルウィンドウの「行数」はいくつ?*
- **Med**: `Snake` 構造体のフィールド順を **`score → body → len → dir → alive`** に書き換え、`./snake_step2 --inspect` の `sizeof` がどう変わるか測る。 *結果は本文の「4. 末尾の落とし穴」を踏まえて考察してみよう (実はサイズは減らない! なぜ?)*
- **Hard**: 体を 100 マスまで伸ばした状態で 1 tick あたりの移動コストを `clock_gettime(CLOCK_MONOTONIC, ...)` で計測。「後ろから前へずらす」素朴実装と、配列の先頭/末尾インデックスだけを動かすデータ構造で書いた実装の **2 通り** を比較せよ。どちらが速いかは伸ばす長さで変わるはず。計測の話は第 12 章 (perf) で再訪する。

## 次章では
第 3 章は **蛇を `malloc` と連結リストで育てます**。配列の「ずらしコスト」を消す代わりに、`malloc` / `free` の呼び出しと **二重 free** という新しい事故が生えます。同じバグを `valgrind` で **観察** しに行きます。
