/* 픽셀 그림으로 보낼 때 다시 그리는 것들: 글자, 말풍선 상자, 진행자 시프봇.
 *
 * 화면 코드는 240x134 캔버스에 그린다. 그것을 그대로 키우면 네모난 면은 또렷하지만 글자와 말풍선의 깎은 모서리는
 * 큰 네모들로 보인다. 그래서 이 둘은 캔버스에 그리지 않고 "무엇을 어디에"만 받아 두었다가(ui_text_capture_begin,
 * ui_capture_note) 키운 그림 위에 실제 해상도로 다시 그린다.
 * 진행자 시프봇은 그림(.spr)을 바꾸지 않고 같은 그림의 윤곽을 매끈하게 이어 키운다(draw_sprite). 다른 그림은
 * 캔버스에 그린 그대로 키운다. */
#include "ui/hd.h"

#include <math.h>

/* 세모의 가장자리까지의 거리(안쪽이 음수). 꼭짓점의 순서는 어느 쪽이든 된다 */
static float d_tri(float u, float v, const float p[6])
{
    float area = (p[2] - p[0]) * (p[5] - p[1]) - (p[4] - p[0]) * (p[3] - p[1]), s = area < 0.0f ? -1.0f : 1.0f, d = -1e9f;
    for (int i = 0; i < 3; i++) {
        float ax = p[i * 2], ay = p[i * 2 + 1], bx = p[(i + 1) % 3 * 2], by = p[(i + 1) % 3 * 2 + 1];
        float ex = bx - ax, ey = by - ay, len = sqrtf(ex * ex + ey * ey);
        float e = s * ((u - ax) * ey - (v - ay) * ex) / (len > 1e-6f ? len : 1e-6f);
        if (e > d) d = e;
    }
    return d;
}

/* 세모 하나를 칠한다(가장자리는 부드럽게) */
static void fill_tri(image_t *im, const float p[6], rgb_t c)
{
    float x0 = fminf(p[0], fminf(p[2], p[4])), x1 = fmaxf(p[0], fmaxf(p[2], p[4]));
    float y0 = fminf(p[1], fminf(p[3], p[5])), y1 = fmaxf(p[1], fmaxf(p[3], p[5]));
    for (int y = (int)floorf(y0) - 1; y <= (int)ceilf(y1); y++)
        for (int x = (int)floorf(x0) - 1; x <= (int)ceilf(x1); x++) {
            float cov = 0.5f - d_tri((float)x + 0.5f, (float)y + 0.5f, p);
            hd_blend(im, x, y, c, cov > 1.0f ? 1.0f : cov);
        }
}

/* 모서리가 둥근 검은 상자에 흰 테두리, 아래로 내려오는 꼬리. 자리는 캔버스의 좌표이고 k배로 키워 (ox, oy)만큼 옮긴다 */
static void draw_bubble(image_t *im, const ui_text_item_t *it, float k, float ox, float oy)
{
    float x = ox + (float)it->x * k, y = oy + (float)it->y * k, w = (float)it->w * k, h = (float)it->h * k, line = k;
    rgb_t white = hd_rgb(255, 255, 255), black = hd_rgb(0, 0, 0);
    hd_rrect(im, x, y, w, h, 3.5f * k, white, 1.0f);
    if (it->a >= 0) {                                /* 꼬리의 자리 */
        float tx = ox + (float)it->a * k, by = y + h - line * 0.5f;
        float outer[6] = { tx - 0.5f * k, by, tx + 4.5f * k, by, tx + 2.0f * k, by + 5.4f * k };
        float inner[6] = { tx + 0.9f * k, by - line, tx + 3.1f * k, by - line, tx + 2.0f * k, by + 3.4f * k };
        fill_tri(im, outer, white);
        hd_rrect(im, x + line, y + line, w - 2.0f * line, h - 2.0f * line, 2.6f * k, black, 1.0f);
        fill_tri(im, inner, black);
    } else {
        hd_rrect(im, x + line, y + line, w - 2.0f * line, h - 2.0f * line, 2.6f * k, black, 1.0f);
    }
}

