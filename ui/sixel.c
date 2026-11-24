/* 표시(Sixel): 픽셀 그림(image_t)을 터미널에 그림으로 보낸다.
 *
 * Sixel은 터미널에 점 그림을 보내는 오래된 규격이다(DEC VT340). 색 번호표(팔레트)를 정하고, 그림을 6줄씩 띠로 나눠
 * 색마다 "이 칸의 6줄 가운데 어느 줄이 이 색인가"를 글자 하나(63 + 6비트)로 적는다. 같은 글자가 이어지면 "!개수 글자"로
 * 줄인다. Windows Terminal 1.22부터 받는다. 받는지는 term_caps가 터미널에 물어서 안다.
 *
 * 하는 일
 *   1. 색 줄이기. 그림의 색은 제한이 없지만 Sixel의 색 번호는 240개만 쓴다. 프레임마다 색을 5비트씩(32768칸)으로 세고,
 *      많이 쓰인 범위를 거듭 반으로 갈라(중앙값 자르기) 240개의 색을 고른다. 8비트를 5비트로 줄일 때 생기는 띠는
 *      자리마다 정해진 값을 더해 흩는다(정렬 디더링). 자리에만 달려 있어서 멈춘 화면은 프레임마다 같은 결과가 나온다.
 *   2. 바뀐 줄만 보내기. 그림을 글자 한 줄 높이의 띠로 나눠 지난번과 달라진 띠만 다시 보낸다. 그림은 터미널이
 *      색으로 바꿔 간직하므로, 안 보낸 띠는 지난 프레임의 색 번호표로 그린 그대로 남는다.
 *   3. 한 프레임을 "한꺼번에 갱신" 표시(?2026)로 감싼다. 그리다 만 화면이 보이지 않는다.
 *
 * 그림의 픽셀 하나가 화면에서 얼마나 큰지는 터미널이 정한다. 터미널은 글자 한 칸을 가로 cell_w, 세로 cell_h 픽셀로
 * 치고(Windows Terminal은 글자 크기와 무관하게 10x20) 그림을 그 비율로 늘리거나 줄인다. 그래서 글자가 클수록
 * 그림이 크게 보인다. */
#include "ui/hd.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define SX_COLORS 240
#define SX_BINS   32768          /* 5비트 x 3 */

struct sixel_state {
    int w, h;                    /* 지난번 그림의 크기 */
    int cols, rows, cell_w, cell_h;
    int valid;                   /* 0이면 다음 번에 전부 다시 보낸다 */
    uint8_t *prev;               /* 지난번 그림(w * h * 3) */
    uint16_t *bin;               /* 픽셀마다의 5비트 색 칸 */
    uint8_t *idx;                /* 픽셀마다의 색 번호 */
    uint8_t *mask;               /* 띠 하나를 적을 때 쓰는 자리: 색마다 w칸 */
    char *out;
    size_t len, cap;
};

sixel_state_t *sixel_new(void) { return calloc(1, sizeof(sixel_state_t)); }

void sixel_free(sixel_state_t *st)
{
    if (!st) return;
    free(st->prev); free(st->bin); free(st->idx); free(st->mask); free(st->out);
    free(st);
}

void sixel_invalidate(sixel_state_t *st) { if (st) st->valid = 0; }

void sixel_cells(const image_t *im, int cell_w, int cell_h, int *cols, int *rows)
{
    *cols = (im->w + cell_w - 1) / cell_w;
    *rows = (im->h + cell_h - 1) / cell_h;
}

int sixel_cell_to_pixel(const image_t *im, int cols, int rows, int cell_w, int cell_h, int cell_x, int cell_y, int *px, int *py)
{
    int ic, ir;
    sixel_cells(im, cell_w, cell_h, &ic, &ir);
    int x = (cell_x - (cols - ic) / 2) * cell_w + cell_w / 2, y = (cell_y - (rows - ir) / 2) * cell_h + cell_h / 2;
    if (x < 0 || y < 0 || x >= im->w || y >= im->h) return 0;
    *px = x;
    *py = y;
    return 1;
}

