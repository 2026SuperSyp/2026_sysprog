/* 실제 픽셀로 그리는 화면: 그림(image_t), 가장자리가 부드러운 도형, 큰 글꼴, 캔버스 키우기. 설명은 hd.h에 있다. */
#include "ui/hd.h"
#include "core/log.h"

#include <math.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ---- 그림 ------------------------------------------------------------------------- */
image_t *image_new(int w, int h)
{
    image_t *im = malloc(sizeof *im);
    if (!im || w < 1 || h < 1) { free(im); return NULL; }
    im->w = w;
    im->h = h;
    im->px = calloc((size_t)w * (size_t)h, 3);
    if (!im->px) { free(im); return NULL; }
    return im;
}

void image_free(image_t *im)
{
    if (!im) return;
    free(im->px);
    free(im);
}

rgb_t hd_color(color_t c)
{
    uint8_t v[3];
    ui_palette_rgb(c, v);
    return (rgb_t){ v[0], v[1], v[2] };
}

rgb_t hd_mix(rgb_t a, rgb_t b, float t)
{
    if (t <= 0.0f) return a;
    if (t >= 1.0f) return b;
    return (rgb_t){ (uint8_t)((float)a.r + ((float)b.r - (float)a.r) * t),
                    (uint8_t)((float)a.g + ((float)b.g - (float)a.g) * t),
                    (uint8_t)((float)a.b + ((float)b.b - (float)a.b) * t) };
}

void hd_fill(image_t *im, rgb_t c)
{
    if (!im) return;
    uint8_t *p = im->px;
    for (size_t i = (size_t)im->w * (size_t)im->h; i > 0; i--, p += 3) { p[0] = c.r; p[1] = c.g; p[2] = c.b; }
}

/* ---- 도형 ------------------------------------------------------------------------- */
/* 도형마다 픽셀의 가운데에서 가장자리까지의 거리 d(안쪽이 음수)를 구하고, 0.5 - d만큼(0~1) 칠한다.
 * 가장자리에 걸친 픽셀이 덮인 만큼만 섞여서 계단이 보이지 않는다. */
static int clip_box(const image_t *im, float x0, float y0, float x1, float y1, int *ax, int *ay, int *bx, int *by)
{
    *ax = (int)floorf(x0); *ay = (int)floorf(y0);
    *bx = (int)ceilf(x1);  *by = (int)ceilf(y1);
    if (*ax < 0) *ax = 0;
    if (*ay < 0) *ay = 0;
    if (*bx > im->w) *bx = im->w;
    if (*by > im->h) *by = im->h;
    return *ax < *bx && *ay < *by;
}

static void rrect(image_t *im, float x, float y, float w, float h, float radius, rgb_t top, rgb_t bot, int grad, float a)
{
    int ax, ay, bx, by;
    if (!im || w <= 0.0f || h <= 0.0f || a <= 0.0f) return;
    float hw = w * 0.5f, hh = h * 0.5f, cx = x + hw, cy = y + hh;
    if (radius > hw) radius = hw;
    if (radius > hh) radius = hh;
    if (!clip_box(im, x, y, x + w, y + h, &ax, &ay, &bx, &by)) return;
    for (int py = ay; py < by; py++) {
        float dy = fabsf((float)py + 0.5f - cy) - (hh - radius);
        rgb_t c = grad ? hd_mix(top, bot, ((float)py + 0.5f - y) / h) : top;
        for (int px = ax; px < bx; px++) {
            float dx = fabsf((float)px + 0.5f - cx) - (hw - radius), d;
            if (dx > 0.0f && dy > 0.0f) d = sqrtf(dx * dx + dy * dy) - radius;
            else d = (dx > dy ? dx : dy) - radius;
            float cov = 0.5f - d;
            if (cov <= 0.0f) continue;
            hd_blend(im, px, py, c, cov >= 1.0f ? a : a * cov);
        }
    }
}

void hd_rrect(image_t *im, float x, float y, float w, float h, float radius, rgb_t c, float a)
{
    rrect(im, x, y, w, h, radius, c, c, 0, a);
}

