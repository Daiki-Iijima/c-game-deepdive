# Plan: 前提知識 intro 集約 + 各章「記事だけで書ける」 化

承認日: 2026-05-11
適用範囲: zenn repo (記事) を中心に、 第 0 章 (intro) 充実 + 第 1 章 s5 抜粋問題の解消 → 他章へ展開

## 動機

中級者向けと銘打っているが、 実際は次の概念で詰まる:
- C: `static` / TU / リンケージ / 関数ポインタ / typedef
- POSIX: fd / syscall / STDIN_FILENO
- signal: handler / async-signal-safe / 冪等性 (idempotence)
- 標準: stdio バッファリング / fflush / perror / errno
- TTY: line discipline / canonical mode / ANSI escape

また第 1 章 s5_full は記事が「抜粋」 で逃げているため、 **記事だけ読んで完成版コードを書けない**。 リポと記事の往復を強いられる。

## 確定した方針 (user 確認済)

1. **code repo の構造はそのまま**: s5_full を更に細分化しない。 段階提示は記事側だけで行う。
2. **記事長は気にしない**: 適切に `:::details` で折りたためばよい。
3. **「本章で使う概念 = intro 参照」、 「本章で正面から扱う概念 = 本章で詳述」** の境界で集約。
4. **前提知識の深さは「ざっくり理解しておけば OK」 まで**: intro は辞書、 教科書ではない。

## 設計原則

### 自己充足性 (self-sufficient)
記事だけ読んで完成版コードが書ける。 「全文はリポを参照」 で逃げない。

### 段階的提示 (progressive)
大きい main.c は **関数単位** で完全コード + 解説を順番に。 各塊は前の塊に追加するだけで動く形。

### 前提リンク化
各章で前提語の初出時に intro.md#anchor へリンク。

### 前提辞典のスタイル
各 `:::details` は:
- **1〜2 段落の最小定義**
- なぜ重要か 1 文
- 「もっと知るなら: `man X` / 第 N 章」 で打ち切り

教科書化しない。 辞書化する。

## intro.md 拡張内容 (前提辞典 64 項目)

各項目を `:::details <用語名>` で実装。 通読でも拾い読みでも対応。

### A. C の設計論 / 言語仕様 (18 項目)
1. TU (翻訳単位)
2. `static` の 4 つの意味 (file scope / function scope / array param / リンケージ)
3. `extern` / 内部 vs 外部リンケージ
4. マクロ vs 関数 (`#define` の影響)
5. ヘッダの役割 / `<>` vs `""` / インクルードガード
6. 整数型 (`size_t`, `ssize_t`, `off_t`, `uint8_t/16/32/64`, `tcflag_t`)
7. signed vs unsigned char、 暗黙符号
8. ポインタの基礎 (`&`, `*`, `->`, `.`)
9. struct と値コピー (既出を統合)
10. `typedef enum` / `typedef struct` イディオム
11. `const` / `volatile`
12. 関数ポインタ記法
13. C 宣言の読み方
14. NULL の正体
15. C の規格 (C89/99/11/17/23) と本連載前提 C11
16. designated initializer `{0}`, `{.field=v}`
17. 関数内の `static` (静的記憶域)
18. inline / static inline

### B. 文字 / バイト / 数 (8 項目)
19. char vs byte / null-terminated 文字列
20. 数値リテラル (10/0x/0/0b)
21. エスケープシーケンス (`\n`, `\r`, `\0`, `\x1b`)
22. ANSI escape sequence の基礎 (`\x1b[H`, `\x1b[2J`, `\x1b[?25l`, `\x1b[r;cH`)
23. format 指定子 (`%d`, `%c`, `%02X`, `%zd`, `%p`)
24. big endian / little endian (第 10 章予備)
25. ビット演算 / bitmask (既出を統合)
26. アライメント (第 2 章 sizeof 予備)

### C. 標準ライブラリ (7 項目)
27. stdio バッファリング (行 / 全 / 無)
28. stdin / stdout / stderr ↔ fd 0/1/2
29. fflush
30. scanf の罠 (先頭スペース / 残留)
31. perror / strerror / errno
32. exit / _exit / return from main
33. EXIT_SUCCESS / EXIT_FAILURE / 終了コード規約

