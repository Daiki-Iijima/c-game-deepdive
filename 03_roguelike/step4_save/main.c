/*
 * 03_roguelike/step4_save/main.c — 第 10 章 (save file の罠と明示シリアライズ)
 *
 * 学習材料:
 *   - 構造体を fwrite で直書きすると padding が混ざる/endian が固定される事故
 *   - 明示的に bytewise 書く版を提供
 *   - --save / --load サブコマンドで両方の挙動を再現
 */
#include <stdint.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* バイト順を host から little-endian へ・逆向きへ変換する関数を、
   `<endian.h>` (Linux glibc 提供の htole64 等) に頼らず bytewise で書く。
   理由: 「host のバイト順に依存しない」 を実装で示すこと自体が本章の教材。

   仕組み:
     - v からシフトと AND で 1 byte ずつ取り出し
     - r のメモリに 「最下位 byte が先頭」 の順で書き込む
   こうして得た r をそのまま fwrite すれば、 host の endian に関わらず
   ファイル中身は LE で固定される。

   ポインタキャスト `(uint8_t *)&r`:
     r のアドレスを取って uint8_t* として解釈し直す = 「r の中身に
     byte 単位でアクセスする」 イディオム。 strict aliasing rule の例外として
     char/uint8_t 越しの読み書きは規格上 OK (実装定義ではなく規格保証)。 */
static uint16_t to_le16(uint16_t v) {
    uint16_t r;
    uint8_t *p = (uint8_t *)&r;
    p[0] = (uint8_t)(v       & 0xFF);  /* 最下位 byte */
    p[1] = (uint8_t)((v >> 8) & 0xFF); /* 次の byte */
    return r;
}
static uint64_t to_le64(uint64_t v) {
    uint64_t r;
    uint8_t *p = (uint8_t *)&r;
    for (int i = 0; i < 8; i++) p[i] = (uint8_t)((v >> (i * 8)) & 0xFF);
    return r;
}
static uint16_t from_le16(uint16_t v) {
    const uint8_t *p = (const uint8_t *)&v;
    return (uint16_t)(p[0] | ((uint16_t)p[1] << 8));
}
static uint64_t from_le64(uint64_t v) {
    const uint8_t *p = (const uint8_t *)&v;
    uint64_t r = 0;
    for (int i = 0; i < 8; i++) r |= (uint64_t)p[i] << (i * 8);
    return r;
}

/* 観察用に「混ざった型」 を意図的に持つ構造体 */
typedef struct {
    uint8_t  version;       /* 1 byte */
    /* ↑ ここに 7 byte の padding が走る (uint64_t score の前) */
    uint64_t score;         /* 8 byte */
    uint16_t player_r;      /* 2 byte */
    uint16_t player_c;      /* 2 byte */
    /* ↑ ここに 4 byte の padding が走る? (次の uint64_t を 8-align するため) */
    uint64_t playtime_ms;
    uint8_t  flags;
} Save;

#define MAGIC "RGSV"   /* 4 bytes */

/* バージョン 1: 構造体直書き = 罠版

   fopen(path, mode):
     ファイルを開いて FILE* を返す高レベル I/O。 内部では open(2) syscall。
     mode の意味:
       "r"  = 読み込み専用
       "w"  = 書き込み (既存内容は破棄)
       "a"  = 追記
       "b"  = バイナリモード (Windows でのみ意味あり。 Unix では無視)
   fwrite(ptr, size, nmemb, fp):
     ptr の指す領域から size バイトを nmemb 個分、 fp に書く。
     戻り値は **実際に書き込めた nmemb の個数** (バイト数ではない)。 */
static int save_naive(const Save *s, const char *path) {
    FILE *fp = fopen(path, "wb");
    if (!fp) { perror(path); return -1; }
    if (fwrite(MAGIC, 1, 4, fp) != 4) { fclose(fp); return -1; }
    /* sizeof(*s) = Save 構造体全体のサイズ (= padding 込み)。
       これを 1 個丸ごと書くと、 padding バイトもファイルに混ざる。 */
    if (fwrite(s, sizeof(*s), 1, fp) != 1) { fclose(fp); return -1; }
    fclose(fp);
    return 0;
}
static int load_naive(Save *out, const char *path) {
    FILE *fp = fopen(path, "rb");
    if (!fp) { perror(path); return -1; }
    char m[4];
    if (fread(m, 1, 4, fp) != 4 || memcmp(m, MAGIC, 4) != 0) { fclose(fp); return -1; }
    if (fread(out, sizeof(*out), 1, fp) != 1) { fclose(fp); return -1; }
    fclose(fp);
    return 0;
}

