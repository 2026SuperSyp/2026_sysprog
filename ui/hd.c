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

/* ---- 윤곽선 글꼴 --------------------------------------------------------------------- */
/* 파일 형식은 tools/font_hd.py에 있다. 글자의 윤곽선(직선과 2차 곡선으로 된 닫힌 선들)이 들어 있다.
 * 그릴 때 원하는 크기로 옮겨 안쪽을 채우므로 어떤 크기에서도 가장자리가 또렷하다.
 *
 * 채우는 방법: 윤곽선을 짧은 직선들로 바꾸고, 직선마다 "이 줄에서 이 직선의 오른쪽이 얼마나 덮이는가"를 픽셀에
 * 더해 둔다(올라가는 선은 더하고 내려가는 선은 뺀다). 줄마다 왼쪽에서부터 그 값을 쌓아 가면 픽셀이 덮인 넓이가
 * 나온다. 가장자리에 걸친 픽셀은 걸친 넓이만큼만 칠해진다.
 *
 * 한 번 채운 글자는 크기와 함께 적어 둔다(같은 글자를 프레임마다 다시 채우지 않는다).
 * 파일이 없거나 틀리면 경고를 한 번 남기고, 모든 글자를 12x12 글꼴을 키워서 그린다. */
#define HDFONT_PATH "ui/assets/font_outline.fnt"
#define HDFONT_MAX  8192u
#define GLYPH_SLOTS 1024         /* 적어 두는 글자 수. 넘치면 같은 자리의 것을 버린다 */
#define GLYPH_MAX   400          /* 글자 하나의 그림의 한 변의 한도(픽셀) */

typedef struct {
    uint32_t cp;
    float cell;
    int w, h, ox, oy;            /* 그림의 크기와, 칸의 왼쪽 위에서 그림까지의 거리 */
    uint8_t *a;                  /* 진하기 w * h */
} slot_t;

static struct {
    int ok, n;
    float wide_k, wide_base, narrow_k, narrow_base;
    uint32_t *cp;                /* 글자 코드. 작은 것부터 */
    const uint8_t **glyph;       /* 글자마다 윤곽선이 시작하는 자리 */
    uint8_t *file;
    slot_t slot[GLYPH_SLOTS];
} hf;
static pthread_once_t hf_once = PTHREAD_ONCE_INIT;

static uint32_t rd32(const uint8_t *p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}
static int rd16(const uint8_t *p) { return (int16_t)((uint16_t)p[0] | ((uint16_t)p[1] << 8)); }
static float rdf(const uint8_t *p) { uint32_t v = rd32(p); float f; memcpy(&f, &v, 4); return f; }

/* 윤곽선 하나가 파일 안에 온전히 들어 있는지 본다 */
static int glyph_ok(const uint8_t *g, const uint8_t *end)
{
    if (end - g < 12) return 0;
    int n = (uint16_t)rd16(g + 10);
    const uint8_t *p = g + 12;
    for (int i = 0; i < n; i++) {
        if (end - p < 2) return 0;
        int pts = (uint16_t)rd16(p);
        p += 2;
        if (pts < 2 || (end - p) / 4 < pts) return 0;
        p += (size_t)pts * 4;
    }
    return 1;
}

static int hf_read(uint8_t *file, size_t size)
{
    if (size < 32 || memcmp(file, "SSPV", 4) != 0 || file[4] != 1) return 0;
    uint32_t n = rd32(file + 8);
    if (n < 1 || n > HDFONT_MAX || size < 32 + (size_t)n * 8) return 0;
    const uint8_t *table = file + 32, *data = table + (size_t)n * 8, *end = file + size;
    uint32_t *cp = malloc(sizeof *cp * n);
    const uint8_t **glyph = malloc(sizeof *glyph * n);
    int good = cp && glyph;
    for (uint32_t i = 0; good && i < n; i++) {
        uint32_t code = rd32(table + i * 8), at = rd32(table + i * 8 + 4);
        if ((i > 0 && code <= cp[i - 1]) || at >= (size_t)(end - data) || !glyph_ok(data + at, end)) { good = 0; break; }
        cp[i] = code;
        glyph[i] = data + at;
    }
    hf.wide_k = rdf(file + 12); hf.wide_base = rdf(file + 16); hf.narrow_k = rdf(file + 20); hf.narrow_base = rdf(file + 24);
    if (!(hf.wide_k > 0.0f && hf.wide_k < 1.0f && hf.narrow_k > 0.0f && hf.narrow_k < 1.0f)) good = 0;
    if (!good) { free(cp); free(glyph); return 0; }
    hf.n = (int)n;
    hf.cp = cp;
    hf.glyph = glyph;
    return 1;
}

