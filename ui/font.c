/* 픽셀 글자. 영문 대문자, 숫자, 몇 가지 기호는 5x7 글꼴을 6x8 칸에 그린다.
 * 한글은 12x12 칸을 차지하고 그림은 ui/assets/hangul12.fnt에서 읽는다.
 *
 * 한글 글꼴 파일은 tools/font_convert.py가 Freesentation 글꼴(SIL OFL 1.1, ui/assets/FONT_LICENSE.txt)로 만든다.
 * 형식(숫자는 리틀 엔디언):
 *   머리 16바이트 : "SSPF", 버전(1), 칸 너비(12), 칸 높이(12), 구간 수 R (각 1바이트),
 *                   글자 수 N (4바이트), 0 (4바이트)
 *   구간표        : R개 x (첫 글자 코드 4바이트, 글자 수 4바이트). 코드가 작은 것부터, 겹치지 않는다
 *   그림          : N개 x 18바이트. 구간표 순서대로 이어진다.
 *                   글자 하나는 144비트이고 왼쪽 위 픽셀부터 줄 단위로, 바이트의 높은 비트가 먼저다
 * 파일은 처음 한글을 그릴 때 한 번 읽는다. 없거나 틀리면 경고를 한 번 남기고 한글을 빈 네모로 그린다.
 *
 * 한글이 섞인 글자열은 한 줄의 높이가 12픽셀이다. 그때 영문과 숫자는 UI_KFONT_LATIN_DY만큼
 * 내려 그려서 한글의 가운데 높이에 맞춘다. 영문만 있는 글자열은 예전과 똑같이 그린다. */
#include "ui/ui_internal.h"
#include "core/log.h"

#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const struct { char ch; uint8_t rows[7]; } glyphs[] = {
    {'A', {0x0E,0x11,0x11,0x1F,0x11,0x11,0x11}}, {'B', {0x1E,0x11,0x11,0x1E,0x11,0x11,0x1E}},
    {'C', {0x0E,0x11,0x10,0x10,0x10,0x11,0x0E}}, {'D', {0x1E,0x11,0x11,0x11,0x11,0x11,0x1E}},
    {'E', {0x1F,0x10,0x10,0x1E,0x10,0x10,0x1F}}, {'F', {0x1F,0x10,0x10,0x1E,0x10,0x10,0x10}},
    {'G', {0x0E,0x11,0x10,0x17,0x11,0x11,0x0F}}, {'H', {0x11,0x11,0x11,0x1F,0x11,0x11,0x11}},
    {'I', {0x0E,0x04,0x04,0x04,0x04,0x04,0x0E}}, {'J', {0x07,0x02,0x02,0x02,0x02,0x12,0x0C}},
    {'K', {0x11,0x12,0x14,0x18,0x14,0x12,0x11}}, {'L', {0x10,0x10,0x10,0x10,0x10,0x10,0x1F}},
    {'M', {0x11,0x1B,0x15,0x15,0x11,0x11,0x11}}, {'N', {0x11,0x19,0x15,0x13,0x11,0x11,0x11}},
    {'O', {0x0E,0x11,0x11,0x11,0x11,0x11,0x0E}}, {'P', {0x1E,0x11,0x11,0x1E,0x10,0x10,0x10}},
    {'Q', {0x0E,0x11,0x11,0x11,0x15,0x12,0x0D}}, {'R', {0x1E,0x11,0x11,0x1E,0x14,0x12,0x11}},
    {'S', {0x0F,0x10,0x10,0x0E,0x01,0x01,0x1E}}, {'T', {0x1F,0x04,0x04,0x04,0x04,0x04,0x04}},
    {'U', {0x11,0x11,0x11,0x11,0x11,0x11,0x0E}}, {'V', {0x11,0x11,0x11,0x11,0x11,0x0A,0x04}},
    {'W', {0x11,0x11,0x11,0x15,0x15,0x1B,0x11}}, {'X', {0x11,0x11,0x0A,0x04,0x0A,0x11,0x11}},
    {'Y', {0x11,0x11,0x0A,0x04,0x04,0x04,0x04}}, {'Z', {0x1F,0x01,0x02,0x04,0x08,0x10,0x1F}},
    {'0', {0x0E,0x11,0x13,0x15,0x19,0x11,0x0E}}, {'1', {0x04,0x0C,0x04,0x04,0x04,0x04,0x0E}},
    {'2', {0x0E,0x11,0x01,0x02,0x04,0x08,0x1F}}, {'3', {0x1E,0x01,0x01,0x0E,0x01,0x01,0x1E}},
    {'4', {0x02,0x06,0x0A,0x12,0x1F,0x02,0x02}}, {'5', {0x1F,0x10,0x1E,0x01,0x01,0x11,0x0E}},
    {'6', {0x06,0x08,0x10,0x1E,0x11,0x11,0x0E}}, {'7', {0x1F,0x01,0x02,0x04,0x08,0x08,0x08}},
    {'8', {0x0E,0x11,0x11,0x0E,0x11,0x11,0x0E}}, {'9', {0x0E,0x11,0x11,0x0F,0x01,0x02,0x0C}},
    {'\'',{0x04,0x04,0x00,0x00,0x00,0x00,0x00}}, {'!', {0x04,0x04,0x04,0x04,0x04,0x00,0x04}},
    {'?', {0x0E,0x11,0x01,0x02,0x04,0x00,0x04}}, {'/', {0x01,0x01,0x02,0x04,0x08,0x10,0x10}},
    {':', {0x00,0x04,0x04,0x00,0x04,0x04,0x00}}, {'.', {0x00,0x00,0x00,0x00,0x00,0x0C,0x0C}},
    {',', {0x00,0x00,0x00,0x00,0x0C,0x04,0x08}}, {'-', {0x00,0x00,0x00,0x1F,0x00,0x00,0x00}},
    {'+', {0x00,0x04,0x04,0x1F,0x04,0x04,0x00}}, {'%', {0x18,0x19,0x02,0x04,0x08,0x13,0x03}},
    {'~', {0x00,0x00,0x08,0x15,0x02,0x00,0x00}},
};

