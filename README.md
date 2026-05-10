# Cで遊んで学ぶコンピューターシステム

> ポインタで止まり、`malloc` で見えなくなり、`fork` で逃げ出した中級者へ。
> Snake / Tetris / Roguelike を 1 行ずつ書きながら、メモリと OS の中身を**目で見て**取り戻す連載です。

![demo placeholder](https://placehold.co/800x300?text=Snake+%2F+Tetris+%2F+Roguelike+demo+%28asciinema%29)

ターミナルで動く Snake / Tetris / Roguelike を作りながら、ポインタ・heap・syscall・ELF まで全部覗く中級者向け連載のサンプルリポジトリです。

## 連載

このリポジトリは **Zenn book** としてそのまま公開できる構成です。

```sh
npm install                            # zenn-cli を初回だけ取得
npx zenn preview --port 8000           # http://localhost:8000 で本のプレビュー
npx zenn list:chapters c-game-deepdive # 章一覧
```

ソース:
- 本のメタ情報: `books/c-game-deepdive/config.yaml`
- 各章の本文: `books/c-game-deepdive/{slug}.md`
- 章で参照するコード: `00_intro/` `01_snake/` `02_tetris/` `03_roguelike/` `04_tools/`

公開フロー (Zenn と GitHub を連携している前提):

1. このリポジトリを GitHub に push
2. Zenn の [GitHub 連携](https://zenn.dev/zenn/articles/connect-to-github) で本リポジトリを登録
3. `books/c-game-deepdive/config.yaml` の `published: true` に変更して push

| # | Zenn slug | コード |
|---|---|---|
| 0 | [intro](books/c-game-deepdive/intro.md) | `00_intro/` |
| 1 | [termios](books/c-game-deepdive/termios.md) | `01_snake/step1_termios/` |
| 2 | [snake-array](books/c-game-deepdive/snake-array.md) | `01_snake/step2_array/` |
| 3 | [snake-list](books/c-game-deepdive/snake-list.md) | `01_snake/step3_linkedlist/` |
| 4 | [tetris-heap](books/c-game-deepdive/tetris-heap.md) | `02_tetris/step1_heap/` |
| 5 | [tetris-bitwise](books/c-game-deepdive/tetris-bitwise.md) | `02_tetris/step2_bitwise/` |
| 6 | [tetris-collision](books/c-game-deepdive/tetris-collision.md) | `02_tetris/step3_collision/` |
| 7 | [roguelike-dungeon](books/c-game-deepdive/roguelike-dungeon.md) | `03_roguelike/step1_map/` |
| 8 | [roguelike-signal](books/c-game-deepdive/roguelike-signal.md) | `03_roguelike/step2_signal/` |
| 9 | [roguelike-ipc](books/c-game-deepdive/roguelike-ipc.md) | `03_roguelike/step3_ipc/` |
| 10 | [roguelike-save](books/c-game-deepdive/roguelike-save.md) | `03_roguelike/step4_save/` |
| 11 | [bug-hunting](books/c-game-deepdive/bug-hunting.md) | `04_tools/bug_hunting/` |
| 12 | [binary-anatomy](books/c-game-deepdive/binary-anatomy.md) | `04_tools/binary_anatomy/` |

## 動かす

### Docker (推奨)

```sh
docker compose -f docker/compose.yml run --rm dev
```

`linux/amd64` を強制しているため、Apple Silicon でもアセンブリ出力が一致します。

中に入ったら章ディレクトリへ:

```sh
cd 01_snake/step1_termios
make run
# 矢印キーで @ が動く。q で終了。
```

### ホスト Linux で直接

```sh
sudo apt install build-essential gdb valgrind strace ltrace binutils elfutils linux-tools-generic
make -C 01_snake/step1_termios run
```

macOS ホスト直接ビルドは想定しません (Docker を使ってください)。

## ライセンス

MIT
