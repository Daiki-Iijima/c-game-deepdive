---
title: "第4章 — 落ちるブロックと heap の海: Tetris と動的キュー"
---

:::message
本連載で書くコード一式は **[GitHub: Daiki-Iijima/c-game-deepdive](https://github.com/Daiki-Iijima/c-game-deepdive)** にあります。本文中で `01_snake/step1_termios/main.c` のように参照する path はすべてそのリポ内のファイルです。
:::


![demo placeholder](https://placehold.co/800x300?text=tetris+v1+heap+queue)

## はじめに
第 3 章で蛇は **「伸びた瞬間に malloc、縮んだ瞬間に free」** で動きました。

本章で作る Tetris は別の動機で heap を必要とします。
 **「次に来るピースの予告キュー」** です。

ゲームが続く限り、キューには常に次のピースが押し込まれ、上から取り出される。
 `malloc` と `free` のリズムが Snake よりずっと忙しい。


このリズムを **目で見るために**、 `valgrind --tool=massif` を導入します。
 「いつ heap が増え、いつ減ったか」 を時系列のグラフで吐き出してくれる、heap の聴診器のようなツールです。


## 本章のテーマ: 7-bag キュー (FIFO) を heap で持つ

Tetris の標準的なピース供給は **7-bag**: 7 種類のピース (I/O/T/S/Z/J/L) を 1 セット (=「袋」) として、その中身をシャッフルしてから順に出す。
1 セット内では同じ種類が出ないため、長期的な確率は均等になります。


実装は **片方向連結リスト + 末尾ポインタ** = キューです。


```c
typedef struct PieceNode {
    int                kind;
    struct PieceNode  *next;
} PieceNode;

typedef struct {
    PieceNode *head;   /* 取り出し側 */
    PieceNode *tail;   /* 追加側 */
    int        size;
} Queue;
```

`q_push` は末尾に `malloc` で 1 個生やし、 `q_pop` は先頭を `free` する。
 push / pop のたびに heap が動きます。


## 実装する
```sh
cd 02_tetris/step1_heap
make
./tetris_step1
# ← → 移動、 ↑ 回転、 ↓ ソフトドロップ、 space ハードドロップ、 q 終了
```

ハードドロップで複数行揃えると `SCORE` が増えます (ライン x 10 点 / 行)。
 ピースの形状はまだ `Shape SHAPES[7][4]` という **回転 4 状態 ベタ書きテーブル** で持っています。
第 5 章でこれを **16-bit の bit パターン** に置き換えます。


## 観察 1: massif で heap を時系列に見る

```sh
make massif
# 中身: valgrind --tool=massif --massif-out-file=massif.out ./tetris_step1
```

:::details `valgrind --tool=massif` と `ms_print` の解説
- `valgrind --tool=massif`: valgrind の中の **ヒーププロファイラ** ツール。 `memcheck` (デフォルト) とは別で、 メモリの「正しさ」ではなく「使用量の時系列」を測る。
- 仕組み: 一定間隔で `malloc` / `free` の状態をスナップショット化し、 各スナップショットを **どの呼び出し元から確保されたか** とともに記録。
- 出力: `--massif-out-file=PATH` で指定した バイナリ形式 (テキストだが ms_print 専用) に書き出す。
- 関連フラグ:
  - `--threshold=N` (default 1.0%): N% 未満の小さな割り当ては集計しない。
  - `--detailed-freq=N` (default 10): N スナップショットに 1 回、 関数単位で詳細記録。
  - `--time-unit=B/ms/i` (default i): 時間軸を「実行命令数」「ミリ秒」「確保バイト数」のどれにするか。
  - `--pages-as-heap=yes`: heap だけでなく `mmap` ページも heap 扱いに。
- `ms_print PATH`: 出力ファイルを **ASCII グラフ + スナップショット表** に整形して標準出力に吐く。
- `massif-visualizer` (別パッケージ) を入れれば GUI でグラフが見られる。
:::

冒頭にこんな ASCII グラフが出ます:

```
    KB
2.50^                                    #
    |    @@@@@@:@@@@:@@@@@@@@@@:@@:@@:#:@#
    |    :  ::: ::: : ::::::: : :: :: # ##
    |    :  ::: ::: : ::::::: : :: :: # ##
    |    :  ::: ::: : ::::::: : :: :: # ##
    |  @@:  ::: ::: : ::::::: : :: :: # ##
  0 +----------------------------------------> i
                                          240k
```

横軸が「実行 instruction 数」、縦軸が「heap 使用 byte 数」。
 ピースを出すたびにキューに `PieceNode` (16-24 byte) が積まれ、 取り出すたびに減る。
 動かしている間ほぼ一定なのは「7 個出して取って 7 個出して取って ...」のリズムが続いているからです。


詳細スナップショットも下に並びます:

```
--------------------------------------------------------------------------------
  n        time(i)         total(B)   useful-heap(B) extra-heap(B)    stacks(B)
--------------------------------------------------------------------------------
 21         50,123              160              112             48            0
            -> 100.00% (112B) (heap allocation functions) malloc/new
              -> 100.00% (112B) main.c:79 (q_push)
                -> 100.00% (112B) main.c:101 (refill_bag)
                  -> 100.00% (112B) main.c:255 (main)
```

「**この瞬間、heap の 112 byte は `q_push` 経由で `refill_bag` から確保された**」が一発で読めます。
 `--bug=double-free` を仕込まなくても、 **健全なコードの heap の振る舞い** が見える。
これが massif の真価です。


## 観察 2: 「Q=」表示 と heap サイズの相関

`render()` 内で HUD に `Q=N` (現在のキュー長) を出しています。
 7-bag 仕様では Q は **7 → 1 → 7 → 1 ... を行き来する** はず。
 massif の useful-heap グラフが微妙にギザギザするのと同じ現象が見えます。


メンタルモデル:

```
  実行時間 →
heap (B)
  ▲
  │      ▄▖    ▄▖    ▄▖
  │   ▄▟▙█▌ ▄▟▙█▌ ▄▟▙█▌
  │   ▀▀▀▀▀ ▀▀▀▀▀ ▀▀▀▀▀
  └────────────────────────────
   |↑refill_bag (7push)  |↑pop1
                         |↑refill_bag again
```

heap のサイズはプログラムの設計と呼吸を合わせて動きます。
 「平均すると一定」は **leak していない** ということ。
 グラフが右肩上がりなら必ず leak が居ます (= 第 11 章で詳しく見ます)。


## メンタルモデルを整理する: heap の使い分け

|  | Snake (第 3 章) | Tetris (第 4 章) |
|---|---|---|
| 使う場面 | 蛇が伸縮する瞬間 | 次ピースを予告する全期間 |
| 形状 | 単純連結リスト | 連結リスト + tail ポインタ (= queue) |
| 寿命 | ノードは長く残らない | 7 個生まれて 7 個取り出される、繰り返し |
| トラブル傾向 | 二重 free / leak | フラグメンテーション / 繰り返し malloc/free のオーバヘッド |

「heap = ただ大きいメモリ」ではなく **「lifetime が異なる小さな塊たちを管理する場所」** という理解が、第 7 章の動的ダンジョン (大きな塊 1 つ) で再び問われます。


## 演習

- **Easy**: `q_pop` の中の `free(n)` を **コメントアウト** し、 massif でグラフを見る。
 `Q=` 表示が増え続けず一定でも、 heap が右肩上がりになることを確認 (= leak)。

- **Med**: 7-bag を **2-bag** (= 14 個まとめてシャッフル) に書き換え、 massif で heap 使用量がどう変わるかを比較。
 平均サイズと最大サイズ両方読む。

- **Hard**: `Queue` を **配列リングバッファ** で書き直し、 `malloc` / `free` を毎 push/pop で呼ばないようにする。
 同じプレイ時間で massif を比較し、 「`malloc` 呼び出しの**回数**」 (`total allocs`) がどれだけ減るかを実測。
 第 12 章 (perf) でこの違いが速度に出るかを再訪する。


## 次章では
第 5 章は同じ Tetris のまま、 **ピース形状を 4x4 のベタ書きテーブルから 16-bit のビット表現** に置き換えます。
 回転は **bit のローテーション (or テーブル参照)** で 1 命令単位の世界へ。
 その途中で **関数ポインタ配列** が登場し、 「データで挙動を変える」C 流のディスパッチを学びます。

