---
title: "第1章 — キー入力をハック: termios 解剖と raw mode の正体"
---

:::message
本連載で書くコード一式は **[GitHub: Daiki-Iijima/c-game-deepdive](https://github.com/Daiki-Iijima/c-game-deepdive)** にあります。本文中で `01_snake/step1_termios/main.c` のように参照する path はすべてそのリポ内のファイルです。
:::


![demo placeholder](https://placehold.co/800x300?text=arrow+keys+move+%40+across+screen+%28asciinema%29)

## はじめに
普通の C プログラムでキー入力を取ると、こうなります。

```c
char c;
scanf("%c", &c);
```

これだと **Enter を押すまで** 何も読めません。Snake のような「押した瞬間に動く」ゲームには使えない。
`scanf` が悪いわけではなく、**カーネルが入力を行単位で溜めてから渡している** からです。
この「溜める」を解除するのが、本章のテーマ **termios の raw mode** です。

## 本章のテーマ: termios

ターミナルは「文字単位」のデバイスのように見えて、実はカーネルの中に **行編集機能** が住んでいます。`Backspace` で 1 文字消えるのも、`Ctrl-C` で SIGINT が飛ぶのも、`Ctrl-S` で出力が止まるのも、すべてカーネルの **line discipline** がやっています。

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

## 実装する
本章は **学習目的のため、敢えて `lib/tty.c` を使わず main.c に termios ロジックを全部詰め込みます**。
1 ファイル完結で読み下せる方が、どのフラグが効いているかを追いやすいからです。第 2 章以降はこの実装を `lib/tty.c` 共有に移し、ゲームロジックに集中していきます。

`01_snake/step1_termios/main.c` のキモだけ抜粋します (全文はリポジトリを参照)。

```c
#include <termios.h>
#include <unistd.h>

static struct termios g_orig;

static void enter_raw(void) {
    tcgetattr(STDIN_FILENO, &g_orig);     // 現状を退避
    atexit(restore);                       // 終了時に必ず戻す

    struct termios raw = g_orig;
    raw.c_lflag &= ~(ICANON | ECHO | ISIG);
    raw.c_iflag &= ~(IXON | ICRNL);
    raw.c_oflag &= ~(OPOST);
    raw.c_cc[VMIN]  = 0;   // 0 byte でも read は即時 return
    raw.c_cc[VTIME] = 0;
    tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw);
}
```

`atexit(restore)` を **必ず**入れること。raw mode のままプロセスが死ぬと、ターミナルが操作不能になります (`reset` で復帰可能ですが、読者には体験させたくない事故です)。
さらに `SIGINT` / `SIGTERM` のハンドラからも `restore` を呼びます。`Ctrl-\` (SIGQUIT) も同様。

```c
static void on_signal(int sig) {
    restore();
    signal(sig, SIG_DFL);
    raise(sig);
}
```

「ハンドラ内で復元 → デフォルトに戻して再送 → プロセスは正しい終了コードで死ぬ」というパターンです。第 8 章 (SIGWINCH) でこのテクをまた使います。

:::message alert
**ここで使った `tcsetattr` は、厳密には async-signal-safe ではありません** (POSIX `signal-safety(7)`)。「実用上は端末を救えるので使う」という割り切りで採用しています。**何が安全で何が不安全か** は第 8 章 (Roguelike signal) で正面から扱います。今は「signal ハンドラからは write(2) と低レベル syscall 中心、printf や malloc は呼ばない」を頭の隅に置いてください。
:::

## ビルドして動かす
抜粋を読んだら、実際に動かします。Docker コンテナの中で:

```sh
cd 01_snake/step1_termios
make           # snake_step1 が生成される
./snake_step1  # 矢印キーで @ が動く。q で終了。
```

矢印キーで `@` がマス目を超えて動き、`q` で抜けます。終了後にプロンプトが普通に戻ってくることも確認してください (= `restore` が効いている証拠)。

## 観察する: strace で syscall 単位で見る

```sh
strace -e trace=ioctl,read,write ./snake_step1
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

`\33[A` (3 byte シーケンス) が **矢印キーの正体** です。raw mode では「上矢印」というキーは無く、ESC `[` `A` の 3 文字が連続して飛んできます。

## メンタルモデルを整理する

```
[ターミナル] ←→ [カーネル line discipline] ←→ [プロセス stdin]
                  ICANON / ECHO / IXON …
                  ↑ termios の各ビットがここのスイッチ
```

普段「ターミナル」と呼んでいる体験は、**カーネルの中の line discipline が大半を提供しています**。Snake のために termios を弄るのは、ゲーム特有のテクニックではなく、**カーネルの干渉を 1 段階剥がしている** だけです。

## 演習

- **Easy**: `ECHO` だけを残して raw mode に入り、押したキーが画面の左上に出てしまうことを確認。なぜ「ゲームに不適切」か言語化する。
- **Med**: `read_key()` で `ESC` 1 文字だけ届いた場合 (3 byte 揃わない場合) を再現するため、`VTIME=1` (0.1 秒タイムアウト) に変更し、ESC キー単体を「終了」として扱うように改造。
- **Hard**: `Ctrl-C` を「ゲームのポーズ」に再割り当て。次の二通りを実装し挙動の違いを観察する:
  - **A**: 本章のまま (`ISIG` OFF) で、`read` が返した `0x03` バイトを自前でポーズ動作に変換する
  - **B**: `ISIG` を **ON** に戻し、Ctrl-C はカーネルが SIGINT に変えて飛ばす経路に任せ、`SIGINT` ハンドラ側でポーズ動作を起こす
  どちらが「Ctrl-C 連打」に強いか、復元処理 (raw mode 解除と再 enter) の責務がどう分かれるか、をレポート 1 段落でまとめる。

## 次章では
第 2 章は **蛇の体をどう表現するか**。固定長の `char map[24][80]` を確保して、頭と体を配列に書き込みます。`sizeof` と `offsetof` を使って **1 マスが何 byte 占有しているか** を覗き、`struct __attribute__((packed))` の効果を実測します。スタック領域の概念が登場します。
