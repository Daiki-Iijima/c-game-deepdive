---
title: "第1章 — キー入力をハック: termios 解剖と raw mode の正体"
---

:::message
本連載で書くコード一式は **[GitHub: Daiki-Iijima/c-game-deepdive](https://github.com/Daiki-Iijima/c-game-deepdive)** にあります。本文中で `01_snake/step1_termios/sN_*/main.c` のように参照する path はすべてそのリポ内のファイルです。
:::


![demo placeholder](https://placehold.co/800x300?text=arrow+keys+move+%40+across+screen+%28asciinema%29)

## はじめに
普通の C プログラムでキー入力を取ると、こうなります。


```c
char c;
scanf("%c", &c);
```

これだと **Enter を押すまで** 何も読めません。
Snake のような「押した瞬間に動く」ゲームには使えない。

`scanf` が悪いわけではなく、**カーネルが入力を行単位で溜めてから渡している** からです。

この「溜める」を解除するのが、本章のテーマ **termios の raw mode** です。


## 本章のテーマ: termios

ターミナルは「文字単位」のデバイスのように見えて、実はカーネルの中に **行編集機能** が住んでいます。
`Backspace` で 1 文字消えるのも、`Ctrl-C` で SIGINT が飛ぶのも、`Ctrl-S` で出力が止まるのも、すべてカーネルの **line discipline** がやっています。


`termios` 構造体は、その挙動の ON/OFF スイッチ集です。


```c
struct termios {
    tcflag_t c_iflag;  // 入力モード
    tcflag_t c_oflag;  // 出力モード
    tcflag_t c_cflag;  // 制御モード
    tcflag_t c_lflag;  // ローカルモード (line discipline 本体)
    cc_t     c_cc[NCCS]; // 特殊文字 (VMIN, VTIME, ...)
    /* ... 速度などはここでは触らない ... */
};
```

ゲームのために落としたいスイッチはこのあたりです。


| フラグ | グループ | 意味 | OFF にすると |
|--------|---------|------|--------------|
| `ICANON` | lflag | 行編集 | Enter を待たずに 1 文字届く |
| `ECHO`   | lflag | 入力エコー | 押したキーが画面に出ない |
| `ISIG`   | lflag | Ctrl-C などをシグナル化 | Ctrl-C が `0x03` のバイトとして届く |
| `IXON`   | iflag | Ctrl-S/Q のフロー制御 | Ctrl-S で表示が止まらない |
| `ICRNL`  | iflag | CR→LF 変換 | Enter が `\r` のまま届く |
| `OPOST`  | oflag | 出力後処理 | `\n` が `\r\n` に化けない |
| `VMIN/VTIME` | c_cc | read のブロッキング | ノンブロッキング read が可能 |

本章は **学習目的のため、敢えて `lib/tty.c` を使わず main.c に termios ロジックを全部詰め込みます**。
1 ファイル完結で読み下せる方が、どのフラグが効いているかを追いやすいからです。第 2 章以降はこの実装を `lib/tty.c` 共有に移し、ゲームロジックに集中していきます。

そして本章は **5 ステップ** に分けて段階的に組み上げます。 各 step は独立したディレクトリで `make run` でき、 step 間の **差分 (= 1 ビット落とすだけ)** が「ターミナルから何が剥がれたか」 を直接表しています。

| step | dir | やること | 体感 |
|------|-----|----------|------|
| 1/5 | `s1_scanf` | `scanf` で 1 文字読むだけ | Enter 押さないと反応しない (痛い) |
| 2/5 | `s2_canon` | `ICANON` を OFF | 押した瞬間に届く! でも画面が echo で荒れる |
| 3/5 | `s3_echo`  | + `ECHO` を OFF | 静か。 自分の write だけが画面に出る |
| 4/5 | `s4_isig`  | + `ISIG` を OFF | Ctrl-C を自前で扱える支配感 |
| 5/5 | `s5_full`  | + `atexit` + `sigaction` で復元 | 異常終了でも端末が救われる完成版 |

各 step は前 step の **小さな上乗せ** なので、 困ったら一つ前に戻って `git diff` で比較してください。


## Step 1/5: scanf の限界を見る

まずは termios を一切触らずに、 「普通に書くとなぜダメか」 を体感します。

```sh
cd 01_snake/step1_termios/s1_scanf
make run
```

```c:s1_scanf/main.c
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

キーを 1 つ押しても何も起きず、 **Enter を押した瞬間にまとめて届く** ことを確認してください。 これが ICANON が ON のときの挙動 = カーネル line discipline が「行が完成するまで」 入力を溜め込んでいる状態です。

ゲームで使うには論外。 次の step で `ICANON` を落とします。


## Step 2/5: ICANON を OFF にする

ここから termios 構造体が初登場します。 やることは 3 つだけ:

1. 現状の termios を `tcgetattr` で取って退避
2. コピーから `ICANON` ビットを落として `tcsetattr` で設定
3. `read` で 1 byte 取る → 退避した設定を戻す

```sh
cd 01_snake/step1_termios/s2_canon
make run
```

```c:s2_canon/main.c
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

    tcsetattr(STDIN_FILENO, TCSAFLUSH, &orig);

    if (n != 1) {
        fprintf(stderr, "\nread failed\n");
        return 1;
    }
    printf("\nyou pressed: '%c' (0x%02X)\n", c, (unsigned char)c);
    return 0;
}
```

押した瞬間に届くようになったはずです。 ただし押したキーは **画面にも echo されている** = カーネルが ECHO ビットでキー入力を勝手に画面に書いている状態。

ゲーム中、 W/A/S/D を連打したら画面が文字で埋まる... これでは話にならないので、 次の step で消します。


## Step 3/5: ECHO を OFF にする

ここから先は **前の step との差分だけ** を貼ります。 `git diff s2_canon s3_echo` を眺める読み方を身につけると、 「どのビットが何を担当しているか」 が体に染み込みます。

```diff:s3_echo/main.c
-    raw.c_lflag &= (tcflag_t)~ICANON;   /* 行編集 OFF */
+    /* Step 2 からの差分: ECHO も落とす */
+    raw.c_lflag &= (tcflag_t)~(ICANON | ECHO);
```

それと、 静かになったことを **連打で実感する** ためループにします。 `q` で抜けます。

```c:s3_echo/main.c (抜粋)
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

```sh
cd 01_snake/step1_termios/s3_echo
make run
```

押したキーは **自分の `printf` で書いた `got: ...` 行にしか現れない**。 OS は黙ります。 ここで初めて 「自分の出力だけが見える」 ゲーム的な画面になります。

ただし、 まだ Ctrl-C を押すと SIGINT が飛んでプロセスが死にます。 `ISIG` がまだ ON だから。


## Step 4/5: ISIG を OFF にして Ctrl-C を自前で

`ISIG` は 「特定のバイトを signal に変換する」 line discipline 機能です。 OFF にすると Ctrl-C は `0x03` の **ただのバイト** として read に届きます。

```diff:s4_isig/main.c
-    raw.c_lflag &= (tcflag_t)~(ICANON | ECHO);
+    /* Step 3 からの差分: ISIG も落とす */
+    raw.c_lflag &= (tcflag_t)~(ICANON | ECHO | ISIG);
```

ループ側で `0x03` をクイットに割り当てます。

```c:s4_isig/main.c (抜粋)
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
    /* ... */
}
```

```sh
cd 01_snake/step1_termios/s4_isig
make run
```

Ctrl-C を押しても **プロセスが死なず、 自分のループが quit メッセージを出して終わる**。 キーボードがカーネルから自分に渡された瞬間です。

ただし、 ここまでで一つ **重大な落とし穴** が残っています。 もしこのプロセスが `kill -9` で殺されたり、 segfault で落ちたりすると、 端末は **raw mode のまま放置** されます。 シェルに戻っても文字が出ない、 Enter が効かない、 という事故です (`reset` で復活はします)。

次の step で塞ぎます。


## Step 5/5: atexit + sigaction で端末を救う

raw mode のままプロセスが死なないために、 **2 つの保険** をかけます。

1. `atexit(restore)` — 正常終了 (`return from main`, `exit()`) で必ず呼ぶ
2. `sigaction(SIGINT, ...)` 等の signal ハンドラから `restore` を呼ぶ

```c:s5_full/main.c (抜粋)
static struct termios g_orig;
static int            g_raw = 0;

static void restore(void) {
    if (!g_raw) return;
    tcsetattr(STDIN_FILENO, TCSAFLUSH, &g_orig);
    const char seq[] = "\x1b[?25h\x1b[2J\x1b[H";
    (void)write(STDOUT_FILENO, seq, sizeof(seq) - 1);
    g_raw = 0;
}

static void on_signal(int sig) {
    restore();
    signal(sig, SIG_DFL);
    raise(sig);
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
    raw.c_cc[VMIN]  = 0;
    raw.c_cc[VTIME] = 0;
    tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw);
    g_raw = 1;
    /* ... カーソル非表示 + 画面クリア ... */
}
```

`s5_full/main.c` には更に **矢印キー (ESC `[` A/B/C/D の 3 byte シーケンス) のパース** と **`@` を動かすメインループ** も載っています。 全文はリポジトリで読んでください。

```sh
cd 01_snake/step1_termios/s5_full
make run    # 矢印キーで @ が動く。 q で終了。
```

「ハンドラ内で復元 → デフォルトに戻して再送 → プロセスは正しい終了コードで死ぬ」 という signal イディオムは、 第 8 章 (Roguelike signal) でまた使います。


:::message alert
**ここで使った `tcsetattr` は、厳密には async-signal-safe ではありません** (POSIX `signal-safety(7)`)。「実用上は端末を救えるので使う」という割り切りで採用しています。**何が安全で何が不安全か** は第 8 章 (Roguelike signal) で正面から扱います。今は「signal ハンドラからは write(2) と低レベル syscall 中心、printf や malloc は呼ばない」を頭の隅に置いてください。
:::


## 観察する: strace で syscall 単位で見る

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

`tcsetattr` の正体が `ioctl(0, TCSETSF, {...})` であること、`read(0, buf, 1)` が即座に 0 byte を返してくる (= ノンブロッキングが効いている) ことが目で確認できます。


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
raw mode では「上矢印」というキーは無く、ESC `[` `A` の 3 文字が連続して飛んできます。


## メンタルモデルを整理する

```
[ターミナル] ←→ [カーネル line discipline] ←→ [プロセス stdin]
                  ICANON / ECHO / IXON …
                  ↑ termios の各ビットがここのスイッチ
```

普段「ターミナル」と呼んでいる体験は、**カーネルの中の line discipline が大半を提供しています**。
Snake のために termios を弄るのは、ゲーム特有のテクニックではなく、**カーネルの干渉を 1 段階剥がしている** だけです。

5 step で 1 ビットずつ剥がしてきたので、 どのビットが何を提供しているか **手の感覚** で覚えたはずです。 これが本章のいちばんの収穫です。


## 演習

- **Easy**: `s3_echo/main.c` を改造して `ECHO` だけを残す (= ICANON OFF / ECHO ON) raw mode に入り、 押したキーが画面に二重 (= 自分の `got: ...` と OS の echo) で出てしまうことを確認。 なぜ「ゲームに不適切」 か言語化する。

- **Med**: `s4_isig/main.c` の `read_key()` 相当処理を、 ESC 1 文字だけ届いた場合 (3 byte 揃わない場合) に対応させるため、 `VTIME=1` (0.1 秒タイムアウト) に変更し、 ESC キー単体を「終了」 として扱うように改造。

- **Hard**: `s5_full/main.c` で `Ctrl-C` を「ゲームのポーズ」 に再割り当て。 次の二通りを実装し挙動の違いを観察する:
  - **A**: 本章のまま (`ISIG` OFF) で、 `read` が返した `0x03` バイトを自前でポーズ動作に変換する
  - **B**: `ISIG` を **ON** に戻し、 Ctrl-C はカーネルが SIGINT に変えて飛ばす経路に任せ、 `SIGINT` ハンドラ側でポーズ動作を起こす

  どちらが「Ctrl-C 連打」 に強いか、 復元処理 (raw mode 解除と再 enter) の責務がどう分かれるか、 をレポート 1 段落でまとめる。


## 次章では
第 2 章は **蛇の体をどう表現するか**。
固定長の `char map[24][80]` を確保して、頭と体を配列に書き込みます。
`sizeof` と `offsetof` を使って **1 マスが何 byte 占有しているか** を覗き、`struct __attribute__((packed))` の効果を実測します。
スタック領域の概念が登場します。
