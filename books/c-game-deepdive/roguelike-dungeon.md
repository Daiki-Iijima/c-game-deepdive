---
title: "第7章 — 自動生成迷宮: 2D 動的確保と flexible array member"
---

![demo placeholder](https://placehold.co/800x300?text=roguelike+v1+dungeon)

## つかみ

ここから 4 章 (7-10) は **Roguelike** に切り替わります。 Snake / Tetris は固定サイズの世界で済みましたが、 Roguelike は **マップサイズが起動時に変わる** のが普通です (24x80 のターミナルもあれば 60x200 で遊びたい人もいる)。

つまり、 ダンジョンは **コンパイル時に大きさが決まらない** = `char map[ROWS][COLS]` では済まない。 本章は、 **起動時の引数でマップを動的確保し、 1 つの `malloc` 呼び出しに収める** 設計を学びます。 鍵は C99 の **flexible array member**。

## 今日の機構: flexible array member

C のよくある悩みは「ヘッダ情報 + 可変長の本体」を 1 つの構造体にしたい、 という需要です。 ナイーブには:

```c
typedef struct {
    int   rows, cols;
    char *tiles;       /* 別途 malloc */
} Map;

Map *m = malloc(sizeof(Map));
m->tiles = malloc(rows * cols);
```

これでも動きますが、 **2 回 malloc** すると **2 回 free** が必要、 `tiles` は別の場所、 構造体だけコピーすると ポインタを共有してしまう。 設計上のトラブルが多い。

C99 の **flexible array member** はこれを 1 つにします。

```c
typedef struct {
    int  rows, cols;
    char tiles[];   /* サイズ未定の配列。 必ず最後のメンバ。 */
} Map;

Map *m = malloc(sizeof(*m) + (size_t)rows * (size_t)cols);
m->rows = rows; m->cols = cols;
memset(m->tiles, '#', (size_t)rows * (size_t)cols);
```

`sizeof(Map)` は `tiles[]` を含みません (ヘッダ部分だけ)。 `malloc` で 「ヘッダ + 本体」 を **1 つのメモリブロック** で取れる。 `free(m)` 1 回で全部返せる。 構造体のコピーやキャッシュ局所性も嬉しい。

## 作る

```sh
cd 03_roguelike/step1_map
make
./rogue_step1            # 24x80 のダンジョン
./rogue_step1 60 200     # ターミナルが大きいなら巨大ダンジョン
```

`hjkl` で移動、 `q` で終了。 起動時の stderr に `Map header sizeof = ..., tiles size = ..., total alloc = ...` を吐きます。 `total alloc` がヘッダ + 本体の合計です。

ダンジョン生成のロジック自体は単純です:

1. ランダムに 6 部屋切る
2. 各部屋の中心を「順番に」廊下でつなぐ (L 字)
3. 1 個目の部屋の中心からスタート

「BSP (Binary Space Partitioning)」 を真面目にやらないと部屋が重なったり廊下が交差したりしますが、 本章のテーマは **メモリの確保方法** なので簡素化しています。 演習で BSP に進化させます。

## 覗く: 1 ブロックに収まっているか

`./rogue_step1` 実行中に別ターミナルで:

```sh
pmap $(pgrep rogue_step1) | grep -E '\[heap\]'
```

heap の使用量に `Map` (ヘッダ + tiles) が **1 領域** として乗っていることが分かります。 試しに main.c を「2 回 malloc 版」に書き換えれば (演習 Med)、 heap 内のブロック数が増える様子が見えます。

## 「2 回 malloc」 vs 「flexible array」 の比較

|  | 2 回 malloc | flexible array |
|---|---|---|
| 確保 | `malloc(sizeof(Map))` + `malloc(rows*cols)` | `malloc(sizeof(*m) + rows*cols)` 1 回 |
| 解放 | `free(m->tiles)` + `free(m)` | `free(m)` 1 回 |
| キャッシュ | ヘッダと本体が別の島 | ヘッダ直後に本体が連続 |
| コピー | 浅いコピー (ポインタ共有) になりやすい | コピーは無効、 別 malloc が要る |
| 拡張 | `realloc(m->tiles, ...)` で本体だけ伸びる | 全体 `realloc` が要る |

「**いつでも flexible array が偉い**」ではなく、 拡張頻度が高ければ 2 回 malloc の方が向くこともあります。 用途で選ぶ訓練が C 中級者の腕の見せ所です。

## メンタルモデル更新

```
flexible array member:
heap   ┌──── Map ────┐
       │ rows = 24   │
       │ cols = 80   │
       ├─ tiles[0..] ┤
       │ ###....###  │
       │ #..#####.#  │
       │ ...         │
       │ ...1920 byte│
       └─────────────┘
       ← 1 個の malloc ブロック (ヘッダ 8 byte + 本体 1920 byte = 1928 byte)
```

「構造体の最後にもう一段、 動的長の領域がぶら下がる」 と考えると視界が開けます。 ELF ファイルの section header table、 protocol message のヘッダ + payload など、 **C で書かれた多くのフォーマット** がこのパターンです。

## 演習

- **Easy**: `./rogue_step1 8 16` (最小サイズ) で起動。 部屋が成立するか? ならないなら、 なぜ?
- **Med**: `Map` を **「2 回 malloc」 版** に書き戻し、 `pmap` で heap のブロック数の差を観察 (ヘッダの直後に本体が連続するか、 飛び飛びになるか)。 `valgrind --tool=massif` の useful-heap グラフで「総量」 は同じだが「ブロック数」 が違うことを確認。
- **Hard**: 部屋の重なりを避ける **BSP 分割** に置き換える。 マップ全体を再帰的に分割 (`split_h` / `split_v`) し、 末端 leaf にだけ部屋を切る。 廊下は **leaf の中心同士を兄弟ノード経由で繋ぐ** 古典実装で。

## 次回予告

第 8 章は **SIGWINCH (ターミナルサイズ変更通知) を捕まえます**。 プレイ中にターミナルウィンドウをドラッグでリサイズすると、 ダンジョンが再描画されないと表示が崩れます。 OS からの **割り込み** を `sigaction` で受け、 **async-signal-safe** という制約を守りながらどうゲームに伝えるか — 第 1 章で軽く触れた signal の話を真面目に詰めます。