/* ---- 내보낼 글자열 ------------------------------------------------------------------ */
static int put(sixel_state_t *st, const char *s, size_t n)
{
    if (st->len + n > st->cap) {
        size_t cap = (st->len + n) * 2 + 65536;
        char *nb = realloc(st->out, cap);
        if (!nb) return 0;
        st->out = nb;
        st->cap = cap;
    }
    memcpy(st->out + st->len, s, n);
    st->len += n;
    return 1;
}

static void put_num(sixel_state_t *st, const char *before, int v, const char *after)
{
    char tmp[48];
    put(st, tmp, (size_t)snprintf(tmp, sizeof tmp, "%s%d%s", before, v, after));
}

/* 같은 글자 n개. 넷 이상이면 "!n글자"로 줄인다 */
static void put_run(sixel_state_t *st, int ch, int n)
{
    char tmp[16];
    if (n <= 0) return;
    if (n > 3) { put(st, tmp, (size_t)snprintf(tmp, sizeof tmp, "!%d%c", n, ch)); return; }
    for (int i = 0; i < n; i++) tmp[i] = (char)ch;
    put(st, tmp, (size_t)n);
}

/* ---- 색 줄이기 --------------------------------------------------------------------- */
/* 8x8 정렬 디더링 값(0~63). 5비트로 자르기 전에 0~7을 더하는 데 쓴다 */
static const uint8_t bayer[8][8] = {
    {  0, 32,  8, 40,  2, 34, 10, 42 }, { 48, 16, 56, 24, 50, 18, 58, 26 },
    { 12, 44,  4, 36, 14, 46,  6, 38 }, { 60, 28, 52, 20, 62, 30, 54, 22 },
    {  3, 35, 11, 43,  1, 33,  9, 41 }, { 51, 19, 59, 27, 49, 17, 57, 25 },
    { 15, 47,  7, 39, 13, 45,  5, 37 }, { 63, 31, 55, 23, 61, 29, 53, 21 } };

typedef struct { int first, count; uint32_t pop; uint8_t lo[3], hi[3]; } box_t;

static uint32_t hist[SX_BINS], sum[SX_BINS][3];
static uint16_t used[SX_BINS];       /* 쓰인 칸들. 상자마다 이어진 구간을 갖는다 */
static uint8_t  box_of[SX_BINS];     /* 칸 -> 색 번호 */

static int chan(int bin, int k) { return (bin >> (10 - 5 * k)) & 31; }

static void box_fit(box_t *b)
{
    b->pop = 0;
    for (int k = 0; k < 3; k++) { b->lo[k] = 31; b->hi[k] = 0; }
    for (int i = 0; i < b->count; i++) {
        int bin = used[b->first + i];
        b->pop += hist[bin];
        for (int k = 0; k < 3; k++) {
            int v = chan(bin, k);
            if (v < b->lo[k]) b->lo[k] = (uint8_t)v;
            if (v > b->hi[k]) b->hi[k] = (uint8_t)v;
        }
    }
}

