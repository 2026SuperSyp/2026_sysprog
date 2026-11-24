/* hd.h : 실제 픽셀로 그리는 화면 (코어 전용)
 *
 * 캔버스(canvas_t)는 색 번호 격자이고 터미널의 글자 칸으로 보낸다(present.c). 그 방식은 한 칸에 색을 둘밖에 못 써서
 * 부드러운 명암이나 무늬를 넣으면 내보내는 양이 감당이 안 된다. 터미널이 Sixel 그림을 받아 주면(term_caps) 화면을
 * 색이 자유로운 픽셀 그림(image_t)으로 만들어 통째로 보낸다(sixel.c). 여기에는 그 그림에 그리는 함수들이 있다.
 *
 *   - 도형은 가장자리를 픽셀이 덮이는 만큼만 칠해서 계단이 보이지 않는다(좌표는 소수여도 된다).
 *   - 글자는 48픽셀로 뽑은 글꼴(ui/assets/font48.fnt, tools/font_hd.py)을 원하는 크기로 줄이거나 키워 그린다.
 *     글자가 차지하는 칸의 비율은 12x12 글꼴과 같다. 한 칸의 크기(cell)가 12배율이면 ui_text와 같은 자리에 놓인다.
 *   - hd_upscale은 240x134 캔버스(지금까지의 화면)를 키워 놓는다. 픽셀 그림의 계단을 깎아 내고 가장자리를 섞는다. */
#ifndef SSP_HD_H
#define SSP_HD_H

#include <stdint.h>
#include "ui/ui_internal.h"

typedef struct image { int w, h; uint8_t *px; } image_t;    /* px는 w * h * 3바이트(R, G, B). px[(y * w + x) * 3] */
typedef struct { uint8_t r, g, b; } rgb_t;

image_t *image_new(int w, int h);
void     image_free(image_t *im);

rgb_t hd_color(color_t c);                              /* 색 번호의 색 */
rgb_t hd_mix(rgb_t a, rgb_t b, float t);                /* t가 0이면 a, 1이면 b */
static inline rgb_t hd_rgb(int r, int g, int b) { return (rgb_t){ (uint8_t)r, (uint8_t)g, (uint8_t)b }; }

/* 도형. a는 덮는 정도(0~1)다. 가장자리는 부드럽게 칠한다 */
void hd_fill(image_t *im, rgb_t c);
void hd_rrect(image_t *im, float x, float y, float w, float h, float radius, rgb_t c, float a);    /* 모서리가 둥근 네모 */
void hd_vgrad(image_t *im, float x, float y, float w, float h, float radius, rgb_t top, rgb_t bot, float a);
void hd_ellipse(image_t *im, float cx, float cy, float rx, float ry, rgb_t c, float a);
void hd_line(image_t *im, float x0, float y0, float x1, float y1, float thick, rgb_t c, float a);  /* 끝이 둥근 선 */
void hd_ring(image_t *im, float cx, float cy, float r, float thick, rgb_t c, float a);
static inline void hd_rect(image_t *im, float x, float y, float w, float h, rgb_t c, float a) { hd_rrect(im, x, y, w, h, 0.0f, c, a); }

/* 픽셀 하나에 색을 섞는다(범위 밖이면 아무 일도 없다) */
static inline void hd_blend(image_t *im, int x, int y, rgb_t c, float a)
{
    if (x < 0 || y < 0 || x >= im->w || y >= im->h || a <= 0.0f) return;
    uint8_t *p = im->px + ((size_t)y * (size_t)im->w + (size_t)x) * 3;
    if (a >= 1.0f) { p[0] = c.r; p[1] = c.g; p[2] = c.b; return; }
    p[0] = (uint8_t)((float)p[0] + ((float)c.r - (float)p[0]) * a);
    p[1] = (uint8_t)((float)p[1] + ((float)c.g - (float)p[1]) * a);
    p[2] = (uint8_t)((float)p[2] + ((float)c.b - (float)p[2]) * a);
}

/* 글자. (x, y)는 왼쪽 위이고 cell은 한글 한 글자가 차지하는 칸의 크기(픽셀)다. 영문과 숫자는 그 절반 폭이다.
 * 놓는 규칙은 ui_text와 같다(한글이 섞인 줄에서는 영문을 조금 내려 그린다). */
void  hd_text(image_t *im, float x, float y, const char *s, rgb_t c, float cell, float a);
float hd_text_w(const char *s, float cell);

/* 캔버스를 n배로 키워 (dx, dy)에 놓는다. 픽셀 그림의 계단을 깎아 내고 가장자리를 섞는다. 그림 밖은 잘라낸다 */
void hd_canvas(image_t *im, int dx, int dy, const canvas_t *src, int n);
/* 캔버스를 그림 전체에 키워 놓는다. 그림의 크기는 캔버스의 정수배여야 한다(아니면 아무것도 하지 않는다) */
void hd_upscale(image_t *im, const canvas_t *src);

/* ---- Sixel로 보내기 (sixel.c) ---- */
typedef struct sixel_state sixel_state_t;
sixel_state_t *sixel_new(void);
void sixel_free(sixel_state_t *st);
void sixel_invalidate(sixel_state_t *st);        /* 다음 번에 전부 다시 보내게 한다 */
/* 그림이 차지하는 글자 칸 수. cell_w, cell_h는 터미널이 글자 한 칸으로 치는 픽셀 수다(term_caps) */
void sixel_cells(const image_t *im, int cell_w, int cell_h, int *cols, int *rows);
/* 그림을 터미널 가운데에 놓고 보낼 글자열을 만든다. 지난번과 달라진 줄만 담는다. 글자열은 다음에 부를 때까지 유효하고
 * 길이는 len에 온다(바뀐 것이 없으면 0). 터미널이 그림보다 작으면 NULL을 돌려준다. */
const char *sixel_encode(sixel_state_t *st, const image_t *im, int cols, int rows, int cell_w, int cell_h, size_t *len);
/* 터미널 글자 칸(0부터)을 그림의 픽셀로 바꾼다(그 칸의 가운데). 그림 밖이면 0을 돌려준다 */
int  sixel_cell_to_pixel(const image_t *im, int cols, int rows, int cell_w, int cell_h, int cell_x, int cell_y, int *px, int *py);

#endif
