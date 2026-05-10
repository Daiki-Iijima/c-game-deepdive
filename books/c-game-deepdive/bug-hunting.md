---
title: "第11章 — gdb と valgrind 実戦: シリーズ中のバグを総ざらい"
---

![demo placeholder](https://placehold.co/800x300?text=gdb+session+%2B+valgrind+output)

## つかみ

ここまでの 10 章で、 4 種類の典型的なメモリバグを順に登場させてきました:

- 第 3 章: 二重 free (Snake)
- 第 6 章: 配列外読み書き (Tetris)
- 第 9 章: pipe デッドロック (Roguelike fork)
- 第 10 章: 構造体直書きで壊れたセーブ

各章で「ツールがどう叫ぶか」を見ましたが、 散らばった知識のままでは 「実際にバグに遭遇した時の手順」 になりません。 本章は **小さな実験ファイル `buggy.c` 1 本** に上記の縮小版を仕込み、 **gdb と valgrind を使った標準手順** を 1 つずつ通します。

## 今日の機構: バグの種別 × ツールの相性

|  | gdb (動的) | valgrind (動的) | ASan (静的計装) |
|---|---|---|---|
| 二重 free       | クラッシュで stop、 backtrace | `Invalid free` を行番号で報告 | `double-free` を即時 |
| use after free | 偶然読めて沈黙 → 別の所で爆発 | `Invalid read of size N` | `heap-use-after-free` |
| 配列外 (stack)  | クラッシュ位置と原因が遠い | 弱い (uninit ヒント程度) | `stack-buffer-overflow` 即時 |
| 配列外 (global)  | 同上 | 弱い | `global-buffer-overflow` 即時 |
| 未初期化      | バグなのにクラッシュしない | `Conditional jump on uninitialised` | UBSan で `-fsanitize=undefined` |
| デッドロック    | `(gdb) thread apply all bt` で全 thread の止まった位置 | Helgrind / DRD で報告 | thread-sanitizer (-fsanitize=thread) |

「症状が出た時にどのツールから当てるか」 を選べることが本章のゴール。

## 作る・壊す・直す

```sh
cd 04_tools/bug_hunting
make
./buggy --bug=double-free       # 多分 abort()
make run_double_free             # → valgrind が報告
make run_use_after_free          # → valgrind の Invalid read
make run_oob                     # → ASan の stack-buffer-overflow
make run_uninit                  # → valgrind の Conditional jump on uninitialised
```

`buggy.c` の中身は意図的に短いので、 `cat buggy.c` してから出力を読むのがおすすめ。

## 覗く 1: gdb の最低限フロー

```sh
./buggy --bug=use-after-free
# Address Sanitizer 無し版でクラッシュさせると沈黙することが多い。
# coredump がほしいので有効化:
ulimit -c unlimited
./buggy --bug=double-free        # → core が落ちる (環境による)
gdb ./buggy core
(gdb) bt
(gdb) frame 0
(gdb) list
(gdb) p p          # 値を見る
```

二重 free の場合は glibc の内部関数で abort するので、 `bt` を辿ると **自分のコード上の `free(p)` 行** に直接届きます。

### live debug

クラッシュを待たずに gdb を仕込む:

```sh
gdb --args ./buggy --bug=double-free
(gdb) break free            # malloc.c::free にブレーク
(gdb) run
(gdb) commands              # ヒットしたとき自動で続行
> bt
> continue
> end
```

### watchpoint

「特定の変数の値が変わったら止まる」 という古典武器:

```sh
gdb ./buggy
(gdb) break main
(gdb) run --bug=use-after-free
(gdb) watch *p              # *p を監視
(gdb) continue
```

これで `*p` の値が破壊された瞬間に gdb が止まります。 デッドロック/use-after-free 系で頻用。

## 覗く 2: valgrind を「静か → 騒がしい」順に当てる

```sh
valgrind ./buggy --bug=uninit            # 軽い chk
valgrind --leak-check=full ./buggy ...    # leak まで
valgrind --track-origins=yes ./buggy ...  # uninitialised の発生源も
valgrind --tool=helgrind ./threaded_app   # race
valgrind --tool=massif ./tetris_step1     # heap 使用量グラフ (第 4 章)
```

`--track-origins=yes` は uninit 系の最強オプション。 **どこで作られた未初期化値が、 どこで使われたか** を全部追ってくれます (代わりに遅い)。

## 覗く 3: pipe デッドロック (第 9 章) を gdb で

第 9 章の AI 連携で `fflush(stdout)` を抜くと、 親の `read` で全プロセスがハングします。 gdb で見るには:

```sh
ps aux | grep rogue_step3        # 親 PID
gdb -p <親PID>
(gdb) bt
(gdb) detach
gdb -p <子PID>
(gdb) bt
(gdb) detach
```

両側の `bt` で「親は read(...) 中、 子は printf 内部の write(...) 中、 双方相手の動きを待っている」 が読み取れます。 これがデッドロックの可視化。

## メンタルモデル更新: 「叫ぶ瞬間」と「気づく瞬間」 の距離

```
[コード上のバグ]   ──→ [メモリ破壊]   ──→ [症状]
   |                       |                 |
  origin             actual corruption     visible crash
```

何も計装が無いと、 私たちが見るのは 「visible crash」 だけ。 そこから origin に戻る作業が **デバッグ**。 ツールは:

- **valgrind / ASan** = corruption の瞬間に叫ぶ → origin との距離が短い
- **gdb (素)** = visible crash の bt から逆算 → origin との距離が遠い (でも内部状態は全部見える)

両方持っていると、 「**叫ぶ位置**」 を選んで絞り込めます。

## 演習

- **Easy**: `buggy.c` の 4 種類をすべて手で動かし、 各ツールの出力を 1 つずつ自分のメモに貼り付けよ。 「何 byte の何が起きたか」 を 1 行サマリで書く。
- **Med**: 第 6 章の Tetris (`02_tetris/step3_collision/main.c`) を `gdb -tui` で開き、 `--bug=oob` で gdb を attach。 `watch g_score_history[15]` を仕掛け、 そこを書き換える瞬間に止まることを確認。
- **Hard**: 第 9 章の Roguelike を `fflush` 削除版に書き換えて起動 → ハングしたら別ターミナルから `gdb -p` で親と子の両方を attach し、 両方の bt を貼り付けてレポート。 「**両プロセスの bt を一画面に並べる**」 のがデッドロック解析の定石。

## 次回予告

最終章 (第 12 章) は **逆アセンブル**。 自分が書いた C コードがどんな asm に化けたか、 `readelf` でセクションを覗き、 `objdump -d` で `main` を覗き、 `perf stat` で 1 tick あたりの命令数 / cache miss を測ります。 第 5 章の関数ポインタ呼び出しが asm のどこに座っているか、 第 6 章の ASan の毒チェックが本当にインライン展開されているか、 自分の目で確かめます。
