# Plan: 章のステップバイステップ化

承認日: 2026-05-11
適用範囲: まず **Chapter 1 (termios)** だけデモ展開 → 質感確認 → 他章へ
方針: 完成版コードは各章末尾の `_final/` として残す (二段構え)

## 動機

現状の各章は「完成版 main.c を一気に提示 → 抜粋を解説」 のスタイル。
C は変数 1 つの初期化忘れで端末が壊れたり、 ポインタの → と . の違いで
コンパイラエラーになったりと、 中級者には「一気どかっ」 が辛い。

提案: 各章を **動くミニステップ** に分割し、 1 step = 1 機能追加。
読者は各 step で `make run` して動作確認 → 次 step との diff を読んで進む。

## Chapter 1 (termios) のステップ分割案

repo パス:
```
01_snake/step1_termios/
├── s1_scanf/          (旧 step1 の前段)
│   ├── main.c         (~40 行: scanf でキー入力)
│   └── Makefile
├── s2_canon/
│   ├── main.c         (~70 行: ICANON OFF だけ)
│   └── Makefile
├── s3_echo/
│   ├── main.c         (~80 行: + ECHO OFF)
│   └── Makefile
├── s4_isig/
│   ├── main.c         (~90 行: + ISIG OFF + 0x03 を自前で扱う)
│   └── Makefile
├── s5_full/           (= 既存の main.c 内容)
│   ├── main.c         (~140 行: + atexit + sigaction で復元)
│   └── Makefile
└── _final/            (省略可、 s5 と同内容)
```

### 各 step の到達点

| step | 動作 | 体感ポイント | 残る痛み |
|------|------|--------------|----------|
| s1_scanf | scanf で 1 文字読んで HUD に表示 | Enter 押さないと反応しない | ゲームに不適切 |
| s2_canon | ICANON OFF。 1 文字で反応 | 押した瞬間に動く! | 押したキーが画面に echo されてうるさい |
| s3_echo | + ECHO OFF | 静か | Ctrl-C で死ぬ、 端末がそのまま raw に残るリスクは無い (まだ restore してないが影響軽微) |
| s4_isig | + ISIG OFF、 0x03 (Ctrl-C) を自前 q として扱う | キーボード支配感 | 異常終了したら端末壊れる |
| s5_full | + atexit(restore) + sigaction(SIGINT, ...) | 完成 (= 現状の main.c) | — |

### 各 step の main.c 雛形 (s1 だけ展開、 他は同パターン)

```c
/* s1_scanf/main.c */
#include <stdio.h>
int main(void) {
    printf("press a key then Enter: ");
    char c; scanf(" %c", &c);
    printf("you pressed: '%c' (0x%02X)\n", c, (unsigned char)c);
    return 0;
}
```

```c
/* s2_canon/main.c — termios 構造体登場、 ICANON だけ OFF */
#include <stdio.h>
#include <termios.h>
#include <unistd.h>
int main(void) {
    struct termios orig, raw;
    tcgetattr(STDIN_FILENO, &orig);
    raw = orig;
    raw.c_lflag &= ~ICANON;     /* 行編集 OFF */
    raw.c_cc[VMIN] = 1;          /* 1 byte 来るまでブロック */
    raw.c_cc[VTIME] = 0;
    tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw);

    printf("press a key (no Enter needed): ");
    char c; read(STDIN_FILENO, &c, 1);
    printf("\nyou pressed: '%c' (0x%02X)\n", c, (unsigned char)c);

    tcsetattr(STDIN_FILENO, TCSAFLUSH, &orig);   /* 手で復元 */
    return 0;
}
```

(s3〜s5 は本章本文で逐次解説。 各 step の差分は git diff で全部見える)

## 章本文 (`books/c-game-deepdive/termios.md`) 改稿方針

現状の章は「完成版コード抜粋 + 解説」 が主軸。 step 化後はこう変える:

1. **はじめに** (現状維持)
2. **本章のテーマ: termios** (現状維持、 ただし「5 ステップで作る」 と予告)
3. **Step 1/5: scanf の限界を見る** (旧 「実装する」 ブロックの 1/5 サイズ)
   - `make -C s1_scanf run`
   - 何が痛いか体感 (Enter 必須)
4. **Step 2/5: ICANON を OFF にする**
   - s2_canon の main.c を全部貼る (40 行程度)
   - 動作と痛みを言葉に
