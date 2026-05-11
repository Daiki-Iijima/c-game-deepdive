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

これだと **Enter を押すまで** 何も読めません。
Snake のような「押した瞬間に動く」 ゲームには使えない。
でも `scanf` が悪いわけではなく、 **カーネルが入力を行単位で溜めてから渡している** からです。

この章では、 その「溜める」 を解除する **termios の raw mode** を、 **5 つの小さなステップ** で組み上げます。

ルールは 1 つだけ:

> **1 step = 1 機能追加 → 必ずビルドして動かして、 動作を確認してから次の step に進む。**

完成版コードを最初から見せられても、 どのビットが何を変えているかは分かりません。
1 ビットずつ落として、 そのたびにターミナルの体感がどう変わるかを **手で覚える** のが本章の目的です。

## 本章のロードマップ

| step | dir | やること | 治った痛み | 残る痛み |
|------|-----|----------|------------|----------|
| 1/5 | `s1_scanf` | termios 不使用の `scanf` 版 | — | Enter 押さないと反応しない |
| 2/5 | `s2_canon` | `ICANON` OFF | Enter 不要 | キーが画面に echo |
| 3/5 | `s3_echo`  | + `ECHO` OFF | 静か | Ctrl-C で死ぬ |
| 4/5 | `s4_isig`  | + `ISIG` OFF | Ctrl-C を奪える | 異常終了で端末壊れる |
| 5/5 | `s5_full`  | + `IXON`/`ICRNL`/`OPOST` & `atexit`/`sigaction` & 矢印キーパース | 復元保証、 Snake の頭が動く | — |

各 step は前 step の **小さな上乗せ** です。
差分を眺めたいときは:

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
termios を一切使わず、 ふつうの `scanf` で 1 文字読むだけのプログラムを書いて、 **「Enter を押さないと何も返ってこない」 痛み** を体に刻みます。
ここがスタート地点。

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

`#include <termios.h>` も `tcgetattr` も登場しません。
ふつうの C プログラムです。

:::details `<stdio.h>` と `printf` / `scanf` / `fflush` / `fprintf` の最小解説
- **`<stdio.h>`** — 標準入出力ライブラリのヘッダ。
stdin/stdout/stderr の `FILE*` と、 `printf`/`scanf`/`fopen`/`fread`/`fwrite` 等を提供。
内部で `FILE*` 構造体がバッファを抱えているので、 `read(2)`/`write(2)` syscall と違って **バッファリングされた I/O** になります。
- **`printf("...", ...)`** — 書式付き出力。
内部で stdout に書きます。
stdout が **行バッファ** (端末の場合) なので、 **改行 `\n` が来るまで実際の `write(2)` syscall は呼ばれません**。
だから改行のないプロンプト (`"press a key:"`) はそのままだと画面に出ません。
- **`fflush(stdout)`** — 上記バッファを **強制的に flush** して中身を `write(2)` に流す。
改行なしのプロンプトを表示させるために必須。
`fflush(NULL)` で全 stdio FILE* を一括 flush できます。
- **`scanf(" %c", &c)`** — 書式付き入力。
`" %c"` の **先頭スペース** は「空白文字 (改行/タブ/スペース) を読み飛ばす」 指示。
これを書かないと、 前回の read で残った `\n` を拾って即返ってきてしまう罠があります。
戻り値は **正しく読めた項目数**。
- **`fprintf(stderr, "...")`** — stderr に書く版。
stderr は **無バッファ** (端末の場合) なので即時表示されます。
エラーメッセージは慣習として stderr に出します (シェルの `2>` でリダイレクトできるよう)。
:::

### ビルドして動かす

```sh
cd 01_snake/step1_termios/s1_scanf
make run
```

### 動作確認

- ✅ `press a key then Enter:` が表示される
- ❌ キーを 1 つ押しても **何も起きない** ← これが痛み
- ✅ **Enter** を押した瞬間に `you pressed: 'a' (0x61)` のような行がドカっと出る

**ここで一度試してほしいこと**: `abc` と打って Enter を押すと、 何が表示されますか? 最初の 1 文字 (`a`) だけが取れて、 `bc\n` はバッファに残っているはずです。
つまりカーネルは **既に全部受け取っている**。
ただプログラムに渡してこないだけ。

