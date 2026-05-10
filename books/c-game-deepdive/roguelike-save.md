---
title: "第10章 — セーブデータの怪: 構造体直書きの罠と endian / padding"
---

:::message
本連載で書くコード一式は **[GitHub: Daiki-Iijima/c-game-deepdive](https://github.com/Daiki-Iijima/c-game-deepdive)** にあります。本文中で `01_snake/step1_termios/main.c` のように参照する path はすべてそのリポ内のファイルです。
:::


![demo placeholder](https://placehold.co/800x300?text=hexdump+of+naive+vs+explicit+save)

## はじめに
「ゲームのセーブを実装してください」 と言われたら、 ナイーブにはこう書きたくなります:

```c
fwrite(&save, sizeof(save), 1, fp);     /* 出す */
fread (&save, sizeof(save), 1, fp);     /* 読む */
```

**1 行で動く**。 同じバイナリで save → load なら問題なし。 罠は **「同じバイナリじゃない時」** に立ち現れます:
- 別のコンパイラで再ビルドしたら struct の padding が変わって読めない
- ARM (Apple Silicon) で保存して x86_64 で読むと数字が逆転する
- 構造体に新しいフィールドを足したら、 過去のセーブが全部使い物にならない

ゲームに限らず、 **C のバイナリフォーマットでよく死ぬ場所** です。 本章は、 同じ `Save` 構造体に対して 2 通りの save/load を実装し、 hex dump で **何が違うのか** を目で見ます。

## 本章のテーマ: 「直書き版」 と 「明示版」 を 1 ファイルに同居させる

```c
typedef struct {
    uint8_t  version;
    uint64_t score;
    uint16_t player_r;
    uint16_t player_c;
    uint64_t playtime_ms;
    uint8_t  flags;
} Save;
```

このまま `sizeof(Save)` を取ると **40 byte**。 中身を足し算すると 1+8+2+2+8+1 = 22 byte。 実に **18 byte が padding** です。 (前章でも別の例で見ました。)

### 直書き版

```c
fwrite(MAGIC, 1, 4, fp);           /* "RGSV" */
fwrite(&save, sizeof(save), 1, fp); /* 40 byte 丸ごと */
```

シンプル。 ただし書かれるのは **コンパイラが置いた padding バイトを含んだ全 40 byte**、 順序は **host のエンディアン**。

### 明示版

```c
wr_u8 (fp, save.version);         /* 1 byte */
wr_u64(fp, save.score);           /* 8 byte (little-endian で書く) */
wr_u16(fp, save.player_r);        /* 2 byte (LE) */
wr_u16(fp, save.player_c);        /* 2 byte (LE) */
wr_u64(fp, save.playtime_ms);     /* 8 byte (LE) */
wr_u8 (fp, save.flags);           /* 1 byte */
```

合計 22 byte + magic 4 byte = **26 byte**。 padding なし、 host のエンディアン非依存。 `wr_u64` は bytewise に書くので、 マシンの内部メモリ順序を気にしなくてよい:

```c
static uint64_t to_le64(uint64_t v) {
    uint64_t r;
    uint8_t *p = (uint8_t *)&r;
    for (int i = 0; i < 8; i++) p[i] = (uint8_t)((v >> (i*8)) & 0xFF);
    return r;
}
static void wr_u64(FILE *fp, uint64_t v) {
    uint64_t le = to_le64(v);
    fwrite(&le, sizeof(le), 1, fp);
}
```

`<endian.h>` の `htole64` を使ってもよいのですが、 **bytewise に組み立てる方が「何が起きているか」が透明** で、 教材としても向きます。

## 動かして観察する
```sh
cd 03_roguelike/step4_save
make
./save_step4 inspect
```

```
sizeof(Save) = 40
offsetof(score) = 8
offsetof(playtime_ms) = 24
```

予想通り `version (1B)` の直後に **7 byte の padding** が入って `score` が 8-byte 境界に揃っています。 `score (8B)` 末尾は 16、 そこから `player_r/c (2B+2B)` で 20、 次の `playtime_ms (8B)` の前に **4 byte の padding** が入って 24 から始まる。

### 同じ `Save` を 2 種類の方法で保存して比較

```sh
make demo
```

実際の出力 (筆者の x86_64 環境):

```
--- naive.save ---
00000000: 5247 5356 0100 0000 0000 0000 d202 9649  RGSV...........I
00000010: 0000 0000 0c00 2200 0000 0000 ea16 b04c  ......"........L
00000020: 0200 0000 a500 0000 0000 0000            ............

--- explicit.save ---
00000000: 5247 5356 01d2 0296 4900 0000 000c 0022  RGSV....I......"
00000010: 00ea 16b0 4c02 0000 00a5                 ....L.....
```

読み比べポイント:

- **naive の 0005-000B (7 バイト)** がすべて `00`: これが padding
- **naive の 0014-0017 (4 バイト)** も `00 00 00 00`: これも padding
- naive 全体は **40 byte (= sizeof(Save))**、 explicit は **22 byte** + magic 4 = **26 byte**
- naive は **little-endian で `score = 1234567890 = 0x499602D2`** が `D2 02 96 49 ...` の順で出ている (= host LE)。 もし big-endian マシンに持っていったら順序逆転で `0x4996 02D2 0000 0000` になり、 戻すと値が壊れる
- explicit は **bytewise に LE で書いた** ため、 host の endian に関わらずファイル中身は同じ

## なぜ ARM (Apple Silicon) と x86_64 で問題が起きるのか

両方とも little-endian です — **多くの場合バイトを逆転する事故にはなりません**。
だが事故は十分起きます:
- `long` のサイズが ABI で違う (LP64 / LLP64) と padding がずれる
- `enum` の幅が処理系で 1/2/4 byte 揺れる
- 同じ値でも C 規格は struct のレイアウトを完全には保証しない

「コンパイラが同じだから動く」 は **同じバージョンのコンパイラを想定する間だけ** の保証。 リリースしたらユーザの環境はこちらの管理外なので、 **明示シリアライズが事実上の必須** になります。

## メンタルモデルを整理する

```
Save struct (in memory)
[ ver | pad pad pad pad pad pad pad |   score (8B)   |
  pl_r | pl_c | pad pad pad pad |  playtime_ms (8B)  |
  flags| pad pad pad pad pad pad pad ]  ← 末尾も 8-byte align まで埋まる
sizeof = 40

ファイル (naive版 fwrite)
↓ 構造体のメモリ表現を そのまま 40 byte 書く
[40 byte padded layout, host-endian dependent]

ファイル (explicit版)
↓ フィールドを 1 つずつ規定の順序で並べる
[ver|score(LE)|pl_r(LE)|pl_c(LE)|playtime(LE)|flags] = 22 byte
```

「データの形は **メモリの上での形** と **ファイルの上での形** の 2 種類ある」 という分離が C に来ると一段意識される必要があります。 第 12 章で ELF を読むときも同じ視点が役立ちます (ELF も「メモリ表現」 と 「ファイル表現」 が異なる規格)。

## 演習

- **Easy**: `Save` 構造体のフィールド順を **`uint64_t score → uint64_t playtime_ms → uint16_t player_r/c → uint8_t version → uint8_t flags`** に並べ替え、 `./save_step4 inspect` で `sizeof` がどう変わるか測れ。 padding はどこに残った?
- **Med**: 直書き版で保存したファイルを **手で書き換え** (`xxd` の逆 `xxd -r`) てから `load_naive` で読ませ、 違う値を読み出すことを確認。 さらに **explicit 版で保存して同じ場所のバイトを書き換え** た時、 直書き版より **どの値が壊れるかが直感的** であることを確認する。
- **Hard**: explicit 版に **「version 番号で未来のフォーマット拡張を許す」 設計** を入れる。 `version=2` で `magic_number` フィールドを追加した場合に、 `version=1` のセーブを読み込むと欠けたフィールドはデフォルト値で埋まる、 そんな loader を実装。 これが本物のゲームのセーブ互換性。

## 次章では
第 11 章は **総復習**: 第 3 章で出した二重 free、 第 6 章の境界外、 第 9 章の pipe デッドロック…  シリーズ中で見せたバグを **再訪し、 gdb と valgrind の合わせ技で潰し方を一通り** やります。 ブレークポイント、 watchpoint、 coredump 解析、 Helgrind (race 検出) まで。 第 12 章では同じバイナリを **逆アセンブル** し、 自分の C コードがどんな asm に化けたかを見ます。