/* 그림(.spr) 한 장을 k배로, 계단 없이 그린다. 그림은 그대로 두고 그 윤곽을 매끈하게 잇는 것이다.
 * 색마다 "그 색인 픽셀은 1, 아니면 0"인 판을 생각하고, 픽셀의 가운데들 사이를 고르게 이어(쌍선형) 값이 기준을
 * 넘는 곳을 그 색으로 칠한다. 그러면 계단으로 찍힌 비스듬한 선이 곧게 이어지고 모서리가 둥글어진다. 가장자리는
 * 값이 변하는 빠르기에서 픽셀이 덮인 정도를 구해 부드럽게 한다.
 * 흰색과 회색은 한 무리로 본다. 구상안의 윤곽선이 흰 점과 회색 점을 번갈아 찍은 것이라 따로 이으면 점선이 된다.
 * 그 무리 안에서 넓게 칠한 곳(둘레 여덟 칸이 거의 다 같은 무리인 픽셀과 그 이웃)은 회색 털 무늬이고 나머지는
 * 윤곽선이다. 윤곽선은 한 가지 밝은 색으로 칠한다. 선은 기준을 낮게 잡아 비스듬한 곳에서 끊기지 않게 한다.
 * 넓게 칠한 곳 안에 한 픽셀씩 뚫린 구멍(구상안을 줄이다 생긴 것)은 둘레의 색으로 메운다 */