/* 그림의 색을 SX_COLORS개 이하로 고른다. pal에 색(0~255)을 채우고 개수를 돌려준다. st->idx가 채워진다 */
static int quantize(sixel_state_t *st, const image_t *im, uint8_t pal[SX_COLORS][3])
{
    size_t n_px = (size_t)im->w * (size_t)im->h;
    int n_used = 0;
    memset(hist, 0, sizeof hist);
    const uint8_t *p = im->px;
    for (int y = 0; y < im->h; y++) {
        const uint8_t *d = bayer[y & 7];
        uint16_t *row = st->bin + (size_t)y * (size_t)im->w;
        for (int x = 0; x < im->w; x++, p += 3) {
            int t = d[x & 7] >> 3;                                   /* 0~7 */
            int r = p[0] + t, g = p[1] + t, b = p[2] + t;
            if (r > 255) r = 255;
            if (g > 255) g = 255;
            if (b > 255) b = 255;
            int bin = (r >> 3) << 10 | (g >> 3) << 5 | (b >> 3);
            if (hist[bin]++ == 0) {
                used[n_used++] = (uint16_t)bin;
                sum[bin][0] = sum[bin][1] = sum[bin][2] = 0;
            }
            sum[bin][0] += p[0]; sum[bin][1] += p[1]; sum[bin][2] += p[2];
            row[x] = (uint16_t)bin;
        }
    }

    /* 중앙값 자르기: 가장 넓게 퍼져 있고 픽셀이 많은 상자를 골라, 가장 긴 축에서 픽셀 수가 반씩 되게 가른다 */
    static box_t box[SX_COLORS];
    int n_box = 1;
    box[0] = (box_t){ .first = 0, .count = n_used };
    box_fit(&box[0]);
    while (n_box < SX_COLORS) {
        int best = -1, axis = 0;
        double best_score = 0.0;
        for (int i = 0; i < n_box; i++) {
            int k = 0, range = 0;
            for (int j = 0; j < 3; j++)
                if (box[i].hi[j] - box[i].lo[j] > range) { range = box[i].hi[j] - box[i].lo[j]; k = j; }
            if (range == 0 || box[i].count < 2) continue;
            /* 픽셀이 많은 상자를 먼저 가르되, 넓게 퍼진 상자(부드러운 명암)도 갈라지게 범위를 곱한다 */
            double score = (double)range * (double)range * (double)box[i].pop;
            if (score > best_score) { best_score = score; best = i; axis = k; }
        }
        if (best < 0) break;
        box_t *b = &box[best];
        uint32_t line[32] = { 0 }, half = b->pop / 2, acc = 0;
        for (int i = 0; i < b->count; i++) line[chan(used[b->first + i], axis)] += hist[used[b->first + i]];
        int cut = b->lo[axis];
        for (int v = b->lo[axis]; v < b->hi[axis]; v++) {              /* cut 이하와 초과로 가른다. 양쪽 다 비지 않는다 */
            acc += line[v];
            cut = v;
            if (acc >= half) break;
        }
        int i = b->first, j = b->first + b->count - 1;
        while (i <= j) {
            if (chan(used[i], axis) <= cut) i++;
            else { uint16_t t = used[i]; used[i] = used[j]; used[j] = t; j--; }
        }
        box[n_box] = (box_t){ .first = i, .count = b->first + b->count - i };
        b->count = i - b->first;
        box_fit(b);
        box_fit(&box[n_box]);
        n_box++;
    }
    for (int i = 0; i < n_box; i++) {
        uint64_t s[3] = { 0, 0, 0 }, pop = 0;
        for (int k = 0; k < box[i].count; k++) {
            int bin = used[box[i].first + k];
            box_of[bin] = (uint8_t)i;
            pop += hist[bin];
            for (int c = 0; c < 3; c++) s[c] += sum[bin][c];
        }
        for (int c = 0; c < 3; c++) pal[i][c] = (uint8_t)(pop ? (s[c] + pop / 2) / pop : 0);
    }
    for (size_t i = 0; i < n_px; i++) st->idx[i] = box_of[st->bin[i]];
    return n_box;
}

/* ---- 그림 한 조각 ------------------------------------------------------------------- */
/* y0줄부터 y1줄 앞까지를 지금 커서 자리에 그리는 Sixel 그림 하나 */
static void put_piece(sixel_state_t *st, const image_t *im, int y0, int y1, uint8_t pal[SX_COLORS][3])
{
    static int seen[SX_COLORS], list[SX_COLORS];
    int w = im->w, defined[SX_COLORS] = { 0 };
    put(st, "\033P0;1;0q", 8);                       /* 가운데 1: 적지 않은 점은 그대로 둔다 */
    put_num(st, "\"1;1;", w, ";");
    put_num(st, "", y1 - y0, "");
    for (int by = y0; by < y1; by += 6) {
        int n_list = 0;
        for (int k = 0; k < 6 && by + k < y1; k++) {
            const uint8_t *row = st->idx + (size_t)(by + k) * (size_t)w;
            for (int x = 0; x < w; x++) {
                int c = row[x];
                if (!seen[c]) { seen[c] = 1; list[n_list++] = c; }
                st->mask[(size_t)c * (size_t)w + (size_t)x] |= (uint8_t)(1 << k);
            }
        }
        for (int i = 0; i < n_list; i++) {
            int c = list[i], last = w - 1, prev = -1, run = 0;
            uint8_t *m = st->mask + (size_t)c * (size_t)w;
            if (!defined[c]) {                       /* 이 그림에서 처음 쓰는 색: 번호에 색을 정한다(0~100) */
                char tmp[48];
                put(st, tmp, (size_t)snprintf(tmp, sizeof tmp, "#%d;2;%d;%d;%d", c, (pal[c][0] * 100 + 127) / 255,
                                              (pal[c][1] * 100 + 127) / 255, (pal[c][2] * 100 + 127) / 255));
                defined[c] = 1;
            }
            put_num(st, "#", c, "");                 /* 이 색으로 적는다 */
            while (last >= 0 && !m[last]) last--;     /* 뒤쪽의 빈칸은 적지 않는다 */
            for (int x = 0; x <= last; x++) {
                int ch = 63 + m[x];
                if (ch == prev) { run++; continue; }
                put_run(st, prev, run);
                prev = ch;
                run = 1;
            }
            put_run(st, prev, run);
            put(st, i + 1 < n_list ? "$" : "-", 1);  /* $: 이 띠의 처음으로, -: 다음 띠로 */
            memset(m, 0, (size_t)w);
            seen[c] = 0;
        }
    }
    put(st, "\033\\", 2);
}