/* バージョン 2: 明示シリアライズ (host order に依存しない) */
static void wr_u8 (FILE *fp, uint8_t  v) { uint8_t b[1] = { v }; fwrite(b, 1, 1, fp); }
static void wr_u16(FILE *fp, uint16_t v) {
    uint16_t le = to_le16(v);
    fwrite(&le, sizeof(le), 1, fp);
}
static void wr_u64(FILE *fp, uint64_t v) {
    uint64_t le = to_le64(v);
    fwrite(&le, sizeof(le), 1, fp);
}
static int  rd_u8 (FILE *fp, uint8_t  *v) { return fread(v, 1, 1, fp) == 1 ? 0 : -1; }
static int  rd_u16(FILE *fp, uint16_t *v) {
    uint16_t le; if (fread(&le, sizeof(le), 1, fp) != 1) return -1;
    *v = from_le16(le); return 0;
}
static int  rd_u64(FILE *fp, uint64_t *v) {
    uint64_t le; if (fread(&le, sizeof(le), 1, fp) != 1) return -1;
    *v = from_le64(le); return 0;
}

static int save_explicit(const Save *s, const char *path) {
    FILE *fp = fopen(path, "wb");
    if (!fp) { perror(path); return -1; }
    fwrite(MAGIC, 1, 4, fp);
    wr_u8 (fp, s->version);
    wr_u64(fp, s->score);
    wr_u16(fp, s->player_r);
    wr_u16(fp, s->player_c);
    wr_u64(fp, s->playtime_ms);
    wr_u8 (fp, s->flags);
    fclose(fp);
    return 0;
}
static int load_explicit(Save *out, const char *path) {
    FILE *fp = fopen(path, "rb");
    if (!fp) { perror(path); return -1; }
    char m[4];
    if (fread(m, 1, 4, fp) != 4 || memcmp(m, MAGIC, 4) != 0) { fclose(fp); return -1; }
    if (rd_u8 (fp, &out->version)     < 0) { fclose(fp); return -1; }
    if (rd_u64(fp, &out->score)       < 0) { fclose(fp); return -1; }
    if (rd_u16(fp, &out->player_r)    < 0) { fclose(fp); return -1; }
    if (rd_u16(fp, &out->player_c)    < 0) { fclose(fp); return -1; }
    if (rd_u64(fp, &out->playtime_ms) < 0) { fclose(fp); return -1; }
    if (rd_u8 (fp, &out->flags)       < 0) { fclose(fp); return -1; }
    fclose(fp);
    return 0;
}

static void dump(const Save *s) {
    printf("version     = %u\n", s->version);
    printf("score       = %llu\n", (unsigned long long)s->score);
    printf("player      = (%u, %u)\n", s->player_r, s->player_c);
    printf("playtime_ms = %llu\n", (unsigned long long)s->playtime_ms);
    printf("flags       = 0x%02x\n", s->flags);
    printf("sizeof(Save) = %zu\n", sizeof(Save));
    printf("offsetof(score) = %zu\n", offsetof(Save, score));
    printf("offsetof(playtime_ms) = %zu\n", offsetof(Save, playtime_ms));
}

static void usage(void) {
    fprintf(stderr,
            "usage:\n"
            "  save_step4 inspect\n"
            "  save_step4 save_naive    <path>\n"
            "  save_step4 load_naive    <path>\n"
            "  save_step4 save_explicit <path>\n"
            "  save_step4 load_explicit <path>\n");
}

int main(int argc, char **argv) {
    if (argc < 2) { usage(); return 1; }
    Save sample = {
        .version = 1, .score = 1234567890ULL,
        .player_r = 12, .player_c = 34,
        .playtime_ms = 9876543210ULL, .flags = 0xA5,
    };

    if (strcmp(argv[1], "inspect") == 0) {
        dump(&sample);
        return 0;
    }
    if (argc != 3) { usage(); return 1; }
    if (strcmp(argv[1], "save_naive") == 0)    return save_naive(&sample, argv[2]);
    if (strcmp(argv[1], "save_explicit") == 0) return save_explicit(&sample, argv[2]);

    Save loaded = {0};
    if (strcmp(argv[1], "load_naive") == 0) {
        if (load_naive(&loaded, argv[2]) < 0) { fprintf(stderr, "load failed\n"); return 1; }
        dump(&loaded); return 0;
    }
    if (strcmp(argv[1], "load_explicit") == 0) {
        if (load_explicit(&loaded, argv[2]) < 0) { fprintf(stderr, "load failed\n"); return 1; }
        dump(&loaded); return 0;
    }
    usage(); return 1;
}