#define SPR_MAX (96 * 96)
#define IS_LIGHT(c) ((c) == C_WHITE || (c) == C_GRAY3)
static void draw_sprite(image_t *im, const ui_text_item_t *it, float k, float ox, float oy)
{
    static const rgb_t line = { 234, 237, 244 };
    static color_t px[SPR_MAX];
    static uint8_t wide[SPR_MAX];                    /* 넓게 칠한 곳인가 */
    int w = 0, h = 0;
    const color_t *src = ui_sprite_pixels(ui_sprite_load(it->s), it->a, &w, &h);
    if (!src || k <= 0.0f || w * h > SPR_MAX) return;
    for (int y = 0; y < h; y++)
        for (int x = 0; x < w; x++) {
            color_t c = src[y * w + x], best = C_NONE;
            int filled = 0, light = 0, most = 0;
            for (int q = 0; q < 9; q++) {
                int nx = x + q % 3 - 1, ny = y + q / 3 - 1, same = 0;
                if (nx < 0 || ny < 0 || nx >= w || ny >= h) continue;
                color_t n = src[ny * w + nx];
                light += IS_LIGHT(n);
                if (q == 4 || n == C_NONE) continue;
                filled++;
                for (int p = 0; p < 9; p++) {
                    int mx = x + p % 3 - 1, my = y + p / 3 - 1;
                    same += p != 4 && mx >= 0 && my >= 0 && mx < w && my < h && src[my * w + mx] == n;
                }
                if (same > most) { most = same; best = n; }
            }
            if (c == C_NONE && filled >= 7) { c = best; light += IS_LIGHT(c); }      /* 구멍을 메운다 */
            px[y * w + x] = c;
            wide[y * w + x] = IS_LIGHT(c) && light >= 7;
        }
    for (int y = 0; y < h; y++)                      /* 넓게 칠한 곳의 가장자리 픽셀도 무늬다(2: 이웃이 넓다) */
        for (int x = 0; x < w; x++) {
            if (wide[y * w + x] || !IS_LIGHT(px[y * w + x])) continue;
            for (int q = 0; q < 9; q++) {
                int nx = x + q % 3 - 1, ny = y + q / 3 - 1;
                if (nx >= 0 && ny >= 0 && nx < w && ny < h && wide[ny * w + nx] == 1) { wide[y * w + x] = 2; break; }
            }
        }
    float left = ox + (float)it->x * k, top = oy + (float)it->y * k;
    int x0 = (int)floorf(left - k), x1 = (int)ceilf(left + (float)(w + 1) * k);
    int y0 = (int)floorf(top - k), y1 = (int)ceilf(top + (float)(h + 1) * k);
    if (x0 < 0) x0 = 0;
    if (y0 < 0) y0 = 0;
    if (x1 > im->w) x1 = im->w;
    if (y1 > im->h) y1 = im->h;
    for (int y = y0; y < y1; y++) {
        float sy = ((float)y + 0.5f - top) / k - 0.5f;               /* 그림의 픽셀 가운데를 정수로 보는 좌표 */
        int iy = (int)floorf(sy);
        float ty = sy - (float)iy;
        for (int x = x0; x < x1; x++) {
            float sx = ((float)x + 0.5f - left) / k - 0.5f;
            int ix = (int)floorf(sx);
            float tx = sx - (float)ix;
            color_t c[4];                                            /* 둘레의 네 픽셀: 왼쪽 위, 오른쪽 위, 왼쪽 아래, 오른쪽 아래 */
            int in_wide[4];
            const float wt[4] = { (1.0f - tx) * (1.0f - ty), tx * (1.0f - ty), (1.0f - tx) * ty, tx * ty };
            int any = 0;
            for (int q = 0; q < 4; q++) {
                int cx = ix + (q & 1), cy = iy + (q >> 1);
                c[q] = cx >= 0 && cy >= 0 && cx < w && cy < h ? px[cy * w + cx] : C_NONE;
                in_wide[q] = c[q] != C_NONE && wide[cy * w + cx];
                any |= c[q] != C_NONE;
            }
            if (!any) continue;
            /* 색 무리마다 한 번씩. 흰색과 회색(무리 0)은 선이라 맨 나중에 얹는다 */
            for (int pass = 0; pass < 2; pass++) {
                for (int q = 0; q < 4; q++) {
                    int light = IS_LIGHT(c[q]), seen = 0;
                    if (c[q] == C_NONE || light != pass) continue;
                    for (int p = 0; p < q; p++)                      /* 같은 무리를 두 번 칠하지 않는다 */
                        seen |= pass ? IS_LIGHT(c[p]) : c[p] == c[q];
                    if (seen) continue;
                    float m[4], f = 0.0f, r = 0.0f, g = 0.0f, b = 0.0f;
                    for (int p = 0; p < 4; p++) {
                        m[p] = (pass ? IS_LIGHT(c[p]) : c[p] == c[q]) ? 1.0f : 0.0f;
                        if (m[p] > 0.0f) {
                            rgb_t col = !pass ? hd_color(c[p]) : (in_wide[p] ? hd_color(C_GRAY3) : line);
                            f += wt[p]; r += wt[p] * (float)col.r; g += wt[p] * (float)col.g; b += wt[p] * (float)col.b;
                        }
                    }
                    float gx = (m[1] - m[0]) * (1.0f - ty) + (m[3] - m[2]) * ty, gy = (m[2] - m[0]) * (1.0f - tx) + (m[3] - m[1]) * tx;
                    float slope = sqrtf(gx * gx + gy * gy), edge = pass ? 0.36f : 0.45f;
                    float cov = slope < 1e-4f ? (f > edge ? 1.0f : 0.0f) : 0.5f + (f - edge) / slope * k;
                    if (cov <= 0.0f || f <= 0.0f) continue;
                    hd_blend(im, x, y, hd_rgb((uint8_t)(r / f), (uint8_t)(g / f), (uint8_t)(b / f)), cov > 1.0f ? 1.0f : cov);
                }
            }
        }
    }
}

void hd_items(image_t *im, const ui_text_item_t *items, int n_items, int n, int ox, int oy)
{
    if (!im || !items || n < 1) return;
    float k = (float)n;
    for (int i = 0; i < n_items; i++) {
        const ui_text_item_t *it = &items[i];
        if (it->kind == UI_HD_TEXT)
            hd_text(im, (float)ox + (float)it->x * k, (float)oy + (float)it->y * k, it->s, hd_color(it->col), (float)UI_KFONT_H * k, 1.0f);
        else if (it->kind == UI_HD_BUBBLE)
            draw_bubble(im, it, k, (float)ox, (float)oy);
        else if (it->kind == UI_HD_SPRITE)
            draw_sprite(im, it, k, (float)ox, (float)oy);
    }
}
