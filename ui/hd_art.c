/* 픽셀 그림으로 보낼 때 다시 그리는 것들: 글자와 말풍선 상자.
 *
 * 화면 코드는 240x134 캔버스에 그린다. 그것을 그대로 키우면 네모난 면은 또렷하지만 글자와 말풍선의 깎은 모서리는
 * 큰 네모들로 보인다. 그래서 이 둘은 캔버스에 그리지 않고 "무엇을 어디에"만 받아 두었다가(ui_text_capture_begin,
 * ui_capture_note) 키운 그림 위에 실제 해상도로 다시 그린다.
 * 진행자 시프봇을 비롯한 그림(.spr)은 손대지 않는다. 그린 사람의 그림 그대로 캔버스에 그려 키운다. */
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
    }
}
