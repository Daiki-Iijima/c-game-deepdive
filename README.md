# Cで遊んで学ぶコンピューターシステム

> ポインタで止まり、`malloc` で見えなくなり、`fork` で逃げ出した中級者へ。
> Snake / Tetris / Roguelike を 1 行ずつ書きながら、メモリと OS の中身を**目で見て**取り戻す連載です。

![demo placeholder](https://placehold.co/800x300?text=Snake+%2F+Tetris+%2F+Roguelike+demo+%28asciinema%29)

ターミナルで動く Snake / Tetris / Roguelike を作りながら、ポインタ・heap・syscall・ELF まで全部覗く中級者向け連載のサンプルリポジトリです。

## 連載

本のテキスト (Zenn book) は別リポジトリで管理しています:

- **記事リポジトリ**: [Daiki-Iijima/zenn](https://github.com/Daiki-Iijima/zenn) — `books/c-game-deepdive/` 配下に各章の `.md` と `config.yaml`
- **コードリポジトリ**: 本リポジトリ — `00_intro/` `01_snake/` `02_tetris/` `03_roguelike/` `04_tools/` 配下の C ソースと Makefile

両者の対応関係:

| # | Zenn slug | コード |
|---|---|---|
| 0 | intro | `00_intro/` |
| 1 | termios | `01_snake/step1_termios/{s1_scanf,s2_canon,s3_echo,s4_isig,s5_full}/` |
| 2 | snake-array | `01_snake/step2_array/` |
| 3 | snake-list | `01_snake/step3_linkedlist/` |
| 4 | tetris-heap | `02_tetris/step1_heap/` |
| 5 | tetris-bitwise | `02_tetris/step2_bitwise/` |
| 6 | tetris-collision | `02_tetris/step3_collision/` |
| 7 | roguelike-dungeon | `03_roguelike/step1_map/` |
| 8 | roguelike-signal | `03_roguelike/step2_signal/` |
| 9 | roguelike-ipc | `03_roguelike/step3_ipc/` |
| 10 | roguelike-save | `03_roguelike/step4_save/` |
| 11 | bug-hunting | `04_tools/bug_hunting/` |
| 12 | binary-anatomy | `04_tools/binary_anatomy/` |

## 動かす

### Docker ワンショット

```sh
docker compose -f docker/compose.yml run --rm dev
# 中に入ったら:
cd 01_snake/step1_termios/s5_full   # 第 1 章は 5 step に分割、 s5_full が完成版
make run
```

`linux/amd64` を強制しているため、Apple Silicon でもアセンブリ出力が一致します。

### ハイブリッド開発 (Mac の nvim + Docker でビルド)

ホスト側で nvim 等を回しつつ、 ビルド/valgrind/gdb は Docker 内で走らせるのが連載中もっとも回しやすいフローです。 リポジトリ同梱の **`./dx` ラッパ** がこれを引き受けます。

```sh
./dx                                # コンテナへ bash で入る (初回は up -d まで自動)
./dx make -C 01_snake/step1_termios/s5_full run
./dx make -C 01_snake/step3_linkedlist valgrind
./dx make -C 02_tetris/step3_collision asan_bug
docker compose -f docker/compose.yml down   # 撤収
```

`./dx` は `docker compose exec dev` のラッパで、 **ホスト側の現在ディレクトリを `/workspace/<相対パス>` として扱う** ので、 章フォルダで `cd` した状態のまま `./dx make` が叩けます。

#### nvim 連携 (任意)

`.nvim-snippet.lua` を `~/.config/nvim/lua/local/cdeepdive.lua` などに置いて `require` すれば、 以下のキーマップが付きます。

| キー | 動作 |
|---|---|
| `<leader>cm` | 現在ファイルの章ディレクトリで `make` |
| `<leader>cr` | `make run` |
| `<leader>cv` | `make valgrind` |
| `<leader>ca` | `make asan_bug` |
| `<leader>ci` | `make inspect` |
| `<leader>cs` | リポルートで `./dx` (= シェルへ入る) |

すべて `:split | terminal` で下窓に出力。 `q` で閉じる。

### ホスト Linux で直接

```sh
sudo apt install build-essential gdb valgrind strace ltrace binutils elfutils linux-tools-generic
make -C 01_snake/step1_termios/s5_full run
```

macOS ホスト直接ビルドは想定しません (Docker を使ってください)。

## ライセンス

MIT