const char *sixel_encode(sixel_state_t *st, const image_t *im, int cols, int rows, int cell_w, int cell_h, size_t *len)
{
    *len = 0;
    if (!st || !im || cell_w < 1 || cell_h < 1) return NULL;
    int ic, ir;
    sixel_cells(im, cell_w, cell_h, &ic, &ir);
    if (cols < ic || rows < ir) { st->valid = 0; return NULL; }

    size_t n_px = (size_t)im->w * (size_t)im->h;
    if (im->w != st->w || im->h != st->h) {
        uint8_t *prev = realloc(st->prev, n_px * 3), *idx = realloc(st->idx, n_px);
        uint8_t *mask = realloc(st->mask, (size_t)SX_COLORS * (size_t)im->w);
        uint16_t *bin = realloc(st->bin, n_px * sizeof *bin);
        if (prev) st->prev = prev;
        if (idx) st->idx = idx;
        if (mask) st->mask = mask;
        if (bin) st->bin = bin;
        if (!prev || !idx || !mask || !bin) { st->w = st->h = 0; return NULL; }
        memset(st->mask, 0, (size_t)SX_COLORS * (size_t)im->w);
        st->w = im->w;
        st->h = im->h;
        st->valid = 0;
    }
    if (cols != st->cols || rows != st->rows || cell_w != st->cell_w || cell_h != st->cell_h) {
        st->cols = cols; st->rows = rows; st->cell_w = cell_w; st->cell_h = cell_h;
        st->valid = 0;
    }

    /* 글자 한 줄 높이의 띠마다 지난번과 같은지 본다 */
    static uint8_t changed[1024];
    int n_strips = ir < (int)sizeof changed ? ir : (int)sizeof changed, any = 0;
    size_t stride = (size_t)im->w * 3;
    for (int s = 0; s < n_strips; s++) {
        int y0 = s * cell_h, y1 = y0 + cell_h < im->h ? y0 + cell_h : im->h;
        changed[s] = !st->valid || memcmp(im->px + (size_t)y0 * stride, st->prev + (size_t)y0 * stride, (size_t)(y1 - y0) * stride) != 0;
        any |= changed[s];
    }
    st->len = 0;
    if (!any) return st->out ? st->out : "";

    uint8_t pal[SX_COLORS][3];
    quantize(st, im, pal);
    put(st, "\033[?2026h", 8);
    if (!st->valid) put(st, "\033[0m\033[2J", 8);
    int offx = (cols - ic) / 2, offy = (rows - ir) / 2;
    for (int s = 0; s < n_strips; ) {
        if (!changed[s]) { s++; continue; }
        int e = s;
        while (e < n_strips && changed[e]) e++;
        char tmp[32];
        put(st, tmp, (size_t)snprintf(tmp, sizeof tmp, "\033[%d;%dH", offy + s + 1, offx + 1));
        put_piece(st, im, s * cell_h, e * cell_h < im->h ? e * cell_h : im->h, pal);
        s = e;
    }
    put(st, "\033[?2026l", 8);
    memcpy(st->prev, im->px, n_px * 3);
    st->valid = 1;
    *len = st->len;
    return st->out;
}