void hd_vgrad(image_t *im, float x, float y, float w, float h, float radius, rgb_t top, rgb_t bot, float a)
{
    rrect(im, x, y, w, h, radius, top, bot, 1, a);
}

void hd_ellipse(image_t *im, float cx, float cy, float rx, float ry, rgb_t c, float a)
{
    int ax, ay, bx, by;
    if (!im || rx <= 0.0f || ry <= 0.0f || a <= 0.0f) return;
    if (!clip_box(im, cx - rx - 1.0f, cy - ry - 1.0f, cx + rx + 1.0f, cy + ry + 1.0f, &ax, &ay, &bx, &by)) return;
    for (int py = ay; py < by; py++) {
        float ny = ((float)py + 0.5f - cy) / ry;
        for (int px = ax; px < bx; px++) {
            float nx = ((float)px + 0.5f - cx) / rx, f2 = nx * nx + ny * ny, cov = 1.0f;
            if (f2 > 0.25f) {
                /* 타원의 가장자리까지의 거리는 (f - 1)을 그 자리의 기울기로 나눈 값에 가깝다 */
                float f = sqrtf(f2), gx = nx / rx, gy = ny / ry, g = sqrtf(gx * gx + gy * gy) / f;
                cov = 0.5f - (f - 1.0f) / (g > 1e-6f ? g : 1e-6f);
            }
            if (cov <= 0.0f) continue;
            hd_blend(im, px, py, c, cov >= 1.0f ? a : a * cov);
        }
    }
}

void hd_line(image_t *im, float x0, float y0, float x1, float y1, float thick, rgb_t c, float a)
{
    int ax, ay, bx, by;
    if (!im || a <= 0.0f) return;
    float r = thick * 0.5f, vx = x1 - x0, vy = y1 - y0, len2 = vx * vx + vy * vy;
    if (!clip_box(im, (x0 < x1 ? x0 : x1) - r - 1.0f, (y0 < y1 ? y0 : y1) - r - 1.0f,
                  (x0 > x1 ? x0 : x1) + r + 1.0f, (y0 > y1 ? y0 : y1) + r + 1.0f, &ax, &ay, &bx, &by)) return;
    for (int py = ay; py < by; py++) {
        for (int px = ax; px < bx; px++) {
            float wx = (float)px + 0.5f - x0, wy = (float)py + 0.5f - y0;
            float t = len2 > 1e-6f ? (wx * vx + wy * vy) / len2 : 0.0f;
            t = t < 0.0f ? 0.0f : (t > 1.0f ? 1.0f : t);
            float ex = wx - vx * t, ey = wy - vy * t, cov = 0.5f - (sqrtf(ex * ex + ey * ey) - r);
            if (cov <= 0.0f) continue;
            hd_blend(im, px, py, c, cov >= 1.0f ? a : a * cov);
        }
    }
}

void hd_ring(image_t *im, float cx, float cy, float r, float thick, rgb_t c, float a)
{
    int ax, ay, bx, by;
    if (!im || r <= 0.0f || a <= 0.0f) return;
    float o = r + thick * 0.5f + 1.0f;
    if (!clip_box(im, cx - o, cy - o, cx + o, cy + o, &ax, &ay, &bx, &by)) return;
    for (int py = ay; py < by; py++) {
        for (int px = ax; px < bx; px++) {
            float dx = (float)px + 0.5f - cx, dy = (float)py + 0.5f - cy;
            float cov = 0.5f - (fabsf(sqrtf(dx * dx + dy * dy) - r) - thick * 0.5f);
            if (cov <= 0.0f) continue;
            hd_blend(im, px, py, c, cov >= 1.0f ? a : a * cov);
        }
    }
}

/* ---- 큰 글꼴 ----------------------------------------------------------------------- */
/* 파일 형식은 tools/font_hd.py에 있다. 처음 글자를 그릴 때 한 번 읽어 글자마다 진하기(0~255)의 그림으로 풀어 둔다.
 * 파일이 없거나 틀리면 경고를 한 번 남기고, 모든 글자를 12x12 글꼴을 키워서 그린다. */
#define HDFONT_PATH "ui/assets/font48.fnt"
#define HDFONT_MAX  8192u

