---
title: "第8章 — SIGWINCH を捕まえる: signal handler の作法 (async-signal-safe)"
---

:::message
本連載で書くコード一式は **[GitHub: Daiki-Iijima/c-game-deepdive](https://github.com/Daiki-Iijima/c-game-deepdive)** にあります。本文中で `01_snake/step1_termios/main.c` のように参照する path はすべてそのリポ内のファイルです。
:::


![demo placeholder](https://placehold.co/800x300?text=resize+terminal+%2C+map+regenerates)

## はじめに
第 7 章のダンジョンは **起動時のターミナルサイズで固定** でした。
 プレイ中にウィンドウをドラッグでリサイズすると、 ダンジョンが画面を超えたり余白が空いたり。
 これを直すには、 OS から **「ウィンドウサイズが変わったよ」** という通知を受け取る必要があります。
 その通知の名前が **SIGWINCH (SIGnal WINdow CHange)**。


第 1 章の終わりに「signal handler から `tcsetattr` を呼ぶのは厳密には不正」と予告しました。
 本章はその約束を回収し、 **signal handler の中で何が許され、 何が禁じられているか** を真面目に整理します。


## 本章のテーマ: handler は flag を立てるだけ

良くないパターン (やってしまいがち):

```c
void on_winch(int sig) {
    free(map);                            /* malloc/free は async-signal-safe ではない */
    map = map_new(new_rows, new_cols);    /* ↑ */
    render(map, player);                  /* printf も同上 */
}
```

handler の中で `malloc` / `free` / `printf` / `pthread_*` を呼ぶのは **POSIX 違反**。
 たまたま動くこともあるが、 本番でデッドロックや二重初期化を引き起こします。
 規格が許す関数は `signal-safety(7)` の表に載っているもの (`write`, `kill`, `_exit`, `read`, `sigaction`, など) **だけ**。


正しいパターン: handler は **「フラグを 1 個立てる」 だけ**。
 メインループがそれを見て安全に処理する。


```c
static volatile sig_atomic_t g_resize_flag = 0;

static void on_winch(int sig) {
    (void)sig;
    g_resize_flag = 1;   /* これしかしない */
}
```

`volatile sig_atomic_t` は **「signal handler とメインスレッドの両方から触れる」** ことを規格が保証している唯一の型です。
 `int` でも実用上は通ることが多いけれど、 教科書通りには `sig_atomic_t`。


メインループはこう:

```c
for (;;) {
    if (g_resize_flag) {
        g_resize_flag = 0;
        int nr, nc;
        tty_size(&nr, &nc);
        free(m);
        m = map_new(nr - 1, nc);
        p = generate(m, 6);
        render(m, p);
    }
    /* ... 入力処理 ... */
}
```

`free` / `malloc` / `render` を **メインループの文脈で** 呼んでいるので、 これは安全。


:::details `sigaction` 構造体と SA_RESTART / SA_NOCLDSTOP / SA_SIGINFO
古い `signal(2)` ではなく `sigaction(2)` を使う理由は、 ハンドラ登録の挙動を細かく制御できるから。

```c
struct sigaction {
    void     (*sa_handler)(int);                       // 通常の handler
    void     (*sa_sigaction)(int, siginfo_t *, void *); // SA_SIGINFO 用拡張 handler
    sigset_t   sa_mask;                                // handler 中マスクするシグナル集合
    int        sa_flags;                               // フラグ (下記参照)
};
```

主要フラグ:

- `SA_RESTART`: 中断された syscall (`read`, `write`, `wait` など) を自動的に再開させる。 シェルスクリプト的な「気にせず動かしたい」用途で便利。
- `SA_NOCLDSTOP`: SIGCHLD で **子の停止/再開 (SIGSTOP/SIGCONT) を無視**。 子の終了 (= reap が必要なケース) だけハンドラに来る。 第 9 章で使います。
- `SA_NOCLDWAIT`: SIGCHLD ハンドラが無くても zombie を残さない (子が即時 reap される)。
- `SA_SIGINFO`: 拡張ハンドラ `sa_sigaction` を使う。 `siginfo_t` で送信元 PID / UID / 起因アドレスなどが取れる。
- `SA_RESETHAND`: 1 回呼ばれたらデフォルトに戻る (古い `signal(2)` の挙動)。

`sa_mask` の役割: handler 実行中に **追加でブロックするシグナル集合**。 `sigemptyset(&sa.sa_mask)` を忘れると未初期化のゴミが入って予期せぬブロックになる。 必ず初期化する。
:::



## 実装する
```sh
cd 03_roguelike/step2_signal
make
./rogue_step2
```

プレイ中にターミナルウィンドウを **ドラッグでリサイズ** してください。
 ダンジョンが新しいサイズで再生成されます (シードは `time(NULL)` 起動時固定なので、 同じシードのもとで作り直す挙動)。
 HUD に `term=AxB` が表示され、 リサイズすると数字が変わります。


## 観察する: strace で SIGWINCH を見る

別ターミナルで `tput lines` / `tput cols` を確認しつつ、 こちらの起動時に:

```sh
strace -e trace=ioctl,rt_sigaction,read ./rogue_step2 2> trace.log
```

ウィンドウをリサイズすると `trace.log` にこんな行が並びます (抜粋):

```
rt_sigaction(SIGWINCH, {sa_handler=0x..., ...}, NULL, 8) = 0
read(0, ...)  = ? ERESTARTSYS (To be restarted ...)
--- SIGWINCH {si_signo=SIGWINCH, si_code=SI_KERNEL} ---
rt_sigreturn({mask=[]}) = 0
ioctl(1, TIOCGWINSZ, {ws_row=30, ws_col=120, ...}) = 0
```

- カーネルが SIGWINCH を配送 (`SI_KERNEL`)
- handler に飛び、 戻ってきた直後にメインループが `ioctl(TIOCGWINSZ)` で新サイズを取りに行く
- `read` が `ERESTARTSYS` で中断され、 main の VMIN=0/VTIME=0 ループでまた呼ばれている

`tty_size()` の正体が `ioctl(TIOCGWINSZ)` であることが strace で見えるのもポイント。
 端末サイズという「変数のような何か」は **カーネルが管理し ioctl で問い合わせる** 仕組みです。


## なぜ SA_RESTART を **付けない** のか

第 1 章の SIGINT ハンドラには `sa.sa_flags = SA_RESTART` を付けました。
 しかし本章では **意図的に外しています**。


- `SA_RESTART` 付き: handler から戻った瞬間、 中断された `read` などが **自動で再呼び出し**。
 アプリは何事もなかったかのよう。

- `SA_RESTART` 無し: `read` などは `EINTR` で戻る。
 アプリは `if (errno == EINTR) /* re-enter */` と書く必要があるが、 **「中断された」 ことに気づける**。


SIGWINCH の場合、 中断された `read` が再開する前に **再描画したい**。
 だから `SA_RESTART` を外します。
 SIGINT (Ctrl-C で素直に終了) なら restart で良い。
 用途で使い分ける。


## 観察する: signal-safety(7) の早見表

`man 7 signal-safety` をコンテナ内で開くと、 handler から呼んでよい関数のリストがあります。
 抜粋:

| 安全 | やめろ |
|---|---|
| `write`, `read`, `_exit` | `printf`, `puts` |
| `kill`, `raise`, `sigaction` | `malloc`, `free`, `realloc` |
| `sleep` (※ 一部実装は微妙) | `pthread_*` |
| `time`, `clock_gettime` | `localtime` (内部 buffer) |

「I/O は write は OK、 printf は NG」 が一番引っかかる罠。
 第 1 章で `printf` をハンドラから使わず `write(2, ...)` を使ったのも、 **本章の予習** だったわけです。


## メンタルモデルを整理する

```
[OS カーネル]                    [ユーザプロセス]
                                   main loop
                                   ┌──────────────┐
   SIGWINCH 配送 ─→  preempt ─→   │ on_winch     │
                                   │   flag = 1   │
                   ←─ rt_sigreturn │ return       │
                                   │              │
   read(0,...) 再入                │ if (flag) {  │
                                   │   redraw     │
                                   │ }            │
                                   └──────────────┘
```

「signal は **割り込み**」 と一語で説明されることが多い概念ですが、 内側を見ると **プログラムカウンタが横道に逸れて短い関数に入って戻ってくる** という、 関数呼び出しに似た構造をしています。
 違うのは **どこから飛んできたか分からない** こと。
 だから handler では何でもできるとは限らないわけです。


## 演習

- **Easy**: handler の中で `write(STDERR_FILENO, "WINCH!\n", 7)` を 1 行呼ぶ実験 (これは安全)。
 リサイズ時に stderr に “WINCH!” が出ることを確認。

- **Med**: handler の中で **わざと** `printf("resized\n")` を呼ぶ。
 リサイズを高速で繰り返したり、 `malloc`-heavy な状況で長時間プレイしたりして、 「**ほとんどの場合は動くが、 時々ハングや変な出力が出る**」 様子を観察する (再現性は低いが体験できる)。

- **Hard**: `self-pipe` パターンを実装せよ。
 `pipe(fd)` を起動時に作り、 handler は `write(fd[1], ...)` だけする。
 メインループは `select` / `poll` で `fd[0]` と `STDIN_FILENO` の両方を待つ。
 `read` の `EINTR` ハンドリングが消え、 構造が綺麗になる。
 第 9 章の fork+pipe の予習にもなる。


## 次章では
第 9 章は **モンスターを別プロセスで動かします**。
 `fork()` で子プロセスを作り、 親と子のあいだに `pipe()` を 2 本張って双方向通信。
 子は AI の経路計算に集中し、 親はゲームループに集中。
 ターミナルの取り合いが起きないよう、 `dup2` で標準入出力を慎重に切り回します。
 第 8 章で作った self-pipe の発想がそのまま生きます。