5. **Step 3/5: ECHO を OFF にする**
   - s2 → s3 の **diff だけ** を貼る (-5+5 行程度)
   - diff を読むトレーニングにもなる
6. **Step 4/5: ISIG を OFF にして Ctrl-C を自前で**
7. **Step 5/5: atexit + sigaction で端末を救う**
8. **観察: strace で全行程を見る** (現状維持)
9. **メンタルモデルを整理する** (現状維持)
10. **演習** (現状維持)
11. **次章では**

### diff の貼り方 (Zenn 流)

```` ```diff:s3_echo/main.c ````
形式で書くと Zenn は + 行を緑、 - 行を赤でハイライトしてくれる。

例:
````
```diff:s3_echo/main.c
     raw.c_lflag &= ~ICANON;
+    raw.c_lflag &= ~ECHO;
```
````

## 作業手順 (次セッションでやること)

1. `01_snake/step1_termios/main.c` を `s5_full/main.c` に **移動** (= 既存完成品の名前変更)
2. `s1_scanf/`, `s2_canon/`, `s3_echo/`, `s4_isig/` のディレクトリを作り、 各 main.c を新規作成
3. 各 step に Makefile を配置 (既存の Makefile 雛形をコピーしてバイナリ名だけ変える)
4. ルート Makefile の `CHAPTERS :=` リストを更新 (新規 step を追加)
5. Docker で 5 step すべてビルド + 動作確認
6. `books/c-game-deepdive/termios.md` を書き直し:
   - Step 1〜5 のセクションを追加
   - 各 step に対応する main.c もしくは diff を本文に埋め込む
   - 章末の 「観察」 「演習」 はそのまま
7. zenn repo にも同期 push
8. README の章リストに s1〜s5 の構造を反映 (任意)

## 工数見積もり

- 各 step main.c: 10〜30 分 × 5 = 60〜120 分
- Makefile 5 個: 10 分
- 章本文書き直し: 60 分
- ビルド検証 + cleanup: 15 分
- 両 repo commit + push: 10 分

**計: 約 2.5〜3 時間**

質感が OK なら、 他章にも展開:
- Chapter 2 (snake-array): 4〜5 ステップ (固定配列 → struct 導入 → padding 観察 → HUD)
- Chapter 3 (snake-list): 5〜6 ステップ (malloc/free → 二重 free → valgrind)
- ... (全 13 章で計 50〜70 サブステップになる見込み、 1 章 ~3 時間 × 13 章 = 40 時間規模)

## 注意 / 落とし穴

- **既存リンク**: 連載本文に `01_snake/step1_termios/main.c` という path を
  直接参照している箇所がある。 step 化で path が変わるので grep + 一括置換が必要。
- **Makefile.common**: step ごとに `REPO_ROOT := ../../..` (= 1 階層深い) に変更が必要。
  これは `01_snake/step1_termios/s1_scanf/Makefile` の話。
- **章間ナビ**: Zenn book は config.yaml の chapter 順で自動ナビ。 章数自体は変えないので OK。
- **diff の渡し方**: Zenn は diff コードブロックに行番号がつかない。 大きい diff は file path 付きの完全コードを貼る方が読みやすい場合もある。
- **「動くけど bug がある」 段階**: s2_canon 等は **意図的に不完全**。 章本文で
  「ここまでで何ができて何が痛いか」 を明確にすること。 完成 step (s5) と勘違いされる事故防止。
- **完成版 (`_final/`)**: 推奨案では「 s5_full = 完成版」 で済むので、 _final/ は省略してよい。
  もし「step 全部スキップして完成版だけ見たい人」 のために残すなら s5 と同内容の symlink で十分。

## 完了の判定基準

Chapter 1 のステップ化は次の全てを満たしたら完成:

- [ ] 5 step ディレクトリ全部ビルド成功 (`./dx make -C 01_snake/step1_termios/sN_*`)
- [ ] 各 step `make run` で動作確認 (s1=Enter 必須、 s2=即時 + echo、 ..., s5=完成)
- [ ] termios.md が 5 step 構成で書き直されている
- [ ] zenn repo + code repo 両方に push 済み
- [ ] README の章リストが現状と整合 (任意)

質感を見て、 他章への展開を別計画に。
