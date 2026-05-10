---
title: "第3章 — 蛇を連結リストで育てる: malloc / free / 二重 free と valgrind"
---

:::message
本連載で書くコード一式は **[GitHub: Daiki-Iijima/c-game-deepdive](https://github.com/Daiki-Iijima/c-game-deepdive)** にあります。本文中で `01_snake/step1_termios/main.c` のように参照する path はすべてそのリポ内のファイルです。
:::


![demo placeholder](https://placehold.co/800x300?text=snake+v2+%28linked+list%29+%2B+food)

## はじめに
第 2 章の蛇は **長さの上限が `MAX_LEN = 256` で固定** でした。長く伸ばしたいなら `MAX_LEN` を増やすしかなく、その分メモリは常に予約されたまま。
プレイ開始時 5 マスしかない蛇のために 256 マス分の配列を抱えるのは「使わない倉庫を借りっぱなし」みたいなものです。

本章は、**蛇が伸びた瞬間にだけ 1 ノード借り、縮んだ瞬間に返す** 仕組みに置き換えます。借りる窓口の名前は **`malloc`**、返す窓口は **`free`**。同時に、その窓口で起こる **最初の事故 — 二重 free** を、 `valgrind` という探偵を呼んで現場で見ます。

## 本章のテーマ: 連結リストと malloc/free

```c
typedef struct Node {
    uint8_t       r, c;
    struct Node  *next;   /* 末尾は next == NULL */
} Node;

typedef struct {
    Node  *head;     /* @ */
    Node  *tail;     /* o (一番後ろ) */
    int    len;
    /* ... */
} Snake;
```

蛇が動く 1 tick で起きること:

1. 新しい頭の座標 `(nr, nc)` を計算
2. **`malloc(sizeof(Node))`** で頭ノードを 1 個もらう
3. `new_head->next = old_head; head = new_head;` で先頭に挿入
4. 食べてなければ、 `tail` を 1 個 **`free`** して縮める

```c
Node *new_head = node_new(nr, nc);
new_head->next = s->head;
s->head        = new_head;

if (eat) {
    /* tail はそのまま残す → 1 マス伸びる */
} else {
    Node *old_tail = s->tail;
    /* 一個前のノードを線形に探す (リストが片方向だから) */
    Node *prev = s->head;
    while (prev->next != old_tail) prev = prev->next;
    prev->next = NULL;
    s->tail    = prev;
    free(old_tail);
}
```

ここで **「`free` を 2 回呼んだらどうなるか?」** に踏み込みます。

## 実装する
```sh
cd 01_snake/step3_linkedlist
make
./snake_step3        # 食料 * を食べると体が伸びる
```

`q` で抜けます。終了時に `snake_free(&s)` で残ったリストを全部 `free` しています。これを忘れると **leak** が発生します。

## 観察 1: わざと二重 free を起こす

`main.c` には学習用に「テールを `free` した直後にもう一度 `free` する」モードを仕込んであります。

```sh
./snake_step3 --bug=double-free
```

何ステップか動いた直後に **クラッシュ**します。glibc の malloc 実装が「同じブロックの二度目の解放」を検出して `double free or corruption` というメッセージを吐いて `abort()` します。 `dmesg` や coredump を見るとさらに細かく追えます。

しかし、 **glibc が必ず気づくとは限りません**。気づくのは比較的最近の glibc の話で、しかも検出は性能とのトレードオフで間引かれます。確実に捕まえるには **valgrind** を使います。

## 観察 2: valgrind で犯行を再現する

```sh
make valgrind
# 中身: valgrind --leak-check=full --show-leak-kinds=all ./snake_step3 --bug=double-free
```

:::details `valgrind --leak-check=full --show-leak-kinds=all` のフラグ解説
- `valgrind` は **動的バイナリ翻訳** を使ってプロセスを実行しつつ、 メモリアクセス 1 つひとつをシミュレート上で監視するツール。 デフォルトのツール (= `--tool=memcheck`) は invalid read/write、 use-after-free、 leak を捕まえる。
- `--leak-check=full`: 終了時に「使われていない・到達不能なヒープブロック」を **詳細にスタックトレース付きで** 表示する。 デフォルトの `summary` だと件数しか出ない。
- `--show-leak-kinds=all`: leak の種別を絞らず全部表示する。 種別は次の 4 つ:
  - `definitely lost`: ポインタが完全に失われた (= 100% 漏れ)
  - `indirectly lost`: 別の漏れ経由で連鎖的に到達不能になった
  - `possibly lost`: ポインタが「中途半端に残っている」
  - `still reachable`: 終了時にまだ参照可能 (= 漏れではないが解放していない)
- その他便利フラグ:
  - `--track-origins=yes`: uninitialised value の **出所** まで追跡 (遅くなる)。
  - `--error-exitcode=N`: バグ検出時に終了コードを N にする (CI で活躍)。
  - `--num-callers=N`: スタックトレースの深さ (デフォルト 12)。
  - `-q`: バグが無ければ無音 (CI 向け)。
:::

少し動かしてから `q` で抜けると、こんな出力が出ます (抜粋)。

```
==1234== Invalid free() / delete / delete[] / realloc()
==1234==    at 0x484XXXX: free (vg_replace_malloc.c:XXX)
==1234==    by 0x10XXXX: snake_step (main.c:140)
==1234==    by 0x10XXXX: main (main.c:218)
==1234==  Address 0x4XXXXXX is 0 bytes inside a block of size 16 free'd
==1234==    at 0x484XXXX: free (vg_replace_malloc.c:XXX)
==1234==    by 0x10XXXX: snake_step (main.c:138)
```

何が言われているか:

- **何が起きたか**: 既に `free` 済みのアドレス (`16 byte block`) をもう一度 `free` した
- **どこで**: `main.c:140` が **2 度目の `free`**、`main.c:138` が **1 度目の `free`**
- **どこから来たブロックか**: その 2 行の間に `at malloc...by main.c:XX` (元の確保場所) が出ます

valgrind は **「同じアドレスがいつ・どこで `malloc` され、いつ・どこで何度 `free` されたか」** を全部覚えています。これが「探偵」の正体です。

valgrind を抜けて leak も見ましょう。

```
==1234== HEAP SUMMARY:
==1234==     in use at exit: 0 bytes in 0 blocks
==1234==   total heap usage: 47 allocs, 47 frees, ...
```

`in use at exit: 0 bytes` ならリーク無し。`snake_free()` を消した状態で再実行すると、 **使用中ブロックの一覧** が `definitely lost` として表示されます。

## メンタルモデルを整理する: heap

```
スタック (高位 → 低位)
  ┌──────────────┐
  │ Snake s      │ ← head/tail/len/dir/...
  │   head ─────┐│
  │   tail ────┐││
  └────────────┼┼┘
               ││
               ││  malloc が返したアドレス
               ▼▼
ヒープ (低位 → 高位)
  ┌──────┐  ┌──────┐  ┌──────┐
  │ Node │→│ Node │→│ Node │→ NULL
  │ .next├─┘.next ├─┘.next │
  └──────┘  └──────┘  └──────┘
   16B       16B       16B   ← それぞれ malloc で確保された島
```

配列 (`Cell body[256]`) との一番大きな違い:

| | 配列 | 連結リスト |
|---|---|---|
| メモリの並び | 連続 | 飛び飛び |
| 要素アクセス | `O(1)` (掛け算) | `O(N)` (next を辿る) |
| 末尾削除 | インデックス減らすだけ | tail 直前まで線形に探索 |
| 拡縮 | 上限固定 | 必要なときに malloc/free |
| キャッシュ局所性 | 強い | 弱い |

「リストの方が偉い」わけでも「配列の方が偉い」わけでもなく、**捨てるコストと借りるコストのトレードオフ** です。第 12 章で `perf` を使ってここに戻ってきます。

## 観察 3: heap と stack のアドレス

ゲームを動かす前に、 `pmap $(pgrep snake_step3)` (別ターミナル) で蛇プロセスのメモリマップを覗くと、 `[heap]` 行と `[stack]` 行のアドレス範囲が見えます。 `Node` ノード達のポインタは `[heap]` 範囲内、 `Snake s` 構造体のアドレスは `[stack]` 範囲内に居ます。第 2 章の `&s_on_stack` と `&map` の話の続きです。

:::details `pmap` と `pgrep` の解説
- `pgrep <pattern>`: プロセス名で PID を引く。 `pgrep snake_step3` は名前に `snake_step3` を含むプロセスの PID を全部出す。 シェルの `$(...)` で囲んで他コマンドに渡すのが定番。
- `pmap <pid>`: そのプロセスの **仮想アドレス空間のレイアウト** を表示する。 内部では `/proc/<pid>/maps` を整形して読んでいるだけ。 同じ情報を直接 `cat /proc/<pid>/maps` でも見られる。
- 出力に出てくるラベル:
  - `[stack]`: メインスレッドのスタック
  - `[heap]`: `brk()` / `sbrk()` で伸びる典型的な heap 領域 (大きい malloc は `mmap` 経由で別の anonymous マッピングになることもある)
  - `r-xp /lib/x86_64-linux-gnu/libc.so.6`: 共有ライブラリのコード
- 別ターミナルで動かしながら見るのがコツ。 ゲームを `q` で抜けるとプロセスは消えるので。
:::

## 演習

- **Easy**: `snake_free(&s)` を main の最後から **削除** して `make valgrind`。 `definitely lost: N bytes in M blocks` の N と M はいくつになる? ノード 1 個のサイズ × 残り長さと一致するはず。
- **Med**: `place_food` のループは「蛇が画面を埋めるとほぼ無限ループになる」設計。蛇の長さが `(ROWS-2)*(COLS-2)` に近づいた時の挙動を観察し、 **配列スキャン → 空きセル列挙 → ランダムに 1 個** に書き直してみよう。
- **Hard**: `--bug=use-after-free` モードを追加せよ。 `free` した直後の `old_tail->r` を読みに行くコードを書き、 valgrind が `Invalid read of size 1` をどう報告するかを観察。 さらに **AddressSanitizer (`make asan`)** で同じバグを動かし、 出力フォーマットの違いを比較する。

## 次章では
第 4 章は **Tetris** に切り替わります。蛇は連結リスト 1 本で済みましたが、 Tetris では **次のピースのキュー** という構造を扱う必要があり、 heap の使い方が一段複雑になります。 `valgrind --tool=massif` で **「いつ・何 byte heap を使っているか」** を時系列でグラフにします。
