---
title: "第6章 — 衝突判定とメモリ事故: AddressSanitizer 初登場"
---

:::message
本連載で書くコード一式は **[GitHub: Daiki-Iijima/c-game-deepdive](https://github.com/Daiki-Iijima/c-game-deepdive)** にあります。本文中で `01_snake/step1_termios/main.c` のように参照する path はすべてそのリポ内のファイルです。
:::


![demo placeholder](https://placehold.co/800x300?text=tetris+v3+%2B+ASan)

## つかみ

第 5 章の Tetris は **衝突判定が無事に動いていれば** 静かなものでした。
本章では、その **衝突判定をわざと壊します**。 そして 「壊れた瞬間に叫ぶツール」 を入れます。 名前は **AddressSanitizer**、 略して ASan。

valgrind は前章までで使いました。 ASan は valgrind と何が違うのか? **コンパイル時に仕掛けを埋め込む** タイプの検査ツール、 動作中のオーバヘッドが小さく、 報告内容が valgrind と少し違う。 同じ仮想バグを両方で見て比較するのが本章のメインイベントです。

## 今日の機構: 仕込まれた 2 つのバグ

`02_tetris/step3_collision/main.c` には、 起動引数 `--bug=oob` で 2 種類の事故が同時に発動します。

### バグ 1: 配列外読み (out-of-bounds read)

`try_place_buggy` は、 行/列の境界チェックの **前に** `g_board[nr][nc]` を読みます。

```c
static int try_place_buggy(int kind, int rot, int r, int c) {
    uint16_t p = PATTERNS[kind][rot & 3];
    for (int i = 0; i < 4; i++) for (int j = 0; j < 4; j++) {
        if (!BIT_AT(p, i, j)) continue;
        int nr = r + i, nc = c + j;
        if (g_board[nr][nc]) return 0;             /* ← OOB を踏む */
        if (nr < 0 || nr >= ROWS || nc < 0 || nc >= COLS) return 0;
    }
    return 1;
}
```

ピースがスポーン直後 (`g_cur_r = 0`) で、 `BIT_AT(p, i=0, j=...)` のセルが立っていなければ問題は起きません。 しかし I ピースが横向きなら 1 行目だけが立っている (`bit 4-7`)、 J/L/T などスポーン形では 0 行目にもブロックがあり、 **ピースを上にもう 1 段押し戻すような操作 (= 通常はあり得ない) を試した瞬間に `nr < 0` が成立** し、 その時点で `g_board[-1][...]` を読みに行きます。

### バグ 2: グローバル配列の境外書き込み

`g_score_history[16]` というスコア履歴を、 `--bug=oob` 時はインデックスを `mod 16` しないで書き続けます。 100 ピース置けば必ず溢れる。 ASan が **`global-buffer-overflow`** を即報告。

## 作る・壊す・直す

### 健全動作の確認 (バグ無し)

```sh
cd 02_tetris/step3_collision
make
./tetris_step3
```

第 5 章とほぼ同じプレイ感。 スコア表示の右に `bug=off` が見えます。

### ASan ビルドでバグ ON

```sh
make asan_bug
# = make asan で -fsanitize=address,undefined を有効にしてビルド
# → ./tetris_step3 --bug=oob
```

少しプレイすると ASan が叫びます (抜粋):

```
=================================================================
==12345==ERROR: AddressSanitizer: global-buffer-overflow on address 0x55... at pc 0x10... bp 0x7f... sp 0x7f...
WRITE of size 8 at 0x55... thread T0
    #0 0x10... in note_score main.c:113
    #1 0x10... in handle_input main.c:184
    #2 0x10... in main main.c:230

0x55... is located 0 bytes to the right of global variable 'g_score_history' defined in 'main.c' (0x55...) of size 128
SUMMARY: AddressSanitizer: global-buffer-overflow main.c:113 in note_score
```

最初の事故 (グローバル配列の越境書き) を **行番号レベルで** 指してきます。 「`g_score_history` という名前のグローバル変数の **直後 0 byte** に 8 byte 書こうとした」 と教えてくれます。
配列のサイズも `(0x55...) of size 128` (= 16 個 × 8 byte) と一致。

### valgrind 同じバグでビルド

```sh
make valgrind_bug
# = valgrind --error-exitcode=1 ./tetris_step3 --bug=oob
```

valgrind の `memcheck` は **グローバル変数の越境書き込みを完全には検出しません** (heap と stack には強いが、 静的領域の 16 byte 配列の 1 つ右隣に 8 byte 書く程度では沈黙することがある)。 一方、 `try_place_buggy` の方の **配列外 read** はうっすら反応します:

```
==4567== Conditional jump or move depends on uninitialised value(s)
==4567==    at 0x4XXXX: try_place_buggy (main.c:91)
```

「uninitialised value に依存して分岐した」と。 これは ASan の `global-buffer-overflow` ほどダイレクトな宣言ではありませんが、 「ヒントとして異物がある」 ことを匂わせる、 という違いです。

## 覗く: ASan の中身

ASan は `-fsanitize=address` でコンパイルすると、 **メモリアクセスのたびに redzone (毒入り領域) の状態を確認** するコードをインライン展開します。 全グローバル変数の前後に毒領域が並び、 そこに触ると即時 `abort()`。 `objdump -d tetris_step3` で `__asan_load*` / `__asan_store*` という symbol が散らばるのが見えます。 第 12 章で逆アセンブルする時にもう一度確認します。

| | valgrind | ASan |
|---|---|---|
| 仕組み | 動的バイナリ翻訳 (実行時に挙動を変換) | コンパイル時計装 |
| 速度 | 5-30 倍遅い | 1.5-3 倍遅い |
| 強み | 元のバイナリを変えずに使える | グローバル/スタックの境外検出に強い |
| 弱み | グローバル/スタック境外は弱い | バイナリ再ビルド必須 |
| 学習用途 | 既存バイナリの調査 | 開発中の常用 |

両方使えるとデバッグの解像度が一段上がります。

## メンタルモデル更新: redzone

```
グローバル領域 (data/BSS):
  ┌─────┬───────────────────────┬─────┬──────────────┬─────┐
  │redzn│ g_score_history (128) │redzn│ g_score (8)  │redzn│
  └─────┴───────────────────────┴─────┴──────────────┴─────┘
                              ↑
               ここに書くと ASan が叫ぶ
```

「変数の隙間が貴重なメモリだから埋めなきゃ」ではなく、 **わざと毒の壁を立てる** デバッグ手法です。 ASan を切れば redzone は消え、 通常のメモリレイアウトに戻ります。

## 演習

- **Easy**: `make asan` (バグ無し) で起動してフルプレイし、 ASan が **何も叫ばない** ことを確認 (= 健全コードで誤検知がないこと)。
- **Med**: `try_place_buggy` の中の `if (g_board[nr][nc])` を **safe 版と同じく境界チェック後に動かす** ように 1 行 swap しただけのコードに直し、 ASan が黙ることを確認せよ。 直した版を valgrind に通しても黙ることを確認。
- **Hard**: ASan を **切った状態 (普通の `make`)** で `--bug=oob` を動かす。 「すぐクラッシュする?」「気づかれず動き続ける?」 「クラッシュ位置と本当の原因の距離は?」 を観察し、 1 段落のレポートにまとめる。 これは「sanitizer 無しの世界での未定義動作の怖さ」を体験する演習。

## 次回予告

第 7 章は **Roguelike** に切り替わり、 動的なダンジョン生成を実装します。 大きな heap 確保 (`malloc(N * M * sizeof(Tile))`) と、 構造体の **flexible array member** の使い分けを学びます。 ASan/valgrind は引き続き使い、 「大きな heap」 の世界で何が変わるかを見ます。