### D. POSIX / syscall / OS (10 項目)
34. syscall とは / `man 2`
35. fd (file descriptor)
36. STDIN_FILENO / STDOUT_FILENO / STDERR_FILENO
37. `read(2)` / `write(2)` の基本 (戻り値、 short read)
38. `open(2)` のフラグ (O_RDONLY / O_WRONLY / O_CREAT / O_NONBLOCK)
39. man セクション (1/2/3/7)
40. POSIX とは / `_POSIX_C_SOURCE`
41. シェルのパイプ / リダイレクト (`|`, `<`, `>`, `2>`, `2>&1`)
42. 環境変数 (PATH / TERM / LANG)
43. C のメモリレイアウト (text / data / bss / heap / stack — 第 2 章予備)

### E. プロセスと signal (7 項目)
44. プロセス / PID / 親子 (`fork(2)` 予備)
45. signal の概念 / 非同期通知 / デフォルト動作
46. SIGINT / SIGTERM / SIGKILL / SIGQUIT / SIGSEGV 一覧
47. signal handler 登録 (`signal(2)` vs `sigaction(2)`)
48. async-signal-safe
49. **冪等性 (idempotence)**
50. シェルの終了コード規約 (0/1/130/137)

### F. ターミナル / TTY (6 項目)
51. TTY の正体
52. terminal emulator vs tty vs pty
53. line discipline
54. canonical vs non-canonical
55. ANSI escape まとめ表
56. キーボード→プロセスのバイト経路

### G. 調査道具 (8 項目)
57. gcc/clang フラグ
58. make 最小知識
59. Docker / `./dx`
60. strace
61. gdb 最小
62. valgrind
63. objdump / readelf / nm
64. asciinema

## 第 1 章 (termios.md) の改修

### 冒頭に「本章で前提とする知識」 セクション追加

intro へのリンク集 (該当 anchor):
- A 群: TU/static, struct value copy, typedef
- B 群: ANSI escape, bitmask, escape sequence, format 指定子
- C 群: stdio buffering, fflush, scanf 罠, perror, errno
- D 群: syscall, fd, STDIN_FILENO, read(2), POSIX
- E 群: signal handler, async-signal-safe, 冪等性
- F 群: TTY, line discipline, canonical mode

### 既存 `:::details` と intro の重複整理

- 「**ヘッダ概説**」 「**read(2) 基本**」 「**fflush の意味**」 など 汎用的なものは intro へ移動
- termios 文脈固有 (`tcgetattr/tcsetattr` のシグネチャ、 `TCSAFLUSH` の使い分け、 `VMIN/VTIME` 早見表、 「VMIN/count/倉庫/バッファ 誤解集」、 「時系列具体例」、 「ビットマスク」、 「struct 値コピー」 のうち termios 例ベースの部分) は本章に残す

### s5_full 段階提示への書き直し ← user 指摘の最重要

#### 現状の問題
`restore`/`on_signal`/`enter_raw` だけ抜粋。 `read_key`/`draw_at`/`msleep`/`main` ループは「全文はリポを参照」 で省略 → **記事だけでは書けない**。

#### 解決: Step 5 を 5 mini-section に分割