### 何が起きたか

カーネルの **line discipline** が、 端末の入力を「行が完成するまで (= Enter が来るまで)」 内部バッファに溜めています。
`scanf` (中身は `read(2)`) はカーネルからバッファを受け取って初めて 1 文字を返す。

このモードを **canonical mode** (= ICANON ON) と呼びます。
`Backspace` で 1 文字消えるのも、 `Ctrl-U` で行が消えるのも、 すべて line discipline の仕事です。

### → 次へ

ゲームには論外なので、 [Step 2/5](#step-25-icanon-を-off-にする--termios-構造体登場) で `ICANON` ビットを落とします。


## Step 2/5: ICANON を OFF にする — termios 構造体登場

### ゴール
**行編集をやめさせて、 押した瞬間に `read` が返る** ようにします。
ここから termios が初登場。

### termios とは (最小限の予習)

`struct termios` は端末挙動の **ON/OFF スイッチ集** です。
フィールドはたくさんありますが、 本章で触るのは次の 4 つだけ:

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
**VMIN=1 / VTIME=0 を選んだ理由**: この step は「キーを押すまで待つ」 ことを示したいので、 `read` がブロックする設定 (= 最低 1 byte 来るまで返らない) にします。
Step 5 では「60 fps メインループを回したい」 ため `VMIN=0` (即時 return) に切り替えます。
:::

:::details `<termios.h>` / `<unistd.h>` ヘッダの最小解説
- **`<termios.h>`** — POSIX 端末制御。
`struct termios`、 `tcgetattr/tcsetattr`、 ビットフラグ定数 (`ICANON`/`ECHO`/`ISIG`/...)、 `c_cc[]` 用の添字定数 (`VMIN`/`VTIME`) を提供。
標準 C ではなく **POSIX** の世界の住人。
Linux/macOS では使えるが Windows ではそのままは使えません。
- **`<unistd.h>`** — POSIX 環境の基本。
`read`/`write`/`close`/`dup` 等の syscall ラッパと、 `STDIN_FILENO` (=0), `STDOUT_FILENO` (=1), `STDERR_FILENO` (=2) の定数を提供。
- 本章の Makefile で `-D_POSIX_C_SOURCE=200809L` を渡しているのは、 glibc にこれらの POSIX 拡張を有効化させるためです (素のままだと `sigaction` 等が見えない処理系がある)。
:::

:::details `tcgetattr` / `tcsetattr` のシグネチャと TCSAFLUSH
```c
int tcgetattr(int fd, struct termios *termios_p);
int tcsetattr(int fd, int optional_actions, const struct termios *termios_p);
```
- **`tcgetattr`**: 指定 fd (端末) の現在の termios 設定を `termios_p` に書き出す。
失敗時 -1、 `errno` に詳細。
fd が端末でないと `ENOTTY` (= `Inappropriate ioctl for device`) になります (パイプ越しに動かしたときの典型エラー)。
- **`tcsetattr`**: 設定を適用する。
第 2 引数の選択:
  - **`TCSANOW`** — 即時適用
  - **`TCSADRAIN`** — 出力キューが捌けてから適用 (出力途中に設定を変えない)
  - **`TCSAFLUSH`** — 出力キューを捌き、 **入力キューを破棄してから適用** ← 本章で使う
- 本章で `TCSAFLUSH` を選ぶ理由: 退避前にキューに残っていたバイトを raw mode 突入後に拾ってしまう事故を防げる (例: ターミナルが先読みで送ってきた ESC シーケンスの残骸など)。
- 「正体」 は ioctl: `tcsetattr` は内部で `ioctl(fd, TCSETSF, ...)` を呼ぶただのラッパ。
strace で見ると ioctl で出てきます (章末で観察します)。
:::

:::details `read(2)` syscall と `perror`
```c
ssize_t read(int fd, void *buf, size_t count);
void    perror(const char *s);
```
- **`read`**: count バイト読む syscall。
stdio (`fgets`/`getc`) と違ってバッファリング無し。
戻り値:
  - **正**: 実際に読んだバイト数 (要求した count 未満のこともある)
  - **0**: EOF (パイプ閉じ等)。
  端末でも `VMIN=0/VTIME=0` で「入力なし」 のときに 0 が返る
  - **負 (-1)**: エラー。
  `errno` に詳細 (`EINTR`, `EAGAIN`, ...)
- `ssize_t` は signed の `size_t` (負を表せる型)。
- **`perror("label")`**: 直前の `errno` を `strerror(errno)` 経由で読みやすい文字列にして stderr に出す。
`<stdio.h>` 由来 (POSIX ではなく標準 C)。
例えば `tcgetattr` 失敗時 `perror("tcgetattr")` で `tcgetattr: Inappropriate ioctl for device` のように出る。
:::

:::details `VMIN` / `VTIME` の組み合わせ早見表
`c_cc[VMIN]` と `c_cc[VTIME]` の値で `read(2)` の挙動が決まります (ICANON OFF のとき有効):

| `VMIN` | `VTIME` | 挙動 |
|--------|---------|------|
| 0 | 0 | **即時 return** (= ノンブロッキング)。 ある分だけ読む、 無ければ 0 byte で帰る ← s5_full |
| 0 | >0 | タイムアウト付きノンブロッキング。 VTIME × 0.1 秒待って何か来れば読み、 来なければ 0 byte |
| >0 | 0 | **ブロック**。 最低 VMIN byte 来るまで返らない ← s2_canon, s3_echo, s4_isig |
| >0 | >0 | 最初の 1 byte が来てから VTIME × 0.1 秒以内に追加が来なければ return (= キーシーケンスのタイムアウト用) |

ESC シーケンス (`\x1b[A`) を「届かなかった ESC 単独キー」 と区別したい場合は最後のモード (`VMIN=1, VTIME=1`) が便利。
本章の演習 Med で扱います。
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

`ICANON` ビットを落とした瞬間、 line discipline は「Enter まで待つ」 のをやめます。
read は最初の 1 byte で返ってきます。

ただし **`ECHO`** はまだ ON のままなので、 押したキーをカーネルが勝手に STDOUT にコピーしています。
ゲーム中に W/A/S/D を連打したら画面が文字で埋まる... これでは話にならない。

### → 次へ

[Step 3/5](#step-35-echo-を-off-にする--diff-で読む練習) で `ECHO` も落として静かな世界を作ります。


## Step 3/5: ECHO を OFF にする — diff で読む練習

### ゴール
押したキーが画面に echo されない **静かな世界** を作ります。
ここから先は **前の step との差分** を読む練習も兼ねて、 diff を前面に出します。

### Step 2 との差分

落とすビットを 1 個増やすだけ:

```diff:01_snake/step1_termios/s3_echo/main.c
-    raw.c_lflag &= (tcflag_t)~ICANON;
+    /* Step 2 からの差分: ECHO も落とす */
+    raw.c_lflag &= (tcflag_t)~(ICANON | ECHO);
```

静かになったことを **連打で実感する** ため、 1 回 read で終わらずループにします。
`q` で抜けます。

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

`ECHO` は line discipline の「入力をそのまま STDOUT に書き戻す」 機能。
OFF にすると **キー入力が画面に出るのは自分が `printf` / `write` したときだけ** になります。
ここでようやくゲーム的な画面の前提が整います。

ただし `ISIG` がまだ ON なので、 Ctrl-C / Ctrl-Z / Ctrl-\ はカーネルが signal に変換してプロセスに飛ばしてきます。

### → 次へ

[Step 4/5](#step-45-isig-を-off-にして-ctrl-c-を奪う) で `ISIG` を落として Ctrl-C を **自分のキー** にします。


## Step 4/5: ISIG を OFF にして Ctrl-C を奪う

### ゴール
Ctrl-C が SIGINT に変換されなくなり、 **0x03 のただのバイト** として `read` に届くようにします。
キーボードがカーネルから自分に渡される瞬間です。

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

その後、 起動していた側のターミナルで何か入力してみてください。
タイプしても文字が表示されない、 Enter が効かない、 という壊れた状態になっているはずです (`ICANON` も `ECHO` も OFF のまま放置されているため)。

復旧方法:

```sh
reset
# または見えないけど押す:
stty sane
```

これが「raw mode のまま死ぬ」 事故です。
ユーザーには体験させたくない。

### 何が起きたか

`ISIG` は line discipline の「**特定のバイトを signal に変換する**」 機能:

| バイト | 元の意味 | ISIG OFF にすると |
|--------|---------|-------------------|
| `0x03` | SIGINT (Ctrl-C) | そのまま read に届く |
| `0x1A` | SIGTSTP (Ctrl-Z) | そのまま read に届く |
| `0x1C` | SIGQUIT (Ctrl-\) | そのまま read に届く |

OFF にした瞬間、 Ctrl-C は「ただのバイト」 になります。
ゲーム中に Ctrl-C を「ポーズ」 として再割り当てしたいなら、 これが土台になります (本章の演習で扱います)。

### → 次へ

[Step 5/5](#step-55-atexit--sigaction-で端末を救う) で **異常終了でも復元される完成版** を作ります。
加えて矢印キーのパースとメインループも導入して、 ようやく `@` が動きます。


## Step 5/5: atexit + sigaction で端末を救う

### ゴール
**どんな終わり方をしても端末が必ず復元される完成版**。
さらに 60 fps メインループ、 矢印キーパース、 `@` の移動描画を加えます。
これで Snake の頭が動くところまで到達。

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

:::details `write(2)` / `snprintf` の最小解説
```c
ssize_t write(int fd, const void *buf, size_t count);
int     snprintf(char *buf, size_t size, const char *fmt, ...);
```
- **`write`**: `read` の出力版 syscall。
`printf` 等の stdio と違ってバッファリング無し = **signal handler や緊急復元から呼んでも安全** (async-signal-safe)。
本章では `restore` 関数や `draw_at` で `write(STDOUT_FILENO, ...)` を直に呼んでいます。
戻り値は実際に書いたバイト数 (count 未満のことあり)。
`sizeof(seq) - 1` で末尾の `'\0'` を除いたバイト数を渡すイディオム。
- **`snprintf`**: `printf` 系の安全版。
buf に最大 `size` バイトまで書き、 必ず終端 `'\0'` を付ける。
戻り値は **「もし size 制限が無ければ書き込んだはずのバイト数」** (= 切り詰められたかどうか分かる)。
char バッファに ANSI エスケープシーケンスを組み立てる用途で本章では多用します。
:::

:::details `atexit` / `sigaction` / `raise` / `signal` の最小解説
```c
int  atexit(void (*function)(void));
int  sigaction(int signum, const struct sigaction *act, struct sigaction *oldact);
int  raise(int sig);
void (*signal(int sig, void (*handler)(int)))(int);   /* 古い API */
```
- **`atexit(fn)`**: 正常終了 (`return from main` / `exit(3)`) 時に呼ぶ関数を登録。
最大 32 個までスタックに積めて、 **LIFO 順** で呼ばれます。
`_exit(2)` や signal で死ぬときは呼ばれません (= だから signal handler が別途必要)。
- **`sigaction(sig, &sa, &old)`**: signal handler 登録の **現代版 API**。
古い `signal(2)` より挙動が POSIX で厳密に定義されている (`signal` は処理系依存の歴史的バグがある)。
`struct sigaction` のフィールド:
  - `sa_handler` — ハンドラ関数ポインタ
  - `sa_flags` — `SA_RESTART` (handler 復帰後の syscall 自動再開) など
  - `sa_mask` — handler 実行中にブロックする signal 集合 (`sigemptyset` で空集合に初期化)
- **`raise(sig)`**: 自プロセスに signal を送る (= `kill(getpid(), sig)`)。
本章ではハンドラ内で `signal(sig, SIG_DFL); raise(sig);` イディオムを使い、 **デフォルト動作 (= 死ぬ) を呼び戻している**。
こうすることで終了コードが 128+sig (SIGINT なら 130) と正しくなり、 シェルの `$?` を見るスクリプトが期待通り動きます。
- **`signal(sig, SIG_DFL)`**: 古い API。
ここではハンドラ解除に使うだけなので簡易に流用しています (`sigaction` で `sa_handler=SIG_DFL` でも可)。
- 注意: 本章末の signal-safety 警告にあるとおり、 ハンドラ内で `tcsetattr` を呼ぶのは POSIX 厳密には NG。
本章は「実用上端末を救えるので踏む」 立場。
第 8 章 (Roguelike signal) で安全な書き方を扱います。
:::

:::details `<time.h>` と `nanosleep` (s5_full の `msleep`)
```c
struct timespec { time_t tv_sec; long tv_nsec; };   /* nsec は 1/10億 秒 */
int nanosleep(const struct timespec *req, struct timespec *rem);
```
- s5_full のメインループは `msleep(16)` (≈ 60 fps) でスリープしますが、 中身は `nanosleep` の薄いラッパ:
  ```c
  static void msleep(int ms) {
      struct timespec ts = { ms / 1000, (ms % 1000) * 1000000L };
      nanosleep(&ts, NULL);
  }
  ```
- `tv_nsec` の単位は **ナノ秒 (1/10億 秒)**。
ミリ秒に変換するには `× 1,000,000` (= `1000000L`)。
- 第 2 引数 `rem` は signal で起こされたときの「残り時間」 を書き戻す先。
今回は不要なので NULL。
:::

### `VMIN=0` への切り替えが必要な理由

メインループは「キーが押されなくても画面を 60 fps で描き続ける」 必要があります。
Step 2〜4 のように `VMIN=1` でブロックしてしまうと、 ユーザーが何も押さない間は描画も時間も止まり、 蛇が動かない世界になります。
そこで「入力が無ければ 0 byte で即返ってこい」 と命じるのが `VMIN=0`。

### 矢印キーの正体: ESC `[` `A/B/C/D` の 3 byte

`s5_full/main.c` には **矢印キーのパース処理** が初登場します。
なぜ s2〜s4 では出てこなかったか? — Step 2〜4 はキーを 1 byte ずつ眺める教育用ループで、 押されたバイトをそのまま表示していました。
そこには「矢印キー」 という独立した存在は無く、 ESC `[` `A` の 3 byte が **連続して** 飛んできていただけです。

試しに `s4_isig` を立ち上げて矢印キー↑ を押すと、 3 行が一気に出ます:

```
  got: '?' (0x1B)   ← ESC
  got: '[' (0x5B)
  got: 'A' (0x41)   ← これで初めて「上矢印」
```

Snake で蛇を動かすには、 この 3 byte をひとまとめに「上矢印」 として解釈する必要があります。
それが s5_full の `read_key()` 関数の役目。

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

`kill -9` だけは原理的に捕まえられないので、 そこは諦め (どんなプログラムも対処不可能)。
普通の事故はこれで全部塞げます。

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
**ここで使った `tcsetattr` は、 厳密には async-signal-safe ではありません** (POSIX `signal-safety(7)`)。
「実用上は端末を救えるので使う」 という割り切りで採用しています。

参考までに、 signal ハンドラから呼んで安全なのは概ね次の関数たち (POSIX.1-2017 列挙):

- ✅ `write(2)`, `read(2)`, `_exit(2)`, `signal(2)`, `raise(3)`, `kill(2)`, `sigaction(2)`, `sigprocmask(2)`
- ✅ `time(2)`, `clock_gettime(2)` (Linux では安全リストに含まれる)
- ❌ `printf(3)`, `fprintf(3)`, `malloc(3)`, `free(3)`, `tcsetattr(3)` — **本章はこの ❌ を意図的に踏んでいます**

「signal ハンドラからは `write(2)` 中心、 `printf` や `malloc` は呼ばない」 を頭の隅に置いてください。
何が安全で何が不安全かは第 8 章で正面から扱います。
:::

### → ここまでで完成

5 step、 5 ビット、 4 経路の復元。
これが本章のすべてです。
完成版で `@` を動かしたら、 章末の **観察** と **メンタルモデル** で全体を俯瞰します。


## 観察する: strace で全行程を見る

完成版 (`s5_full`) に対して syscall を覗きます。

```sh
strace -e trace=ioctl,read,write ./snake_step1_s5
```

:::details strace と `-e trace=...` の解説
- `strace` はプロセスの **すべての syscall を覗き見る** デバッグツール。
内部で `ptrace(PTRACE_SYSCALL, ...)` を使い、 syscall 入口と出口で対象プロセスを止めて引数と戻り値を読む。
- 何もフィルタを付けないと膨大に出力されるので、 `-e trace=...` で絞り込む:
  - `-e trace=ioctl,read,write` のように **カンマ区切りで syscall 名を列挙**。
  - `-e trace=%file` (ファイル系全部)、 `-e trace=%network` (ネットワーク系全部) のような **シンセティックグループ** もある。
- 他の便利フラグ:
  - `-f`: 子プロセスも追跡 (`fork` 後の `clone` で生まれた子)。
  第 9 章で使います。
  - `-c`: 終了時に syscall ごとの **回数と消費時間の集計** を表示。
  - `-p PID`: 既に動いているプロセスにアタッチ。
  - `-o trace.log`: stdout ではなくファイルに記録。
- 注意: Docker コンテナ内で使うには `seccomp:unconfined` 権限が要る (本連載の `docker/compose.yml` で許可済み)。
:::

`tcsetattr` の正体が `ioctl(0, TCSETSF, {...})` であること、 `read(0, buf, 1)` が **即座に 0 byte を返してくる** (= s5_full の `VMIN=0` ノンブロッキングが効いている = 60 fps メインループが回せる) ことが目で確認できます。
もし `s2_canon` (`VMIN=1`) を strace すると、 ここの `read(0, "", 1) = 0` の行が出ず、 キーを押すまで止まったままになります。

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

`\33[A` (3 byte シーケンス) が **矢印キーの正体** です。
raw mode では「上矢印」 というキーは無く、 ESC `[` `A` の 3 文字が連続して飛んできます。


## メンタルモデルを整理する

```
[ターミナル] ←→ [カーネル line discipline] ←→ [プロセス stdin]
                  ICANON / ECHO / IXON …
                  ↑ termios の各ビットがここのスイッチ
```

普段「ターミナル」 と呼んでいる体験は、 **カーネルの中の line discipline が大半を提供しています**。
Snake のために termios を弄るのは、 ゲーム特有のテクニックではなく、 **カーネルの干渉を 1 段階剥がしている** だけです。

5 step で 1 ビットずつ剥がしてきたので、 どのビットが何を提供しているか **手の感覚** で覚えたはずです。
これが本章のいちばんの収穫です。


## 演習

- **Easy**: `s3_echo/main.c` を改造して `ECHO` だけを残す (= ICANON OFF / ECHO ON) raw mode に入り、 押したキーが画面に二重 (= 自分の `got: ...` と OS の echo) で出てしまうことを確認。
なぜ「ゲームに不適切」 か言語化する。

- **Med**: `s4_isig/main.c` の入力処理を、 ESC 1 文字だけ届いた場合 (3 byte 揃わない場合) に対応させるため、 `VTIME=1` (0.1 秒タイムアウト) に変更し、 ESC キー単体を「終了」 として扱うように改造。

- **Hard**: `s5_full/main.c` で `Ctrl-C` を「ゲームのポーズ」 に再割り当て。
次の二通りを実装し挙動の違いを観察する:
  - **A**: 本章のまま (`ISIG` OFF) で、 `read` が返した `0x03` バイトを自前でポーズ動作に変換する
  - **B**: `ISIG` を **ON** に戻し、 Ctrl-C はカーネルが SIGINT に変えて飛ばす経路に任せ、 `SIGINT` ハンドラ側でポーズ動作を起こす

  どちらが「Ctrl-C 連打」 に強いか、 復元処理 (raw mode 解除と再 enter) の責務がどう分かれるか、 をレポート 1 段落でまとめる。


## 次章では
第 2 章は **蛇の体をどう表現するか**。
固定長の `char map[24][80]` を確保して、 頭と体を配列に書き込みます。
`sizeof` と `offsetof` を使って **1 マスが何 byte 占有しているか** を覗き、 `struct __attribute__((packed))` の効果を実測します。
スタック領域の概念が登場します。
