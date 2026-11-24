/* 캔버스와 기본 그리기. 캔버스는 색 번호 1바이트의 격자다. */
#include "ui/ui_internal.h"

#include <stdlib.h>
#include <string.h>

#define COORD_LIMIT 4000   /* 터무니없이 먼 좌표는 여기까지로 자른다 */

static int clampi(int v, int lo, int hi) { return v < lo ? lo : (v > hi ? hi : v); }

canvas_t *canvas_new(int w, int h)
{
    canvas_t *c = malloc(sizeof *c);
    if (!c) return NULL;
    c->w = w;
    c->h = h;
    c->px = malloc((size_t)w * (size_t)h);
    if (!c->px) { free(c); return NULL; }
    memset(c->px, C_BG, (size_t)w * (size_t)h);
    return c;
}

void canvas_free(canvas_t *c)
{
    if (!c) return;
    free(c->px);
    free(c);
}

int ui_width (const canvas_t *c) { return c ? c->w : 0; }
int ui_height(const canvas_t *c) { return c ? c->h : 0; }

void ui_clear(canvas_t *c, color_t bg)
{
    if (!c) return;
    memset(c->px, bg, (size_t)c->w * (size_t)c->h);
}

void ui_px(canvas_t *c, int x, int y, color_t col)
{
    if (!c || col == C_NONE) return;
    if (x < 0 || y < 0 || x >= c->w || y >= c->h) return;
    c->px[y * c->w + x] = col;
}

void ui_rect(canvas_t *c, int x, int y, int w, int h, color_t col)
{
    if (!c || col == C_NONE || w <= 0 || h <= 0) return;
    x = clampi(x, -COORD_LIMIT, COORD_LIMIT);
    y = clampi(y, -COORD_LIMIT, COORD_LIMIT);
    w = clampi(w, 0, 2 * COORD_LIMIT);
    h = clampi(h, 0, 2 * COORD_LIMIT);
    int x0 = clampi(x, 0, c->w), x1 = clampi(x + w, 0, c->w);
    int y0 = clampi(y, 0, c->h), y1 = clampi(y + h, 0, c->h);
    for (int yy = y0; yy < y1; yy++)
        memset(&c->px[yy * c->w + x0], col, (size_t)(x1 - x0));
}

/* 선 긋기: 브레젠험 알고리즘 */
void ui_line(canvas_t *c, int x0, int y0, int x1, int y1, color_t col)
{
    if (!c || col == C_NONE) return;
    x0 = clampi(x0, -COORD_LIMIT, COORD_LIMIT);
    y0 = clampi(y0, -COORD_LIMIT, COORD_LIMIT);
    x1 = clampi(x1, -COORD_LIMIT, COORD_LIMIT);
    y1 = clampi(y1, -COORD_LIMIT, COORD_LIMIT);
    int dx = abs(x1 - x0), sx = x0 < x1 ? 1 : -1;
    int dy = -abs(y1 - y0), sy = y0 < y1 ? 1 : -1;
    int err = dx + dy;
    for (;;) {
        ui_px(c, x0, y0, col);
        if (x0 == x1 && y0 == y1) break;
        int e2 = 2 * err;
        if (e2 >= dy) { err += dy; x0 += sx; }
        if (e2 <= dx) { err += dx; y0 += sy; }
    }
}

void ui_bar(canvas_t *c, int x, int y, int w, float ratio, color_t col)
{
    if (ratio < 0.0f) ratio = 0.0f;
    if (ratio > 1.0f) ratio = 1.0f;
    ui_rect(c, x, y, w, 4, C_GRAY1);
    ui_rect(c, x, y, (int)((float)w * ratio + 0.5f), 4, col);
}

void canvas_blit(canvas_t *dst, int dx, int dy, const canvas_t *src)
{
    if (!dst || !src) return;
    for (int y = 0; y < src->h; y++)
        for (int x = 0; x < src->w; x++)
            ui_px(dst, dx + x, dy + y, src->px[y * src->w + x]);
}

/* 2x2 픽셀을 1픽셀로 줄인다. 배경이 아닌 픽셀이 하나라도 있으면 그 색을 고른다.
 * 작은 캐릭터가 절반 크기 칸에서 사라지지 않게 하려는 것이다. */
void canvas_blit_half(canvas_t *dst, int dx, int dy, const canvas_t *src)
{
    if (!dst || !src) return;
    for (int y = 0; y + 1 < src->h; y += 2) {
        for (int x = 0; x + 1 < src->w; x += 2) {
            color_t pick = src->px[y * src->w + x];
            for (int k = 0; k < 4 && (pick == C_BG || pick == C_NONE); k++)
                pick = src->px[(y + (k >> 1)) * src->w + x + (k & 1)];
            ui_px(dst, dx + x / 2, dy + y / 2, pick == C_NONE ? C_BG : pick);
        }
    }
}

/* 픽셀 하나를 scale x scale로 키워서 놓는다. 그림은 그대로이고 크기만 커진다. */
void canvas_blit_scaled(canvas_t *dst, int dx, int dy, const canvas_t *src, int scale)
{
    if (!dst || !src || scale < 1) return;
    for (int y = 0; y < src->h; y++)
        for (int x = 0; x < src->w; x++)
            ui_rect(dst, dx + x * scale, dy + y * scale, scale, scale, src->px[y * src->w + x]);
}

/* 색 번호를 RGB로 바꾸는 표. include/ui.h의 이름표 순서와 같다. */
static const uint8_t palette[][3] = {
    [C_NONE]     = {  0,   0,   0},
    [C_BG]       = { 18,  20,  32},
    [C_TEXT]     = {235, 235, 225},
    [C_FLY_BODY] = { 70,  62,  58},
    [C_FLY_EYE]  = {214,  48,  49},
    [C_WING]     = {190, 220, 235},
    [C_WEB]      = {205, 210, 220},
    [C_GOOD]     = { 85, 200, 120},
    [C_BAD]      = {230,  80,  80},
    [C_WARN]     = {245, 190,  60},
    [C_WHITE]    = {255, 255, 255},
    [C_BLACK]    = {  0,   0,   0},
    [C_GRAY1]    = { 60,  64,  78},
    [C_GRAY2]    = {110, 115, 130},
    [C_GRAY3]    = {170, 175, 190},
    [C_RED]      = {220,  50,  47},
    [C_ORANGE]   = {240, 140,  40},
    [C_YELLOW]   = {250, 220,  70},
    [C_GREEN]    = { 60, 180,  75},
    [C_CYAN]     = { 70, 200, 220},
    [C_BLUE]     = { 60, 110, 220},
    [C_PURPLE]   = {150,  90, 210},
    [C_PINK]     = {245, 130, 180},
    [C_BROWN]    = {140,  95,  60},
    [CX_TAN]     = {235, 188, 140},
    [CX_CLAY]    = {193, 137,  88},
    [CX_MAROON]  = {130,  40,  60},
    [CX_TEAL]    = { 38, 140, 135},
    [CX_SLATE]   = { 42,  58,  75},
};

void ui_palette_rgb(color_t c, uint8_t rgb[3])
{
    if (c >= sizeof palette / sizeof palette[0]) c = C_BG;
    rgb[0] = palette[c][0];
    rgb[1] = palette[c][1];
    rgb[2] = palette[c][2];
}
