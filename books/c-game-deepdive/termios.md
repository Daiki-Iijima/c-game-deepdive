---
title: "第1章 — キー入力をハック: termios 解剖と raw mode の正体"
---

:::message
本連載で書くコード一式は **[GitHub: Daiki-Iijima/c-game-deepdive](https://github.com/Daiki-Iijima/c-game-deepdive)** にあります。本文中で `01_snake/step1_termios/sN_*/main.c` のように参照する path はすべてそのリポ内のファイルです。
:::


![demo placeholder](https://placehold.co/800x300?text=arrow+keys+move+%40+across+screen+%28asciinema%29)

## はじめに — Snake の頭が動かない

普通の C プログラムでキー入力を取ると、こうなります。

```c
char c;
scanf("%c", &c);
```

これだと **Enter を押すまで** 何も読めません。 Snake のような「押した瞬間に動く」 ゲームには使えない。 でも `scanf` が悪いわけではなく、 **カーネルが入力を行単位で溜めてから渡している** からです。

この章では、 その「溜める」 を解除する **termios の raw mode** を、 **5 つの小さなステップ** で組み上げます。

ルールは 1 つだけ:

> **1 step = 1 機能追加 → 必ずビルドして動かして、 動作を確認してから次の step に進む。**

完成版コードを最初から見せられても、 どのビットが何を変えているかは分かりません。 1 ビットずつ落として、 そのたびにターミナルの体感がどう変わるかを **手で覚える** のが本章の目的です。

## 本章のロードマップ

| step | dir | やること | 治った痛み | 残る痛み |
|------|-----|----------|------------|----------|
| 1/5 | `s1_scanf` | termios 不使用の `scanf` 版 | — | Enter 押さないと反応しない |
| 2/5 | `s2_canon` | `ICANON` OFF | Enter 不要 | キーが画面に echo |
| 3/5 | `s3_echo`  | + `ECHO` OFF | 静か | Ctrl-C で死ぬ |
| 4/5 | `s4_isig`  | + `ISIG` OFF | Ctrl-C を奪える | 異常終了で端末壊れる |
| 5/5 | `s5_full`  | + `IXON`/`ICRNL`/`OPOST` & `atexit`/`sigaction` & 矢印キーパース | 復元保証、 Snake の頭が動く | — |

各 step は前 step の **小さな上乗せ** です。 差分を眺めたいときは:

```sh
diff -u 01_snake/step1_termios/s2_canon/main.c \
        01_snake/step1_termios/s3_echo/main.c
```

各 step のセクションは次のフォーマットで進めます:

1. **ゴール** — この step で何を達成するか
2. **書くコード** (or 前 step との diff)
3. **ビルドして動かす**
4. **動作確認** — ✅ こう動けば成功、 ❌ ここがまだ痛い
5. **何が起きたか** — 仕組みの解説
6. **→ 次へ**

それでは Step 1 から。


## Step 1/5: scanf の限界を体感する

### ゴール
termios を一切使わず、 ふつうの `scanf` で 1 文字読むだけのプログラムを書いて、 **「Enter を押さないと何も返ってこない」 痛み** を体に刻みます。 ここがスタート地点。

### 書くコード

```c:01_snake/step1_termios/s1_scanf/main.c
#include <stdio.h>

int main(void) {
    printf("press a key then Enter: ");
    fflush(stdout);

    char c;
    if (scanf(" %c", &c) != 1) {
        fprintf(stderr, "scanf failed\n");
        return 1;
    }
    printf("you pressed: '%c' (0x%02X)\n", c, (unsigned char)c);
    return 0;
}
```

`#include <termios.h>` も `tcgetattr` も登場しません。 ふつうの C プログラムです。

### ビルドして動かす

```sh
cd 01_snake/step1_termios/s1_scanf
make run
```

### 動作確認

- ✅ `press a key then Enter:` が表示される
- ❌ キーを 1 つ押しても **何も起きない** ← これが痛み
- ✅ **Enter** を押した瞬間に `you pressed: 'a' (0x61)` のような行がドカっと出る

**ここで一度試してほしいこと**: `abc` と打って Enter を押すと、 何が表示されますか? 最初の 1 文字 (`a`) だけが取れて、 `bc\n` はバッファに残っているはずです。 つまりカーネルは **既に全部受け取っている**。 ただプログラムに渡してこないだけ。

### 何が起きたか

カーネルの **line discipline** が、 端末の入力を「行が完成するまで (= Enter が来るまで)」 内部バッファに溜めています。 `scanf` (中身は `read(2)`) はカーネルからバッファを受け取って初めて 1 文字を返す。

このモードを **canonical mode** (= ICANON ON) と呼びます。 `Backspace` で 1 文字消えるのも、 `Ctrl-U` で行が消えるのも、 すべて line discipline の仕事です。

### → 次へ

ゲームには論外なので、 [Step 2/5](#step-25-icanon-を-off-にする--termios-構造体登場) で `ICANON` ビットを落とします。


## Step 2/5: ICANON を OFF にする — termios 構造体登場

### ゴール
**行編集をやめさせて、 押した瞬間に `read` が返る** ようにします。 ここから termios が初登場。

### termios とは (最小限の予習)

`struct termios` は端末挙動の **ON/OFF スイッチ集** です。 フィールドはたくさんありますが、 本章で触るのは次の 4 つだけ:

| フィールド | 役割 | 本章で触るビット |
|-----------|------|-----------------|
| `c_lflag` | ローカル (line discipline 本体) | `ICANON`, `ECHO`, `ISIG` |
| `c_iflag` | 入力モード | `IXON`, `ICRNL` (Step 5) |
| `c_oflag` | 出力モード | `OPOST` (Step 5) |
| `c_cc[]`  | 特殊文字 | `VMIN`, `VTIME` |

手順はこの 3 つを覚えればよい:

1. `tcgetattr(STDIN_FILENO, &orig)` で現状を退避
2. コピーの c_lflag から `ICANON` を落として `tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw)` で適用
3. 終了前に `tcsetattr(&orig)` で手動復元

### 書くコード

```c:01_snake/step1_termios/s2_canon/main.c
#include <stdio.h>
#include <termios.h>
#include <unistd.h>

int main(void) {
    struct termios orig;
    if (tcgetattr(STDIN_FILENO, &orig) == -1) {
        perror("tcgetattr"); return 1;
    }

    struct termios raw = orig;
    raw.c_lflag &= (tcflag_t)~ICANON;   /* 行編集 OFF (= 1 byte で read が返る) */
    raw.c_cc[VMIN]  = 1;                 /* 最低 1 byte 来るまでブロック */
    raw.c_cc[VTIME] = 0;
    if (tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw) == -1) {
        perror("tcsetattr"); return 1;
    }

    printf("press a key (no Enter needed): ");
    fflush(stdout);

    char c;
    ssize_t n = read(STDIN_FILENO, &c, 1);

    tcsetattr(STDIN_FILENO, TCSAFLUSH, &orig);   /* 手で復元 */

    if (n != 1) {
        fprintf(stderr, "\nread failed\n");
        return 1;
    }
    printf("\nyou pressed: '%c' (0x%02X)\n", c, (unsigned char)c);
    return 0;
}
```

:::message
**VMIN=1 / VTIME=0 を選んだ理由**: この step は「キーを押すまで待つ」 ことを示したいので、 `read` がブロックする設定 (= 最低 1 byte 来るまで返らない) にします。 Step 5 では「60 fps メインループを回したい」 ため `VMIN=0` (即時 return) に切り替えます。
:::

### ビルドして動かす

```sh
cd 01_snake/step1_termios/s2_canon
make run
```

### 動作確認

- ✅ `press a key (no Enter needed):` の直後、 **1 文字押した瞬間** に `you pressed: 'a' (0x61)` が出る
- ❌ ただし押したキーも **画面に echo されて見える** ← これがまだ痛い
- ✅ プログラム終了後、 シェルに戻ってもちゃんと入力できる (= `tcsetattr(&orig)` が効いている)

### 何が起きたか

`ICANON` ビットを落とした瞬間、 line discipline は「Enter まで待つ」 のをやめます。 read は最初の 1 byte で返ってきます。

ただし **`ECHO`** はまだ ON のままなので、 押したキーをカーネルが勝手に STDOUT にコピーしています。 ゲーム中に W/A/S/D を連打したら画面が文字で埋まる... これでは話にならない。

### → 次へ

[Step 3/5](#step-35-echo-を-off-にする--diff-で読む練習) で `ECHO` も落として静かな世界を作ります。


## Step 3/5: ECHO を OFF にする — diff で読む練習

### ゴール
押したキーが画面に echo されない **静かな世界** を作ります。 ここから先は **前の step との差分** を読む練習も兼ねて、 diff を前面に出します。

### Step 2 との差分

落とすビットを 1 個増やすだけ:

```diff:01_snake/step1_termios/s3_echo/main.c
-    raw.c_lflag &= (tcflag_t)~ICANON;
+    /* Step 2 からの差分: ECHO も落とす */
+    raw.c_lflag &= (tcflag_t)~(ICANON | ECHO);
```

静かになったことを **連打で実感する** ため、 1 回 read で終わらずループにします。 `q` で抜けます。

```c:01_snake/step1_termios/s3_echo/main.c (抜粋)
printf("type keys (q to quit). nothing should echo to the screen:\r\n");
fflush(stdout);

for (;;) {
    char c;
    ssize_t n = read(STDIN_FILENO, &c, 1);
    if (n != 1) break;
    if (c == 'q') break;
    printf("  got: '%c' (0x%02X)\r\n", c, (unsigned char)c);
    fflush(stdout);
}
```

全文は `s3_echo/main.c` を参照。

### ビルドして動かす

```sh
cd 01_snake/step1_termios/s3_echo
make run
```

### 動作確認

- ✅ `a` を 10 回連打しても画面が荒れない (= OS は黙る)
- ✅ 自分の `printf` で書いた `got: 'a' (0x61)` 行だけが見える
- ✅ `q` で正常終了、 シェルが正常に戻る
- ❌ ただし **Ctrl-C を押すと SIGINT で死ぬ** ← まだ痛い

### 何が起きたか

`ECHO` は line discipline の「入力をそのまま STDOUT に書き戻す」 機能。 OFF にすると **キー入力が画面に出るのは自分が `printf` / `write` したときだけ** になります。 ここでようやくゲーム的な画面の前提が整います。

ただし `ISIG` がまだ ON なので、 Ctrl-C / Ctrl-Z / Ctrl-\ はカーネルが signal に変換してプロセスに飛ばしてきます。

### → 次へ

[Step 4/5](#step-45-isig-を-off-にして-ctrl-c-を奪う) で `ISIG` を落として Ctrl-C を **自分のキー** にします。


## Step 4/5: ISIG を OFF にして Ctrl-C を奪う

### ゴール
Ctrl-C が SIGINT に変換されなくなり、 **0x03 のただのバイト** として `read` に届くようにします。 キーボードがカーネルから自分に渡される瞬間です。

### Step 3 との差分

```diff:01_snake/step1_termios/s4_isig/main.c
-    raw.c_lflag &= (tcflag_t)~(ICANON | ECHO);
+    /* Step 3 からの差分: ISIG も落とす */
+    raw.c_lflag &= (tcflag_t)~(ICANON | ECHO | ISIG);
```

ループ側で `0x03` を quit キーとして扱います:

```c:01_snake/step1_termios/s4_isig/main.c (抜粋)
for (;;) {
    char c;
    ssize_t n = read(STDIN_FILENO, &c, 1);
    if (n != 1) break;

    /* 0x03 = Ctrl-C のバイト。 ISIG が OFF なので SIGINT にならず素のバイトで届く */
    if (c == 'q' || c == 0x03) {
        printf("\r\nquit by %s\r\n",
               (c == 0x03) ? "Ctrl-C (0x03)" : "'q'");
        break;
    }
    printf("  got: '%c' (0x%02X)\r\n",
           (c >= 32 && c < 127) ? c : '?',
           (unsigned char)c);
    fflush(stdout);
}
```

### ビルドして動かす

```sh
cd 01_snake/step1_termios/s4_isig
make run
```

### 動作確認

- ✅ 普通のキー (`a`) は `got: 'a' (0x61)` で表示される
- ✅ **Ctrl-C** を押しても **プロセスは死なず**、 `quit by Ctrl-C (0x03)` と表示して綺麗に終了する
- ✅ `q` で抜けても同様
- ❌ ただし `kill -9 <pid>` や segfault で死ぬと **端末が raw mode のまま残る** ← 最後の痛み

### 痛みを実際に体験する (やや危険)

別のターミナルから:

```sh
# 別ターミナルで s4_isig を起動して PID を確認
make run &
# 例えば PID が 12345 なら
kill -9 12345
```

その後、 起動していた側のターミナルで何か入力してみてください。 タイプしても文字が表示されない、 Enter が効かない、 という壊れた状態になっているはずです (`ICANON` も `ECHO` も OFF のまま放置されているため)。

復旧方法:

```sh
reset
# または見えないけど押す:
stty sane
```

これが「raw mode のまま死ぬ」 事故です。 ユーザーには体験させたくない。

### 何が起きたか

`ISIG` は line discipline の「**特定のバイトを signal に変換する**」 機能:

| バイト | 元の意味 | ISIG OFF にすると |
|--------|---------|-------------------|
| `0x03` | SIGINT (Ctrl-C) | そのまま read に届く |
| `0x1A` | SIGTSTP (Ctrl-Z) | そのまま read に届く |
| `0x1C` | SIGQUIT (Ctrl-\) | そのまま read に届く |

OFF にした瞬間、 Ctrl-C は「ただのバイト」 になります。 ゲーム中に Ctrl-C を「ポーズ」 として再割り当てしたいなら、 これが土台になります (本章の演習で扱います)。

### → 次へ

[Step 5/5](#step-55-atexit--sigaction-で端末を救う) で **異常終了でも復元される完成版** を作ります。 加えて矢印キーのパースとメインループも導入して、 ようやく `@` が動きます。


## Step 5/5: atexit + sigaction で端末を救う

### ゴール
**どんな終わり方をしても端末が必ず復元される完成版**。 さらに 60 fps メインループ、 矢印キーパース、 `@` の移動描画を加えます。 これで Snake の頭が動くところまで到達。

### 追加する 6 つの要素

1. **`atexit(restore)`** — `return from main` / `exit()` で必ず呼ぶ
2. **`sigaction(SIGINT/SIGTERM, ...)`** — signal で殺されても復元
3. **`IXON` OFF** — Ctrl-S/Q によるフロー制御を止める
4. **`ICRNL` OFF** — Enter を `\r` のまま取得
5. **`OPOST` OFF** — 出力後処理 (`\n` → `\r\n`) を止める
6. **`VMIN=0 / VTIME=0`** — ノンブロッキング read (Step 2〜4 の `VMIN=1` から変更)

### コード (主要部分)

```c:01_snake/step1_termios/s5_full/main.c (抜粋)
static struct termios g_orig;
static int            g_raw = 0;

static void restore(void) {
    if (!g_raw) return;
    tcsetattr(STDIN_FILENO, TCSAFLUSH, &g_orig);
    const char seq[] = "\x1b[?25h\x1b[2J\x1b[H";   /* カーソル表示 + クリア */
    (void)write(STDOUT_FILENO, seq, sizeof(seq) - 1);
    g_raw = 0;
}

static void on_signal(int sig) {
    restore();
    signal(sig, SIG_DFL);
    raise(sig);                 /* DFL に戻して再送 = 正しい終了コードで死ぬ */
}

static void enter_raw(void) {
    tcgetattr(STDIN_FILENO, &g_orig);
    atexit(restore);                          /* 正常終了の保険 */

    struct sigaction sa = {0};
    sa.sa_handler = on_signal;
    sa.sa_flags = SA_RESTART;
    sigemptyset(&sa.sa_mask);
    sigaction(SIGINT,  &sa, NULL);            /* signal の保険 */
    sigaction(SIGTERM, &sa, NULL);

    struct termios raw = g_orig;
    raw.c_lflag &= (tcflag_t)~(ICANON | ECHO | ISIG);
    raw.c_iflag &= (tcflag_t)~(IXON | ICRNL);
    raw.c_oflag &= (tcflag_t)~(OPOST);
    raw.c_cc[VMIN]  = 0;                      /* ← Step 2〜4 は 1 だった */
    raw.c_cc[VTIME] = 0;
    tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw);
    g_raw = 1;

    const char hide[] = "\x1b[?25l\x1b[2J\x1b[H";
    (void)write(STDOUT_FILENO, hide, sizeof(hide) - 1);
}
```

全文 (矢印キーパース、 メインループ含む) は `s5_full/main.c` を参照。

### `VMIN=0` への切り替えが必要な理由

メインループは「キーが押されなくても画面を 60 fps で描き続ける」 必要があります。 Step 2〜4 のように `VMIN=1` でブロックしてしまうと、 ユーザーが何も押さない間は描画も時間も止まり、 蛇が動かない世界になります。 そこで「入力が無ければ 0 byte で即返ってこい」 と命じるのが `VMIN=0`。

### 矢印キーの正体: ESC `[` `A/B/C/D` の 3 byte

`s5_full/main.c` には **矢印キーのパース処理** が初登場します。 なぜ s2〜s4 では出てこなかったか? — Step 2〜4 はキーを 1 byte ずつ眺める教育用ループで、 押されたバイトをそのまま表示していました。 そこには「矢印キー」 という独立した存在は無く、 ESC `[` `A` の 3 byte が **連続して** 飛んできていただけです。

試しに `s4_isig` を立ち上げて矢印キー↑ を押すと、 3 行が一気に出ます:

```
  got: '?' (0x1B)   ← ESC
  got: '[' (0x5B)
  got: 'A' (0x41)   ← これで初めて「上矢印」
```

Snake で蛇を動かすには、 この 3 byte をひとまとめに「上矢印」 として解釈する必要があります。 それが s5_full の `read_key()` 関数の役目。

### ビルドして動かす

```sh
cd 01_snake/step1_termios/s5_full
make run
```

### 動作確認 (4 つの終了経路すべて)

| 終了方法 | 期待される挙動 |
|----------|---------------|
| ✅ `q` で抜ける | `atexit(restore)` → 端末復元、 シェル正常 |
| ✅ Ctrl-C で抜ける | `on_signal` → restore → DFL に戻して再 raise → プロセスは exit code 130 で死ぬが、 シェルは正常 |
| ✅ 別端末から `kill -TERM <pid>` | 同上 (`SIGTERM` も sigaction 済み) |
| ⚠️ `kill -9 <pid>` / segfault | 復元できない (SIGKILL は捕まえられない) — ここは塞げない |

`kill -9` だけは原理的に捕まえられないので、 そこは諦め (どんなプログラムも対処不可能)。 普通の事故はこれで全部塞げます。

### 何が起きたか — 復元タイミングの 3 経路

```
                ┌──────────────────────────┐
プロセス開始 →  │ enter_raw()              │
                │   tcgetattr(&g_orig)     │
                │   atexit(restore)        │ ← (1) 正常終了の保険
                │   sigaction(SIGINT, ..)  │ ← (2) signal の保険
                │   sigaction(SIGTERM, ..) │
                │   tcsetattr(&raw)        │
                └──────────────────────────┘
                            │
                            ▼
                ┌──────────────────────────┐
                │ メインループ (60 fps)    │
                │   read(VMIN=0) → KEY     │
                │   draw_at(@)             │
                │   msleep(16)             │
                └──────────────────────────┘
                            │
        ┌───────────────────┼───────────────────┐
        ▼                   ▼                   ▼
   q で break          SIGINT 受信         SIGKILL 受信
        │                   │                   │
        ▼                   ▼                   ▼
   return → exit()    on_signal()          ❌ 復元不可
   → atexit(restore)  → restore()             (諦める)
                      → signal(SIG_DFL)
                      → raise(SIGINT)
                      → 死ぬ (exit 130)
```

`Ctrl-C` の経路で `signal(sig, SIG_DFL); raise(sig);` する **signal イディオム** は、 第 8 章 (Roguelike signal) でまた使います。

:::message alert
**ここで使った `tcsetattr` は、 厳密には async-signal-safe ではありません** (POSIX `signal-safety(7)`)。 「実用上は端末を救えるので使う」 という割り切りで採用しています。

参考までに、 signal ハンドラから呼んで安全なのは概ね次の関数たち (POSIX.1-2017 列挙):

- ✅ `write(2)`, `read(2)`, `_exit(2)`, `signal(2)`, `raise(3)`, `kill(2)`, `sigaction(2)`, `sigprocmask(2)`
- ✅ `time(2)`, `clock_gettime(2)` (Linux では安全リストに含まれる)
- ❌ `printf(3)`, `fprintf(3)`, `malloc(3)`, `free(3)`, `tcsetattr(3)` — **本章はこの ❌ を意図的に踏んでいます**

「signal ハンドラからは `write(2)` 中心、 `printf` や `malloc` は呼ばない」 を頭の隅に置いてください。 何が安全で何が不安全かは第 8 章で正面から扱います。
:::

### → ここまでで完成

5 step、 5 ビット、 4 経路の復元。 これが本章のすべてです。 完成版で `@` を動かしたら、 章末の **観察** と **メンタルモデル** で全体を俯瞰します。


## 観察する: strace で全行程を見る

完成版 (`s5_full`) に対して syscall を覗きます。

```sh
strace -e trace=ioctl,read,write ./snake_step1_s5
```

:::details strace と `-e trace=...` の解説
- `strace` はプロセスの **すべての syscall を覗き見る** デバッグツール。 内部で `ptrace(PTRACE_SYSCALL, ...)` を使い、 syscall 入口と出口で対象プロセスを止めて引数と戻り値を読む。
- 何もフィルタを付けないと膨大に出力されるので、 `-e trace=...` で絞り込む:
  - `-e trace=ioctl,read,write` のように **カンマ区切りで syscall 名を列挙**。
  - `-e trace=%file` (ファイル系全部)、 `-e trace=%network` (ネットワーク系全部) のような **シンセティックグループ** もある。
- 他の便利フラグ:
  - `-f`: 子プロセスも追跡 (`fork` 後の `clone` で生まれた子)。 第 9 章で使います。
  - `-c`: 終了時に syscall ごとの **回数と消費時間の集計** を表示。
  - `-p PID`: 既に動いているプロセスにアタッチ。
  - `-o trace.log`: stdout ではなくファイルに記録。
- 注意: Docker コンテナ内で使うには `seccomp:unconfined` 権限が要る (本連載の `docker/compose.yml` で許可済み)。
:::

`tcsetattr` の正体が `ioctl(0, TCSETSF, {...})` であること、 `read(0, buf, 1)` が **即座に 0 byte を返してくる** (= s5_full の `VMIN=0` ノンブロッキングが効いている = 60 fps メインループが回せる) ことが目で確認できます。 もし `s2_canon` (`VMIN=1`) を strace すると、 ここの `read(0, "", 1) = 0` の行が出ず、 キーを押すまで止まったままになります。

```
ioctl(0, TCGETS, {c_iflag=ICRNL|IXON, c_oflag=OPOST|ONLCR, c_lflag=ICANON|ECHO|ISIG, ...}) = 0
ioctl(0, TCSETSF, {c_iflag=0, c_oflag=0, c_lflag=CS8, c_cc[VMIN]=0, c_cc[VTIME]=0}) = 0
read(0, "", 1)                          = 0
read(0, "", 1)                          = 0
read(0, "\33", 1)                       = 1
read(0, "[", 1)                         = 1
read(0, "A", 1)                         = 1   ← 矢印キー上!
write(1, "\33[10;20H ", 9)              = 9
```

`\33[A` (3 byte シーケンス) が **矢印キーの正体** です。 raw mode では「上矢印」 というキーは無く、 ESC `[` `A` の 3 文字が連続して飛んできます。


## メンタルモデルを整理する

```
[ターミナル] ←→ [カーネル line discipline] ←→ [プロセス stdin]
                  ICANON / ECHO / IXON …
                  ↑ termios の各ビットがここのスイッチ
```

普段「ターミナル」 と呼んでいる体験は、 **カーネルの中の line discipline が大半を提供しています**。 Snake のために termios を弄るのは、 ゲーム特有のテクニックではなく、 **カーネルの干渉を 1 段階剥がしている** だけです。

5 step で 1 ビットずつ剥がしてきたので、 どのビットが何を提供しているか **手の感覚** で覚えたはずです。 これが本章のいちばんの収穫です。


## 演習

- **Easy**: `s3_echo/main.c` を改造して `ECHO` だけを残す (= ICANON OFF / ECHO ON) raw mode に入り、 押したキーが画面に二重 (= 自分の `got: ...` と OS の echo) で出てしまうことを確認。 なぜ「ゲームに不適切」 か言語化する。

- **Med**: `s4_isig/main.c` の入力処理を、 ESC 1 文字だけ届いた場合 (3 byte 揃わない場合) に対応させるため、 `VTIME=1` (0.1 秒タイムアウト) に変更し、 ESC キー単体を「終了」 として扱うように改造。

- **Hard**: `s5_full/main.c` で `Ctrl-C` を「ゲームのポーズ」 に再割り当て。 次の二通りを実装し挙動の違いを観察する:
  - **A**: 本章のまま (`ISIG` OFF) で、 `read` が返した `0x03` バイトを自前でポーズ動作に変換する
  - **B**: `ISIG` を **ON** に戻し、 Ctrl-C はカーネルが SIGINT に変えて飛ばす経路に任せ、 `SIGINT` ハンドラ側でポーズ動作を起こす

  どちらが「Ctrl-C 連打」 に強いか、 復元処理 (raw mode 解除と再 enter) の責務がどう分かれるか、 をレポート 1 段落でまとめる。


## 次章では
第 2 章は **蛇の体をどう表現するか**。 固定長の `char map[24][80]` を確保して、 頭と体を配列に書き込みます。 `sizeof` と `offsetof` を使って **1 マスが何 byte 占有しているか** を覗き、 `struct __attribute__((packed))` の効果を実測します。 スタック領域の概念が登場します。