static void hf_load(void)
{
    FILE *f = fopen(HDFONT_PATH, "rb");
    if (!f) { log_warn_once("hdfont", "윤곽선 글꼴 파일이 없습니다: %s (12픽셀 글꼴을 키워서 그립니다)", HDFONT_PATH); return; }
    long size = 0;
    if (fseek(f, 0, SEEK_END) == 0 && (size = ftell(f)) > 0 && size < (16L << 20) && fseek(f, 0, SEEK_SET) == 0)
        hf.file = malloc((size_t)size);
    if (hf.file && fread(hf.file, 1, (size_t)size, f) == (size_t)size) hf.ok = hf_read(hf.file, (size_t)size);
    fclose(f);
    if (!hf.ok) {
        free(hf.file);
        hf.file = NULL;
        log_warn_once("hdfont", "윤곽선 글꼴 파일의 형식이 틀렸습니다: %s (12픽셀 글꼴을 키워서 그립니다)", HDFONT_PATH);
    }
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

/* 직선 하나를 넓이 표(acc, w x h)에 더한다. 좌표는 픽셀이고 x는 0 이상 w - 1 이하로 들어온다 */
static void fill_line(float *acc, int w, int h, float x0, float y0, float x1, float y1)
{
    if (y0 == y1) return;
    float dir = 1.0f;
    if (y0 > y1) { float t = x0; x0 = x1; x1 = t; t = y0; y0 = y1; y1 = t; dir = -1.0f; }
    float dxdy = (x1 - x0) / (y1 - y0), x = x0;
    if (y0 < 0.0f) x -= y0 * dxdy;
    int ya = y0 < 0.0f ? 0 : (int)y0, yb = (int)ceilf(y1);
    if (yb > h) yb = h;
    for (int y = ya; y < yb; y++) {
        float *row = acc + (size_t)y * (size_t)w;
        float dy = ((float)(y + 1) < y1 ? (float)(y + 1) : y1) - ((float)y > y0 ? (float)y : y0);
        float xn = x + dxdy * dy, d = dy * dir;
        float xa = x < xn ? x : xn, xb = x < xn ? xn : x;
        float fa = floorf(xa);
        int ia = (int)fa, ib = (int)ceilf(xb);
        if (ia < 0) ia = 0;
        if (ia > w - 2) ia = w - 2;
        if (ib > w - 1) ib = w - 1;
        if (ib <= ia + 1) {                          /* 한 픽셀 안에서 지나간다 */
            float mid = 0.5f * (x + xn) - fa;
            row[ia] += d - d * mid;
            row[ia + 1] += d * mid;
        } else {                                     /* 여러 픽셀에 걸친다: 픽셀마다 사다리꼴의 넓이를 나눠 준다 */
            float s = 1.0f / (xb - xa), fx0 = xa - fa, fx1 = xb - (float)ib + 1.0f;
            float a0 = 0.5f * s * (1.0f - fx0) * (1.0f - fx0), am = 0.5f * s * fx1 * fx1;
            row[ia] += d * a0;
            if (ib == ia + 2) {
                row[ia + 1] += d * (1.0f - a0 - am);
            } else {
                float a1 = s * (1.5f - fx0);
                row[ia + 1] += d * (a1 - a0);
                for (int xi = ia + 2; xi < ib - 1; xi++) row[xi] += d * s;
                row[ib - 1] += d * (1.0f - (a1 + (float)(ib - ia - 3) * s) - am);
            }
            row[ib] += d * am;
        }
        x = xn;
    }
}

/* 2차 곡선을 짧은 직선들로 나눠 더한다 */
static void fill_curve(float *acc, int w, int h, float x0, float y0, float cx, float cy, float x1, float y1)
{
    float len = fabsf(cx - x0) + fabsf(cy - y0) + fabsf(x1 - cx) + fabsf(y1 - cy);
    int n = 2 + (int)(len * 0.25f);
    if (n > 16) n = 16;
    float px = x0, py = y0;
    for (int i = 1; i <= n; i++) {
        float t = (float)i / (float)n, u = 1.0f - t;
        float qx = u * u * x0 + 2.0f * u * t * cx + t * t * x1, qy = u * u * y0 + 2.0f * u * t * cy + t * t * y1;
        fill_line(acc, w, h, px, py, qx, qy);
        px = qx;
        py = qy;
    }
}

/* 글자 하나를 cell 크기로 채워 적어 둔 것을 준다. 글꼴에 없는 글자면 NULL */
static const slot_t *glyph_bitmap(uint32_t cp, float cell)
{
    static float *acc;
    static size_t acc_cap;
    slot_t *sl = &hf.slot[(cp * 2654435761u + (uint32_t)(cell * 8.0f) * 40503u) % GLYPH_SLOTS];
    if (sl->a && sl->cp == cp && sl->cell == cell) return sl;
    const uint8_t *g = hf_glyph(cp);
    if (!g) return NULL;

    int narrow = cp < 128, adv = rd16(g), xmin = rd16(g + 2), xmax = rd16(g + 4), ymin = rd16(g + 6), ymax = rd16(g + 8);
    float k = (narrow ? hf.narrow_k : hf.wide_k) * cell, kx = k, box = narrow ? cell * 0.5f : cell;
    float base = (narrow ? hf.narrow_base : hf.wide_base) * cell, gx;
    if (narrow) {                                    /* 영문: 칸 가운데에. 칸보다 넓으면 가로로 줄인다 */
        float gw = (float)(xmax - xmin) * kx, room = box - cell * (1.5f / 48.0f);
        if (gw > room) kx *= room / gw;
        gx = (box - (float)(xmax - xmin) * kx) * 0.5f - (float)xmin * kx;
    } else if (cp >= 0x3131) {                       /* 한글: 나아가는 폭의 가운데를 칸의 가운데에 */
        gx = (box - (float)adv * kx) * 0.5f;
    } else {                                         /* 부호: 그림의 가운데를 칸의 가운데에 */
        gx = (box - (float)(xmax - xmin) * kx) * 0.5f - (float)xmin * kx;
        base = cell * 0.5f + (float)(ymax + ymin) * 0.5f * k;
    }
    int x0 = (int)floorf(gx + (float)xmin * kx) - 1, x1 = (int)ceilf(gx + (float)xmax * kx) + 2;
    int y0 = (int)floorf(base - (float)ymax * k) - 1, y1 = (int)ceilf(base - (float)ymin * k) + 1;
    int w = x1 - x0, h = y1 - y0;
    if (w < 2 || h < 1 || w > GLYPH_MAX || h > GLYPH_MAX) return NULL;
    size_t need = (size_t)w * (size_t)h;
    if (acc_cap < need) {
        float *na = realloc(acc, need * sizeof *acc);
        if (!na) return NULL;
        acc = na;
        acc_cap = need;
    }
    memset(acc, 0, need * sizeof *acc);

    /* 닫힌 선마다: 곡선 위의 점에서 시작해 한 바퀴 돈다. 조절점이 둘 이어지면 그 가운데가 곡선 위의 점이다 */
    int n_contours = (uint16_t)rd16(g + 10);
    const uint8_t *p = g + 12;
    for (int c = 0; c < n_contours; c++) {
        int n = (uint16_t)rd16(p);
        const uint8_t *pt = p + 2;
        p = pt + (size_t)n * 4;
        #define PX(i) (gx + (float)(rd16(pt + (size_t)(i) * 4) >> 1) * kx - (float)x0)
        #define PY(i) (base - (float)rd16(pt + (size_t)(i) * 4 + 2) * k - (float)y0)
        #define ON(i) (rd16(pt + (size_t)(i) * 4) & 1)
        int first = 0, all_off = 0;
        float sx, sy;
        while (first < n && !ON(first)) first++;
        if (first < n) { sx = PX(first); sy = PY(first); }
        else { all_off = 1; first = n - 1; sx = (PX(0) + PX(n - 1)) * 0.5f; sy = (PY(0) + PY(n - 1)) * 0.5f; }
        float cx = sx, cy = sy, qx = 0.0f, qy = 0.0f;
        int have_q = 0;
        for (int step = 1; step < n + all_off; step++) {     /* 시작점 다음부터 시작점 앞까지(시작점이 조절점들의 가운데면 전부) */
            int i = (first + step) % n;
            float x = PX(i), y = PY(i);
            if (ON(i)) {
                if (have_q) fill_curve(acc, w, h, cx, cy, qx, qy, x, y);
                else fill_line(acc, w, h, cx, cy, x, y);
                cx = x; cy = y;
                have_q = 0;
            } else {
                if (have_q) {
                    float mx = (qx + x) * 0.5f, my = (qy + y) * 0.5f;
                    fill_curve(acc, w, h, cx, cy, qx, qy, mx, my);
                    cx = mx; cy = my;
                }
                qx = x; qy = y;
                have_q = 1;
            }
        }
        if (have_q) fill_curve(acc, w, h, cx, cy, qx, qy, sx, sy);
        else fill_line(acc, w, h, cx, cy, sx, sy);
        #undef PX
        #undef PY
        #undef ON
    }

    uint8_t *a = realloc(sl->a, need);
    if (!a) return NULL;
    for (int y = 0; y < h; y++) {
        float sum = 0.0f;
        for (int x = 0; x < w; x++) {
            sum += acc[(size_t)y * (size_t)w + (size_t)x];
            float v = fabsf(sum);
            a[(size_t)y * (size_t)w + (size_t)x] = (uint8_t)(v >= 1.0f ? 255.0f : v * 255.0f + 0.5f);
        }
    }
    *sl = (slot_t){ cp, cell, w, h, x0, y0, a };
    return sl;
}

/* 윤곽선 글꼴에 없는 글자: 12x12 글꼴의 픽셀을 그대로 키워서 놓는다 */
static void put_small_glyph(image_t *im, int cp, float x, float y, float dw, float dh, rgb_t c, float a)
{
    static canvas_t *tiny;
    char buf[8];
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
    for (int gy = 0; gy < h; gy++)
        for (int gx = 0; gx < w; gx++)
            if (tiny->px[gy * tiny->w + gx] == C_WHITE)
                hd_rrect(im, x + (float)gx * dw / (float)w, y + (float)gy * dh / (float)h, dw / (float)w, dh / (float)h, 0.0f, c, a);
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
        int narrow = cp < 128;
        if (narrow && cp >= 'a' && cp <= 'z') cp -= 'a' - 'A';
        float gy = narrow ? ly : y, adv = narrow ? lw : cell;
        const slot_t *g = hf.ok && cp != ' ' ? glyph_bitmap((uint32_t)cp, cell) : NULL;
        if (g) {
            int bx = (int)floorf(x + 0.5f) + g->ox, by = (int)floorf(gy + 0.5f) + g->oy;
            for (int py = 0; py < g->h; py++) {
                const uint8_t *row = g->a + (size_t)py * (size_t)g->w;
                for (int px = 0; px < g->w; px++)
                    if (row[px]) hd_blend(im, bx + px, by + py, c, a * (float)row[px] * (1.0f / 255.0f));
            }
        } else if (cp != ' ') {
            put_small_glyph(im, cp, x, gy, adv, narrow ? lh : cell, c, a);
        }
        x += adv;
    }
}

