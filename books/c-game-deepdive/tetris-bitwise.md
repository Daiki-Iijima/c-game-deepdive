---
title: "第5章 — 回転とビット演算: ピース形状を 16 bit に詰める"
---

:::message
本連載で書くコード一式は **[GitHub: Daiki-Iijima/c-game-deepdive](https://github.com/Daiki-Iijima/c-game-deepdive)** にあります。本文中で `01_snake/step1_termios/main.c` のように参照する path はすべてそのリポ内のファイルです。
:::


![demo placeholder](https://placehold.co/800x300?text=tetris+v2+bitwise)

## つかみ

第 4 章の Tetris では、ピース形状を `uint8_t cells[4][4]` でベタ書きしていました。 1 つの形状で **16 byte**。 4 状態 × 7 種で 448 byte。 数字としては小さいけれど、 「**16 マス分の情報なんだから 16 bit で済むはずだ**」 という直感は正しい。

本章はその直感を実装し、 形状を `uint16_t` 1 個で表現します。 ついでに 「回転をどうディスパッチするか」 を **関数ポインタの配列** で書き直し、 「データで挙動を切り替える」 C 流のテクニックを学びます。

## 今日の機構: 4x4 grid を 16 bit に

```
   bit 順: (i, j) → bit (i*4 + j)
   最下位 (0) 側が左上、 上位 (15) 側が右下
```

例として **横向き I ピース** (1 行目だけ全部 1):

```
....    bit 0  1  2  3   = 0
####    bit 4  5  6  7   = 1
....    bit 8  9 10 11   = 0
....    bit 12 13 14 15  = 0
       ↓
   0b00000000_11110000  = 0x00F0
```

実装はこうなります:

```c
static const uint16_t PATTERNS[7][4] = {
    /* I */ { 0x00F0, 0x4444, 0x0F00, 0x2222 },
    /* O */ { 0x6600, 0x6600, 0x6600, 0x6600 },
    /* T */ { 0x4E00, 0x4640, 0x0E40, 0x4C40 },
    /* ... */
};

#define BIT_AT(p, i, j) (((p) >> ((i)*4 + (j))) & 1u)
```

衝突判定はこう変わります:

```c
static int try_place(int kind, int rot, int r, int c) {
    uint16_t p = PATTERNS[kind][rot & 3];
    for (int i = 0; i < 4; i++) for (int j = 0; j < 4; j++) {
        if (!BIT_AT(p, i, j)) continue;
        /* ... 第 4 章と同じ ... */
    }
}
```

二重ループの中身は同じですが、 **形状データが 16 byte → 2 byte** に。 さらに第 12 章で見る通り、 連続したパターンを **16-bit 同士の比較や popcount で一発判定** することも可能 (例: ライン消去候補の高速化)。

## 関数ポインタ配列で回転をディスパッチ

「↑ キーで時計回り」「z で反時計回り」「x で 180 度回転」を switch で書くと、 ロジックの本体 (= rot 値の更新) と入力解釈が混ざります。 これを **テーブル + 関数ポインタ** で剥がします。

```c
typedef int (*RotateFn)(int cur);
static int rot_id  (int cur) { return cur; }
static int rot_cw  (int cur) { return (cur + 1) & 3; }
static int rot_180 (int cur) { return (cur + 2) & 3; }
static int rot_ccw (int cur) { return (cur + 3) & 3; }

static const RotateFn ROT_FN[4] = { rot_id, rot_cw, rot_180, rot_ccw };

static void try_rotate(int idx) {
    int nr = ROT_FN[idx & 3](g_cur_rot);
    if (try_place(g_cur_kind, nr, g_cur_r, g_cur_c)) g_cur_rot = nr;
}
```

`ROT_FN[1](2)` は **「ROT_FN 配列の 1 番目に居る関数アドレスへ jump して、引数 2 で呼ぶ」**。 配列要素が **値ではなく実行コードへの番地** になっている、というのがポイントです。 第 12 章で `objdump` を使うと、 `try_rotate` の中に `call *(ROT_FN+rdi*8)` のような **間接呼び出し** が立つのが見えます。

## 作る

```sh
cd 02_tetris/step2_bitwise
make
./tetris_step2          # ↑=CW、z=CCW、x=180、← →=移動、↓=ソフト、space=ハード
./tetris_step2 --dump-shapes   # 16-bit パターンを ASCII で確認
```

`--dump-shapes` は学習用の隠しモードです:

```
T rot=0  bits=0x4E00
.#..
###.
....
....

T rot=1  bits=0x4640
.#..
.##.
.#..
....
```

「自分が書いた hex の数字が、 想定通りの形になっているか」 を一発で確かめられます。 第 4 章のベタ書きテーブルでは目で形が見えていたのに、 今回は数字に化けたから、 **自分の手で形を取り戻すツールが要る**。

## 覗く: 「数字の代わりに形」を確認

`--dump-shapes` を全部眺めれば、 28 個の `uint16_t` がきちんとピースになっていることが分かります。 もし `T rot=2` の bits を **書き間違えていたら**、 そこだけ `#` の並びが崩れて見える。 こういう **テストを兼ねたダンプ機能** は、 ゲームに限らず C で「データを変な数字でエンコードした」とき必ず役に立ちます。

## ビット演算のおさらい

本章で使った演算:

| 式 | 名前 | 意味 |
|---|---|---|
| `(p >> n) & 1u` | bit テスト | n bit 目が立っているか |
| `p \| (1u << n)` | bit セット | n bit 目を立てる |
| `p & ~(1u << n)` | bit クリア | n bit 目を消す |
| `p ^ (1u << n)` | bit トグル | n bit 目を反転 |
| `(rot + 1) & 3` | mod 4 | 4 で割った余り (4 が 2 のべき乗だから) |

第 9 章 (Roguelike fork+pipe) では、 親と子のあいだで 1 byte に **複数のフラグ** を詰めて IPC します。 第 10 章 (endian) は **「同じ bit 列をどう順序づけるか」** がテーマ。 ここで bit に慣れておくと後が楽になります。

## メンタルモデル更新

```
[人間にとって便利な表現]      [メモリにとって便利な表現]
  uint8_t cells[4][4]   ←→   uint16_t pattern
   16 byte                    2 byte

[ifと switch で書いた挙動] ←→ [テーブルと関数ポインタで書いた挙動]
   ROT_FN[idx](cur)           call *table[idx]
```

「データの形」と「コードの形」は **入れ替えられる** ことが多い。 C の中級者からは、 そのトレードオフを意識してデータ構造を選ぶ場面が増えてきます。

## 演習

- **Easy**: `PATTERNS[T][2]` を **わざと** `0x0F00` (= 横棒) に書き換え、 `--dump-shapes` で T の rot=2 がおかしくなることを確認。 直すこと。
- **Med**: 4x4 のパターン `uint16_t p` を **時計回りに 90 度回転** させる関数 `uint16_t rotate_cw(uint16_t p)` を書け。 これを使えば `PATTERNS` のテーブルを 1 状態分だけ手書きすれば残り 3 状態は自動生成できる。 ただし O ピースのように対称な形だと余分な自由度が出る。
- **Hard**: 関数ポインタテーブル `ROT_FN` を **2 段** に拡張: `KICK_FN[kind][rot_idx](cur_rot, *cur_r, *cur_c)` のシグニチャで、 ピース種別ごとに **回転中心がずれる Wall Kick** を実装する (簡易版で OK)。 関数ポインタの 2 次元配列を C で書いてみる。

## 次回予告

第 6 章は **衝突判定とメモリ事故**。 `try_place` の境界チェックを **わざと甘く** 書いた版を用意し、 `AddressSanitizer (ASan)` でビルドして **buffer overrun を実時間で検出** します。 valgrind より速く、 そして 「同じ無効アクセスでも違うレポート」 が出ることを比べます。