static const uint8_t *find_glyph(int ch)
{
    if (ch >= 'a' && ch <= 'z') ch -= 'a' - 'A';
    for (unsigned i = 0; i < sizeof glyphs / sizeof glyphs[0]; i++)
        if (glyphs[i].ch == ch) return glyphs[i].rows;
    return 0;
}

/* ---- 한글 글꼴 파일 ---- */
#define KFONT_PATH       "ui/assets/hangul12.fnt"
#define KFONT_BYTES      ((UI_KFONT_W * UI_KFONT_H + 7) / 8)
#define KFONT_MAX_RANGES 64
#define KFONT_MAX_GLYPHS 65536u

typedef struct { uint32_t first, count, start; } krange_t;    /* start: 이 구간의 첫 글자가 몇 번째 그림인지 */

static struct {
    int ok;
    int n_ranges;
    krange_t range[KFONT_MAX_RANGES];
    uint8_t *bits;                 /* 글자 수 x KFONT_BYTES. 한 번 읽고 끝까지 둔다 */
} kfont;
static pthread_once_t kfont_once = PTHREAD_ONCE_INIT;

static uint32_t le32(const uint8_t *p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

/* 파일을 읽어 kfont를 채운다. 성공하면 1. 크기와 구간이 하나라도 안 맞으면 쓰지 않는다. */
static int kfont_read(FILE *f)
{
    uint8_t head[16], table[KFONT_MAX_RANGES * 8];
    if (fread(head, 1, sizeof head, f) != sizeof head) return 0;
    if (memcmp(head, "SSPF", 4) != 0 || head[4] != 1) return 0;
    if (head[5] != UI_KFONT_W || head[6] != UI_KFONT_H) return 0;

    int n_ranges = head[7];
    uint32_t n_glyphs = le32(head + 8);
    if (n_ranges < 1 || n_ranges > KFONT_MAX_RANGES || n_glyphs < 1 || n_glyphs > KFONT_MAX_GLYPHS) return 0;
    if (fread(table, 8, (size_t)n_ranges, f) != (size_t)n_ranges) return 0;

    uint32_t total = 0, next_free = 0;
    for (int i = 0; i < n_ranges; i++) {
        uint32_t first = le32(table + i * 8), count = le32(table + i * 8 + 4);
        if (count < 1 || count > n_glyphs - total) return 0;         /* 글자 수를 넘는 구간 */
        if (first < next_free || first > 0x10FFFFu) return 0;        /* 순서가 틀렸거나 겹친다 */
        kfont.range[i] = (krange_t){ first, count, total };
        total += count;
        next_free = first + count;
    }
    if (total != n_glyphs) return 0;

    size_t size = (size_t)n_glyphs * KFONT_BYTES;
    uint8_t *bits = malloc(size);
    if (!bits) return 0;
    if (fread(bits, 1, size, f) != size || fgetc(f) != EOF) {        /* 모자라도, 남아도 틀린 파일이다 */
        free(bits);
        return 0;
    }
    kfont.bits = bits;
    kfont.n_ranges = n_ranges;
    return 1;
}

static void kfont_load(void)
{
    FILE *f = fopen(KFONT_PATH, "rb");
    if (!f) {
        log_warn_once("kfont", "한글 글꼴 파일이 없습니다: %s (한글은 빈 네모로 보입니다)", KFONT_PATH);
        return;
    }
    kfont.ok = kfont_read(f);
    fclose(f);
    if (!kfont.ok)
        log_warn_once("kfont", "한글 글꼴 파일의 형식이 틀렸습니다: %s (한글은 빈 네모로 보입니다)", KFONT_PATH);
}

/* 그 글자의 그림(KFONT_BYTES바이트). 글꼴에 없는 글자면 NULL. */
static const uint8_t *kfont_glyph(uint32_t cp)
{
    for (int i = 0; i < kfont.n_ranges; i++) {
        const krange_t *r = &kfont.range[i];
        if (cp >= r->first && cp - r->first < r->count)
            return kfont.bits + (size_t)(r->start + (cp - r->first)) * KFONT_BYTES;
    }
    return NULL;
}

/* 12x12 칸 하나를 그린다. 글꼴에 없는 글자는 빈칸이다. 글꼴 파일을 못 읽었으면 빈 네모다. */
static void draw_wide(canvas_t *c, int x, int y, int cp, color_t col)
{
    pthread_once(&kfont_once, kfont_load);
    if (!kfont.ok) {
        ui_line(c, x + 1, y + 1, x + 10, y + 1, col);
        ui_line(c, x + 1, y + 10, x + 10, y + 10, col);
        ui_line(c, x + 1, y + 1, x + 1, y + 10, col);
        ui_line(c, x + 10, y + 1, x + 10, y + 10, col);
        return;
    }
    const uint8_t *g = kfont_glyph((uint32_t)cp);
    if (!g) return;
    for (int i = 0; i < UI_KFONT_W * UI_KFONT_H; i++)
        if (g[i >> 3] & (0x80 >> (i & 7)))
            ui_px(c, x + i % UI_KFONT_W, y + i / UI_KFONT_W, col);
}

/* 12x12 칸을 차지하는 글자(한글 등)가 하나라도 있는가 */
static int has_wide(const char *s)
{
    int cp;
    while ((cp = utf8_next(&s)) != 0)
        if (cp >= 128) return 1;
    return 0;
}

void ui_text(canvas_t *c, int x, int y, const char *s, color_t col)
{
    if (!c || !s) return;
    int latin_y = y + (has_wide(s) ? UI_KFONT_LATIN_DY : 0);
    int cp;
    while ((cp = utf8_next(&s)) != 0) {
        if (cp < 128) {
            const uint8_t *g = find_glyph(cp);
            for (int r = 0; g && r < 7; r++)
                for (int k = 0; k < 5; k++)
                    if (g[r] & (0x10 >> k)) ui_px(c, x + k, latin_y + r, col);
            x += UI_FONT_W;
        } else {
            draw_wide(c, x, y, cp, col);
            x += UI_KFONT_W;
        }
    }
}

int ui_text_h(const char *s)
{
    return (s && has_wide(s)) ? UI_KFONT_H : UI_FONT_H;
}

int ui_text_w(const char *s)
{
    int w = 0, cp;
    if (!s) return 0;
    while ((cp = utf8_next(&s)) != 0)
        w += (cp < 128) ? UI_FONT_W : UI_KFONT_W;
    return w;
}