float hd_text_w(const char *s, float cell)
{
    return (float)ui_text_w(s) * cell / (float)UI_KFONT_W;
}

/* ---- 캔버스 키우기 ------------------------------------------------------------------ */
/* 픽셀 하나를 n x n으로 그대로 키운다. 섞거나 깎지 않으므로 네모는 네모대로 또렷하다 */
void hd_canvas(image_t *im, int dx, int dy, const canvas_t *src, int n)
{
    static uint8_t pal[256][3];
    static int have_pal;
    if (!im || !src || src->w < 1 || src->h < 1 || n < 1) return;
    if (!have_pal) {
        for (int c = 0; c < 256; c++) ui_palette_rgb((color_t)c, pal[c]);
        have_pal = 1;
    }
    int x0 = dx < 0 ? -dx : 0, x1 = dx + src->w * n > im->w ? im->w - dx : src->w * n;
    if (x0 >= x1) return;
    for (int sy = 0; sy < src->h; sy++) {
        int y = dy + sy * n;
        if (y + n <= 0 || y >= im->h) continue;
        int ya = y < 0 ? 0 : y, yb = y + n > im->h ? im->h : y + n;
        uint8_t *first = im->px + ((size_t)ya * (size_t)im->w + (size_t)(dx + x0)) * 3, *o = first;
        const uint8_t *row = src->px + (size_t)sy * (size_t)src->w;
        for (int x = x0; x < x1; x++, o += 3) memcpy(o, pal[row[x / n]], 3);
        for (int yy = ya + 1; yy < yb; yy++)           /* 아래 줄들은 첫 줄을 베낀다 */
            memcpy(im->px + ((size_t)yy * (size_t)im->w + (size_t)(dx + x0)) * 3, first, (size_t)(x1 - x0) * 3);
    }
}

void hd_upscale(image_t *im, const canvas_t *src)
{
    if (!im || !src || src->w < 1) return;
    int n = im->w / src->w;
    if (n < 1 || im->w != src->w * n || im->h != src->h * n) return;
    hd_canvas(im, 0, 0, src, n);
}
