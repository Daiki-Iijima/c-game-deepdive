---
title: "第9章 — モンスターを別プロセスで動かす: fork / pipe / dup2 / SIGCHLD"
---

:::message
本連載で書くコード一式は **[GitHub: Daiki-Iijima/c-game-deepdive](https://github.com/Daiki-Iijima/c-game-deepdive)** にあります。本文中で `01_snake/step1_termios/main.c` のように参照する path はすべてそのリポ内のファイルです。
:::


![demo placeholder](https://placehold.co/800x300?text=parent+game+%2B+child+AI+via+pipes)

## つかみ

第 8 章までの Roguelike は **単一プロセス** でした。 AI を入れたい時、 普通は同じプロセス内で `update_monsters()` を呼びます。 それで困らないのに、 なぜわざわざ **別プロセスに切り出す** か?

理由はいくつもあります:
- **AI が重い計算をしても本体は止まらない** (60fps のループで 100ms 考える AI は別プロセスじゃないと無理)
- **AI が暴走 (無限ループ・segfault) しても親は無事**
- **AI の難易度を別バイナリで差し替え可能** (`exec` で `./ai_easy` / `./ai_hard` を切替)
- **将来ネットワーク越しに動かしても同じ I/O 抽象** (pipe → socket 置換)

そして何より、 **C の中で「複数プロセスを協調させる」 を体感する一番素直な題材** です。

## 今日の機構: fork + 2 本の pipe

親と子の通信は **2 本の pipe** で双方向にします。

```
parent ──── p2c[1] ──→ pipe ──→ p2c[0] ──── child stdin
parent ←── c2p[0] ←── pipe ←── c2p[1] ──── child stdout
```

`pipe()` は `int fd[2]` を返し、 `fd[0]` が読み口、 `fd[1]` が書き口。 `fork()` 後、 親と子は **両方ともこの 4 つの fd を所有** している状態。 そのまま使うとデッドロックの元なので、 各々が **使わない方を必ず閉じる**。

```c
int p2c[2], c2p[2];
pipe(p2c); pipe(c2p);

pid_t pid = fork();
if (pid == 0) {
    /* 子 */
    dup2(p2c[0], STDIN_FILENO);
    dup2(c2p[1], STDOUT_FILENO);
    close(p2c[0]); close(p2c[1]); close(c2p[0]); close(c2p[1]);
    run_ai_child();        /* fgets/printf でやり取りできる */
}
/* 親: 使わない端を閉じる */
close(p2c[0]); close(c2p[1]);
```

`dup2(src, dst)` は `dst` を一旦 `close` してから `src` の fd 番号を `dst` 番号に複製する syscall。 これで 「**子の `printf("move %d\n", ...)` がパイプ経由で親に届く**」 構図を作れます。 子のコードはほぼ普通の標準入出力プログラムとして書けます。

```c
static void run_ai_child(void) {
    char buf[256];
    while (fgets(buf, sizeof(buf), stdin)) {
        int pr, pc, mr, mc;
        if (sscanf(buf, "tick %d %d %d %d", &pr, &pc, &mr, &mc) != 4) continue;
        int dr = 0, dc = 0;
        if      (pr < mr) dr = -1;
        else if (pr > mr) dr =  1;
        else if (pc < mc) dc = -1;
        else if (pc > mc) dc =  1;
        printf("move %d %d\n", dr, dc);
        fflush(stdout);                    /* これを忘れると親が永遠に待つ */
    }
    _exit(0);
}
```

`fflush(stdout)` を忘れると pipe にバッファされたまま flush されず、 親が `read` でブロック → 親も子も止まる **古典的なデッドロック** が起きます。 第 11 章 (gdb 実戦) でこのバグの再現を扱います。

## SIGCHLD で子を看取る

親が `read` 中に子が落ちると、 親の `read` は EOF (= 0 byte) を返します。 ただ、 「いつ・なぜ落ちたか」 を真面目に拾うには SIGCHLD の handler が必要。 handler 内で許される async-signal-safe 関数の中に **`waitpid` がある** ので、 そこで reap します。

```c
static void on_sigchld(int sig) {
    (void)sig;
    int status;
    while (waitpid(-1, &status, WNOHANG) > 0) g_child_dead = 1;
}

struct sigaction sa = {0};
sa.sa_handler = on_sigchld;
sa.sa_flags   = SA_RESTART | SA_NOCLDSTOP;   /* SIGSTOP/CONT は無視 */
sigaction(SIGCHLD, &sa, NULL);
```

`g_child_dead` は第 8 章で覚えた **volatile sig_atomic_t** パターン。 メインループはそれを見て「AI 居なくなった」 を HUD に出します。

## 作る

```sh
cd 03_roguelike/step3_ipc
make
./rogue_step3
# @ がプレイヤ (hjkl で移動)、 M が AI モンスター。
# AI は別プロセスで 1 マスずつ追跡してくる。 q で終了。
```

捕まると `caught by AI!` メッセージが出ます。 `q` で終了すると、 親が `close(p2c[1])` してから `waitpid` で子を待ちます。 子は `fgets` が EOF を受けて `_exit(0)`、 SIGCHLD が発火して reap、 親が終了。 流れが見える設計です。

## 覗く: strace で fork/pipe/dup2 の流れを見る

```sh
make strace
# = strace -f -e trace=clone,fork,pipe,pipe2,dup2,read,write,close ./rogue_step3
```

短い抜粋:

```
pipe2([3, 4], 0)         = 0
pipe2([5, 6], 0)         = 0
clone(child_stack=NULL, ...) = 12345  ← fork
[pid 12345] dup2(3, 0)   = 0
[pid 12345] dup2(6, 1)   = 1
[pid 12345] close(3)     = 0
[pid 12345] close(4)     = 0
[pid 12345] close(5)     = 0
[pid 12345] close(6)     = 0
close(3)                 = 0          ← 親側、 不要 fd を閉じる
close(6)                 = 0
write(4, "tick 1 1 14 48\n", 15) = 15  ← 親 → 子
[pid 12345] read(0, "tick 1 1 14 48\n", 4096) = 15
[pid 12345] write(1, "move 1 0\n", 9)
read(5, "move 1 0\n", 1) = 1           ← 親 ← 子
```

`pipe2` で 2 本作り、 `clone` (`fork`) で子を生み、 子側で `dup2` して `close` して、 親子の text 会話が始まる。 **教科書の絵がそのまま syscall として並ぶ** のが strace の強みです。

## 端末の取り合いは起こらない

第 7 章のリスク登録に「Roguelike fork/pipe で端末制御を奪い合う」 と書きましたが、 本実装では **子の stdin/stdout は pipe にすり替えてある** ため、 子は ttybe touch しません。 親だけが `tty_raw_mode()` を呼んで端末を独占。 これが「dup2 で `STDIN_FILENO` / `STDOUT_FILENO` を切る」 重要性です。

## メンタルモデル更新

```
親プロセス (端末を独占)              子プロセス (AI)
 ┌──────────────────────────┐         ┌─────────────────────┐
 │ stdin  fd0 = TTY        │         │ stdin  fd0 = pipe ──┼──┐
 │ stdout fd1 = TTY        │         │ stdout fd1 = pipe ──┼─┐│
 │ p2c-w  fd4              │ ──────→ │ ←── fgets buf       │ ││
 │ c2p-r  fd5              │ ←────── │ printf("move ...") ─┘ ││
 └──────────────────────────┘         └─────────────────────┘ ││
                                                              ││
                                                              ││
                              ←── pipes are owned by kernel ──┘┘
```

「プロセス」 は `fd` の表 (file descriptor table) を持っていて、 **fd を付け替える** = `dup2` するだけで、 同じ `printf` のコードが TTY とも pipe とも socket とも会話できる。 これが **Unix の I/O モデルの核** です。

## 演習

- **Easy**: 子の `fflush(stdout)` を **わざと外して** ビルド。 ゲームを起動すると親が `read` で詰まり、 AI が動かなくなる (`make strace` で確認できる)。 直す。
- **Med**: AI を **別バイナリ** に切り出す。 `ai.c` を書き、 `main` を `run_ai_child()` 内容にする。 親の `if (pid == 0)` 分岐は `execvp("./ai", args)` に置き換える。 子の依存ライブラリを切り替えるだけで AI を差し替え可能になる。
- **Hard**: `select` (or `poll`) で 「親の TTY 入力」 と 「子からの返信」 を同時に待つ ノンブロッキング設計に直す。 ask_ai の中の同期 read を止め、 「最新の応答が届いていれば反映、 まだなら去年の応答を使う」 設計に。 60fps を維持できるか strace で確認。

## 次回予告

第 10 章は **セーブデータ**。 ゲーム状態の構造体を `fwrite` でバイナリにそのまま書き出すと、 一見動く。 だが **別マシンでロードした瞬間に壊れる**。 padding と endian の罠です。 `Map` 構造体を例に、 「シリアライザを明示的に書く」 という当然の作法を、 失敗込みで体験します。