static struct {
    int ok, n, ww, wh, nw, nh;
    uint32_t *cp;                /* 글자 코드. 작은 것부터 */
    uint8_t **glyph;             /* 글자마다 칸 하나의 진하기 */
    uint8_t *pixels;
} hf;
static pthread_once_t hf_once = PTHREAD_ONCE_INIT;

static uint32_t rd32(const uint8_t *p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

static int hf_read(const uint8_t *file, size_t size)
{
    if (size < 16 || memcmp(file, "SSPH", 4) != 0 || file[4] != 1) return 0;
    int ww = file[5], wh = file[6], nw = file[7], nh = file[8];
    uint32_t n = rd32(file + 12);
    if (ww < 8 || wh < 8 || nw < 4 || nh < 4 || n < 1 || n > HDFONT_MAX || size < 16 + (size_t)n * 8) return 0;
    const uint8_t *table = file + 16, *data = table + (size_t)n * 8;
    size_t data_size = size - 16 - (size_t)n * 8, total = 0;
    for (uint32_t i = 0; i < n; i++) total += rd32(table + i * 8) < 128 ? (size_t)nw * (size_t)nh : (size_t)ww * (size_t)wh;

    uint32_t *cp = malloc(sizeof *cp * n);
    uint8_t **glyph = malloc(sizeof *glyph * n), *pixels = malloc(total), *out = pixels;
    int good = cp && glyph && pixels;
    for (uint32_t i = 0; good && i < n; i++) {
        uint32_t code = rd32(table + i * 8), at = rd32(table + i * 8 + 4);
        size_t want = code < 128 ? (size_t)nw * (size_t)nh : (size_t)ww * (size_t)wh, got = 0;
        if ((i > 0 && code <= cp[i - 1]) || at > data_size) { good = 0; break; }      /* 순서가 틀렸거나 자리가 파일 밖이다 */
        cp[i] = code;
        glyph[i] = out;
        while (got < want && at < data_size) {
            int run = (data[at] >> 4) + 1, v = (data[at] & 15) * 17;
            at++;
            if (got + (size_t)run > want) { good = 0; break; }
            memset(out + got, v, (size_t)run);
            got += (size_t)run;
        }
        if (got != want) good = 0;
        out += want;
    }
    if (!good) { free(cp); free(glyph); free(pixels); return 0; }
    hf.n = (int)n; hf.ww = ww; hf.wh = wh; hf.nw = nw; hf.nh = nh;
    hf.cp = cp; hf.glyph = glyph; hf.pixels = pixels;
    return 1;
}

static void hf_load(void)
{
    FILE *f = fopen(HDFONT_PATH, "rb");
    if (!f) { log_warn_once("hdfont", "큰 글꼴 파일이 없습니다: %s (12픽셀 글꼴을 키워서 그립니다)", HDFONT_PATH); return; }
    uint8_t *file = NULL;
    long size = 0;
    if (fseek(f, 0, SEEK_END) == 0 && (size = ftell(f)) > 0 && size < (16L << 20) && fseek(f, 0, SEEK_SET) == 0)
        file = malloc((size_t)size);
    if (file && fread(file, 1, (size_t)size, f) == (size_t)size) hf.ok = hf_read(file, (size_t)size);
    free(file);
    fclose(f);
    if (!hf.ok) log_warn_once("hdfont", "큰 글꼴 파일의 형식이 틀렸습니다: %s (12픽셀 글꼴을 키워서 그립니다)", HDFONT_PATH);
}

static const uint8_t *hf_glyph(uint32_t cp)
{
    int lo = 0, hi = hf.n - 1;
    while (lo <= hi) {
        int mid = (lo + hi) / 2;
        if (hf.cp[mid] == cp) return hf.glyph[mid];
        if (hf.cp[mid] < cp) lo = mid + 1; else hi = mid - 1;
    }
    return NULL;
}

/* 진하기 그림에서 (sx, sy) 자리의 값(0~255). 가까운 네 점을 섞고, 그림 밖은 0이다 */
static float tap(const uint8_t *src, int sw, int sh, float sx, float sy)
{
    int x0 = (int)floorf(sx), y0 = (int)floorf(sy);
    float fx = sx - (float)x0, fy = sy - (float)y0, v[4];
    for (int k = 0; k < 4; k++) {
        int x = x0 + (k & 1), y = y0 + (k >> 1);
        v[k] = (x < 0 || y < 0 || x >= sw || y >= sh) ? 0.0f : (float)src[y * sw + x];
    }
    return (v[0] * (1.0f - fx) + v[1] * fx) * (1.0f - fy) + (v[2] * (1.0f - fx) + v[3] * fx) * fy;
}

/* 진하기 그림 하나를 (x, y)에 dw x dh 크기로 그린다. 많이 줄일 때는 네 군데를 떠서 평균한다 */
static void put_glyph(image_t *im, const uint8_t *src, int sw, int sh, float x, float y, float dw, float dh, rgb_t c, float a)
{
    int ax, ay, bx, by;
    if (!clip_box(im, x, y, x + dw, y + dh, &ax, &ay, &bx, &by)) return;
    float kx = (float)sw / dw, ky = (float)sh / dh;
    int small = kx > 1.3f || ky > 1.3f;
    for (int py = ay; py < by; py++) {
        float sy = ((float)py + 0.5f - y) * ky - 0.5f;
        for (int px = ax; px < bx; px++) {
            float sx = ((float)px + 0.5f - x) * kx - 0.5f, v;
            if (small)
                v = (tap(src, sw, sh, sx - kx * 0.25f, sy - ky * 0.25f) + tap(src, sw, sh, sx + kx * 0.25f, sy - ky * 0.25f) +
                     tap(src, sw, sh, sx - kx * 0.25f, sy + ky * 0.25f) + tap(src, sw, sh, sx + kx * 0.25f, sy + ky * 0.25f)) * 0.25f;
            else
                v = tap(src, sw, sh, sx, sy);
            if (v > 0.5f) hd_blend(im, px, py, c, a * v * (1.0f / 255.0f));
        }
    }
}

/* 큰 글꼴에 없는 글자: 12x12 글꼴로 작은 캔버스에 그린 것을 키워서 놓는다 */
static void put_small_glyph(image_t *im, int cp, float x, float y, float dw, float dh, rgb_t c, float a)
{
    static canvas_t *tiny;
    char buf[8];
    uint8_t alpha[UI_KFONT_W * UI_KFONT_H];
    int n = 0, w = cp < 128 ? UI_FONT_W : UI_KFONT_W, h = cp < 128 ? UI_FONT_H : UI_KFONT_H;
    if (!tiny) tiny = canvas_new(UI_KFONT_W, UI_KFONT_H);
    if (!tiny) return;
    if (cp < 0x80) buf[n++] = (char)cp;
    else if (cp < 0x800) { buf[n++] = (char)(0xC0 | (cp >> 6)); buf[n++] = (char)(0x80 | (cp & 0x3F)); }
    else if (cp < 0x10000) { buf[n++] = (char)(0xE0 | (cp >> 12)); buf[n++] = (char)(0x80 | ((cp >> 6) & 0x3F)); buf[n++] = (char)(0x80 | (cp & 0x3F)); }
    else { buf[n++] = (char)(0xF0 | (cp >> 18)); buf[n++] = (char)(0x80 | ((cp >> 12) & 0x3F)); buf[n++] = (char)(0x80 | ((cp >> 6) & 0x3F)); buf[n++] = (char)(0x80 | (cp & 0x3F)); }
    buf[n] = '\0';
    ui_clear(tiny, C_NONE);
    ui_text(tiny, 0, 0, buf, C_WHITE);
    for (int i = 0; i < w * h; i++) alpha[i] = tiny->px[(i / w) * tiny->w + i % w] == C_WHITE ? 255 : 0;
    put_glyph(im, alpha, w, h, x, y, dw, dh, c, a);
}

void hd_text(image_t *im, float x, float y, const char *s, rgb_t c, float cell, float a)
{
    if (!im || !s || cell < 1.0f || a <= 0.0f) return;
    pthread_once(&hf_once, hf_load);
    int wide = 0, cp;
    for (const char *p = s; (cp = utf8_next(&p)) != 0; ) if (cp >= 128) { wide = 1; break; }
    /* ui_text와 같은 규칙: 영문은 6x8칸(한글 칸의 절반 폭, 3분의 2 높이)이고, 한글이 섞인 줄에서는 조금 내려 그린다 */
    float lw = cell * 0.5f, lh = cell * (float)UI_FONT_H / (float)UI_KFONT_H;
    float ly = y + (wide ? cell * (float)UI_KFONT_LATIN_DY / (float)UI_KFONT_H : 0.0f);
    while ((cp = utf8_next(&s)) != 0) {
        if (cp < 128) {
            if (cp >= 'a' && cp <= 'z') cp -= 'a' - 'A';
            const uint8_t *g = hf.ok ? hf_glyph((uint32_t)cp) : NULL;
            if (g) put_glyph(im, g, hf.nw, hf.nh, x, ly, lw, lh, c, a);
            else if (cp != ' ') put_small_glyph(im, cp, x, ly, lw, lh, c, a);
            x += lw;
        } else {
            const uint8_t *g = hf.ok ? hf_glyph((uint32_t)cp) : NULL;
            if (g) put_glyph(im, g, hf.ww, hf.wh, x, y, cell, cell, c, a);
            else put_small_glyph(im, cp, x, y, cell, cell, c, a);
            x += cell;
        }
    }
}

float hd_text_w(const char *s, float cell)
{
    return (float)ui_text_w(s) * cell / (float)UI_KFONT_W;
}

/* ---- 캔버스 키우기 ------------------------------------------------------------------ */
/* 픽셀 그림을 두 배로: 한 픽셀을 넷으로 나누되, 이웃한 두 픽셀(위와 왼쪽 등)이 같은 색이면 그 사이의 모서리 조각을
 * 그 색으로 칠한다. 대각선의 계단이 깎여 나간다(Scale2x라고 부르는 규칙). 색 번호끼리 같은지만 보므로 새 색이
 * 생기지 않는다. */
static void double_up(const uint8_t *s, int w, int h, uint8_t *d)
{
    for (int y = 0; y < h; y++) {
        const uint8_t *row = s + y * w, *up = y > 0 ? row - w : row, *dn = y + 1 < h ? row + w : row;
        uint8_t *o0 = d + (2 * y) * (2 * w), *o1 = o0 + 2 * w;
        for (int x = 0; x < w; x++) {
            uint8_t p = row[x], a = up[x], dd = dn[x], c = x > 0 ? row[x - 1] : p, b = x + 1 < w ? row[x + 1] : p;
            o0[2 * x]     = (c == a && c != dd && a != b) ? a : p;
            o0[2 * x + 1] = (a == b && a != c && b != dd) ? b : p;
            o1[2 * x]     = (dd == c && dd != b && c != a) ? c : p;
            o1[2 * x + 1] = (b == dd && b != a && dd != c) ? dd : p;
        }
    }
}

void hd_canvas(image_t *im, int dx, int dy, const canvas_t *src, int n)
{
    static uint8_t *buf[2];
    static size_t cap[2];
    static uint8_t pal[256][3];
    static int have_pal;
    if (!im || !src || src->w < 1 || src->h < 1 || n < 1) return;
    if (!have_pal) {
        for (int c = 0; c < 256; c++) ui_palette_rgb((color_t)c, pal[c]);
        have_pal = 1;
    }

    /* 두 배 하기를 거듭해 원하는 크기 이상(f배)으로 만든다 */
    const uint8_t *cur = src->px;
    int f = 1, w = src->w, h = src->h;
    for (int k = 0; f < n; k++) {
        int slot = k & 1;
        size_t need = (size_t)w * 2 * (size_t)h * 2;
        if (cap[slot] < need) {
            uint8_t *nb = realloc(buf[slot], need);
            if (!nb) return;
            buf[slot] = nb;
            cap[slot] = need;
        }
        double_up(cur, w, h, buf[slot]);
        cur = buf[slot];
        f *= 2; w *= 2; h *= 2;
    }

    int ow = src->w * n, oh = src->h * n;            /* 놓일 크기 */
    int x0 = dx < 0 ? -dx : 0, y0 = dy < 0 ? -dy : 0;
    int x1 = dx + ow > im->w ? im->w - dx : ow, y1 = dy + oh > im->h ? im->h - dy : oh;
    if (x0 >= x1 || y0 >= y1) return;
    if (f == n) {                                    /* 딱 맞는다: 색만 옮긴다 */
        for (int y = y0; y < y1; y++) {
            uint8_t *o = im->px + ((size_t)(dy + y) * (size_t)im->w + (size_t)(dx + x0)) * 3;
            const uint8_t *row = cur + (size_t)y * (size_t)w;
            for (int x = x0; x < x1; x++, o += 3) memcpy(o, pal[row[x]], 3);
        }
        return;
    }
    /* f배 그림을 n배로 줄인다. 받는 픽셀 하나가 덮는 넓이만큼 색을 섞으므로 가장자리가 부드러워진다.
     * 받는 칸마다 걸치는 원본 칸(셋까지)과 걸친 길이를 먼저 구해 둔다 */
    enum { TAPS = 3 };
    static int *ix, *iy;
    static float *wx, *wy;
    static int cap_w, cap_h;
    if (cap_w < ow) {
        ix = realloc(ix, sizeof *ix * (size_t)ow * TAPS);
        wx = realloc(wx, sizeof *wx * (size_t)ow * TAPS);
        cap_w = ix && wx ? ow : 0;
    }
    if (cap_h < oh) {
        iy = realloc(iy, sizeof *iy * (size_t)oh * TAPS);
        wy = realloc(wy, sizeof *wy * (size_t)oh * TAPS);
        cap_h = iy && wy ? oh : 0;
    }
    if (!cap_w || !cap_h) return;
    float step = (float)f / (float)n;
    for (int axis = 0; axis < 2; axis++) {
        int cnt = axis ? oh : ow, lim = axis ? h : w, *ii = axis ? iy : ix;
        float *ww = axis ? wy : wx;
        for (int i = 0; i < cnt; i++) {
            float a = (float)i * step, b = a + step;
            for (int k = 0; k < TAPS; k++) {
                int t = (int)a + k;
                float lo = (float)t > a ? (float)t : a, hi = (float)(t + 1) < b ? (float)(t + 1) : b;
                ii[i * TAPS + k] = t < lim ? t : lim - 1;
                ww[i * TAPS + k] = hi > lo ? (hi - lo) / step : 0.0f;
            }
        }
    }
    for (int y = y0; y < y1; y++) {
        const int *sy = iy + y * TAPS;
        const float *fy = wy + y * TAPS;
        uint8_t *o = im->px + ((size_t)(dy + y) * (size_t)im->w + (size_t)(dx + x0)) * 3;
        for (int x = x0; x < x1; x++, o += 3) {
            const int *sx = ix + x * TAPS;
            const float *fx = wx + x * TAPS;
            uint8_t first = cur[sy[0] * w + sx[0]];
            int same = 1;
            for (int j = 0; j < TAPS && same; j++)
                for (int i = 0; i < TAPS; i++)
                    if (fy[j] * fx[i] > 0.0f && cur[sy[j] * w + sx[i]] != first) { same = 0; break; }
            if (same) { memcpy(o, pal[first], 3); continue; }
            float r = 0.0f, g = 0.0f, b = 0.0f;
            for (int j = 0; j < TAPS; j++) {
                if (fy[j] <= 0.0f) continue;
                for (int i = 0; i < TAPS; i++) {
                    float wgt = fy[j] * fx[i];
                    if (wgt <= 0.0f) continue;
                    const uint8_t *q = pal[cur[sy[j] * w + sx[i]]];
                    r += (float)q[0] * wgt; g += (float)q[1] * wgt; b += (float)q[2] * wgt;
                }
            }
            o[0] = (uint8_t)(r + 0.5f); o[1] = (uint8_t)(g + 0.5f); o[2] = (uint8_t)(b + 0.5f);
        }
    }
}

void hd_upscale(image_t *im, const canvas_t *src)
{
    if (!im || !src || src->w < 1) return;
    int n = im->w / src->w;
    if (n < 1 || im->w != src->w * n || im->h != src->h * n) return;
    hd_canvas(im, 0, 0, src, n);
}