```
Step 5a: グローバル状態 + restore 関数
  - static struct termios g_orig; static int g_raw = 0;
  - static void restore(void) 完全コード
  - 解説: static の意味 → intro 参照、 冪等性 → intro 参照、
          write を選ぶ理由、 ANSI escape は intro 参照
  - 動作確認: コンパイル通過 (この時点で main は空)

Step 5b: signal handler (on_signal) 完全コード
  - signal イディオム解説 (DFL に戻して raise)
  - 単独動作確認不可、 5c とセット

Step 5c: enter_raw 完全コード
  - atexit / sigaction / 全ビット OFF / VMIN=0 への遷移
  - 「VMIN=0 になぜ切り替えるか」 はここで詳述
  - 動作確認: 簡易 main (raw mode → 1 byte read → restore) で
              s4_isig と同等の挙動を確認

Step 5d: 矢印キー判定
  - typedef enum { KEY_NONE, KEY_UP, ... } Key;
  - static Key read_key(void) 完全コード
  - 解説: ESC [ X の 3 byte シーケンスのパース、 VMIN=0 で
          途中で read が空返りした場合の扱い
  - 動作確認: read_key の戻り値を printf で表示する仮 main で確認

Step 5e: 描画 + メインループ
  - static void draw_at(int row, int col, char ch) 完全コード
    (snprintf → write イディオム解説)
  - static void msleep(int ms) 完全コード
    (nanosleep + timespec、 ナノ秒単位の理由)
  - int main(void) 完全コード
    (初期位置 / メインループ / key dispatch / redraw / break)
  - 動作確認: @ が矢印キーで動く、 q/Ctrl-C で抜けてシェル正常
```

各 mini-section の末尾に **その時点までの累積コード全体** を `:::details 現在の main.c 全体` で添える。 読者は「今書いてるファイルがこうなってるはず」 を確認できる。

## 実装フェーズ

### Phase 1: intro.md 充実 (前提辞典 64 項目)
- A〜G を `:::details` ベースで追加
- 既存 intro 本文と統合 (重複削除)
- 通読/拾い読み両対応の構成
- スタイル: 最小定義 + 1 文の重要性 + 「もっと知るなら...」 で打ち切り

### Phase 2: 第 1 章 termios.md 前提リンク化
- 冒頭に「本章で前提とする知識」 セクション追加
- 既存 `:::details` のうち汎用部分は intro に移し、 文脈固有のみ残す

### Phase 3: 第 1 章 s5_full 段階提示への書き直し
- Step 5 を 5a-5e の mini-section に分割
- 各セクションで完全コード + 動作確認 + 累積コード折りたたみ

### Phase 4: 他章 (2-12) への波及
- 1 章ずつ前提リンク化 + 抜粋解消
- 1 章単位で commit / push
- 質感を見ながら順次

### Phase 5: code repo 側の整合確認
- ソース ↔ 記事の同期
- Phase 3 の関数別解説に合わせてコード内コメントが整合してるか確認

### Phase 6: README / 連載トップページ整合
- intro.md の前提辞典が見える形 (TOC で目立つ位置)
- 章リストの説明文を最新化

## 完了の判定基準

- [ ] intro.md だけで前提概念全 64 項目が読める
- [ ] 各章を **記事だけ** 読んで動くコードが書ける (= 抜粋禁止)
- [ ] 全章冒頭に「本章の前提」 リンク集
- [ ] zenn repo に全 push 完了
- [ ] code repo はソース更新のみ (記事は zenn repo の単独管理)

## 注意 / 落とし穴

### 重複の整理
intro と各章で同じ概念を扱う場合、 **intro 優先**。 章の `:::details` は intro へのリンク + 章固有の補足のみ。

### スコープクリープ
intro が教科書化しないこと。 1 概念 1〜2 段落の辞書スタイルを徹底。 深掘りは別記事 (前提知識用記事) を将来作る選択肢を残す。

### Phase 4 の章順
Phase 3 完了後の「質感」 で他章の作業量見積もりが変わる。 一度ペースを止めて確認してから本格展開。

### code repo との同期コスト
記事と コードの 二重 管理は前回廃止したので、 今回も「記事は zenn のみ」 を維持。 ただし Phase 3 で関数別解説に合わせてコメント補強が要るなら code repo も更新。

## 工数見積もり (概算)

| Phase | 内容 | 工数 |
|-------|------|------|
| 1 | intro 64 項目 | 6-8h |
| 2 | termios 前提リンク化 + 重複整理 | 2-3h |
| 3 | termios Step 5 段階提示 | 3-4h |
| 4 | 他章 12 本 | 章あたり 1-2h × 12 = 12-24h |
| 5 | code repo 整合 | 1-2h |
| 6 | README 整合 | 0.5h |
| **計** | | **25-42h** |

全部を一気通貫はしない方針。 Phase 1-3 完了 → 質感確認 → Phase 4 へ判断。
