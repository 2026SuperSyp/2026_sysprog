/* 픽셀 그림으로 보낼 때 다시 그리는 것들: 글자, 말풍선, 진행자 시프봇.
 *
 * 화면 코드는 240x134 캔버스에 그린다. 그것을 그대로 키우면 네모난 면은 또렷하지만, 작은 픽셀로 찍은 그림(시프봇,
 * 말풍선의 깎은 모서리)은 큰 네모들로 보인다. 그래서 그런 것들은 캔버스에 그리지 않고 "무엇을 어디에"만 받아 두었다가
 * (ui_text_capture_begin, ui_capture_note) 키운 그림 위에 실제 해상도로 다시 그린다.
 *
 * 시프봇은 그림 파일이 아니라 식으로 그린다(타원, 세모, 선분을 겹친다). 흰 바탕에 주황과 회색 무늬가 있는 고양이다.
 * 가장자리는 픽셀이 덮인 만큼만 칠하므로 어떤 크기에서도 매끈하다. 좌표는 원래 그림(44x40)의 픽셀 단위다. */
#include "ui/hd.h"

#include <math.h>
#include <string.h>

typedef struct { float r, g, b; } col_t;
typedef struct { float r, g, b, a; } acc_t;          /* 겹쳐 칠한 결과. 색에는 덮는 정도가 곱해져 있다 */

static inline float clamp01(float v) { return v < 0.0f ? 0.0f : (v > 1.0f ? 1.0f : v); }

static inline void over(acc_t *o, col_t c, float cov)
{
    if (cov <= 0.0f) return;
    if (cov > 1.0f) cov = 1.0f;
    o->r += (c.r - o->r) * cov; o->g += (c.g - o->g) * cov; o->b += (c.b - o->b) * cov;
    o->a += (1.0f - o->a) * cov;
}

/* 타원의 가장자리까지의 거리(안쪽이 음수, 그림의 단위) */
static inline float d_ell(float u, float v, float cx, float cy, float rx, float ry)
{
    float nx = (u - cx) / rx, ny = (v - cy) / ry, f2 = nx * nx + ny * ny;
    if (f2 < 1e-6f) return -(rx < ry ? rx : ry);
    float f = sqrtf(f2), g = sqrtf(nx * nx / (rx * rx) + ny * ny / (ry * ry)) / f;
    return (f - 1.0f) / g;
}

static inline float d_seg(float u, float v, float ax, float ay, float bx, float by, float r)
{
    float vx = bx - ax, vy = by - ay, wx = u - ax, wy = v - ay, len2 = vx * vx + vy * vy;
    float t = len2 > 1e-9f ? clamp01((wx * vx + wy * vy) / len2) : 0.0f;
    float ex = wx - vx * t, ey = wy - vy * t;
    return sqrtf(ex * ex + ey * ey) - r;
}

/* 세모의 가장자리까지의 거리(안쪽이 음수). 꼭짓점의 순서는 어느 쪽이든 된다 */
static inline float d_tri(float u, float v, const float p[6])
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

static inline float cov(float d, float pp) { return clamp01(0.5f - d * pp); }

/* ---- 시프봇 -------------------------------------------------------------------------- */
typedef struct {
    int blink;                   /* 눈을 감았다 */
    float mouth;                 /* 입을 벌린 정도(0~1). 말할 때 */
    float sway;                  /* 꼬리가 흔들리는 양(-1~1) */
    int paw;                     /* 앞발을 들었다(안내할 때) */
} pose_t;

static const col_t OL = { 46, 52, 76 }, FUR = { 242, 242, 236 }, ORANGE = { 240, 146, 44 }, GREY = { 160, 168, 188 };
static const col_t PINK = { 245, 150, 185 }, EYE = { 38, 42, 60 }, WHISK = { 206, 210, 222 };
#define EDGE 0.85f               /* 테두리의 굵기 */

/* 테두리가 있는 덩어리 하나: 가장자리까지의 거리가 d인 점에 테두리 색을 칠하고, EDGE만큼 안쪽에 속의 색을 칠한다 */
static inline void blob(acc_t *o, float d, col_t fill, float pp)
{
    over(o, OL, cov(d, pp));
    over(o, fill, cov(d + EDGE, pp));
}

/* 시프봇의 한 점. u, v는 44x40 그림의 좌표, pp는 좌표 1의 픽셀 수 */
static acc_t cat_point(float u, float v, float pp, const pose_t *p)
{
    static const float ear_l[6] = { 5.0f, 11.0f, 9.5f, 0.9f, 17.0f, 6.5f }, ear_r[6] = { 22.6f, 6.5f, 30.1f, 0.9f, 34.6f, 11.0f };
    static const float in_l[6] = { 7.6f, 8.6f, 9.9f, 3.6f, 13.6f, 6.6f }, in_r[6] = { 26.0f, 6.6f, 29.7f, 3.6f, 32.0f, 8.6f };
    acc_t o = { 0, 0, 0, 0 };
    float sw = p->sway;

    /* 꼬리: 몸 뒤에서 오른쪽으로 나와 위로 말린다. 끝이 주황이다 */
    if (u > 28.0f && v > 20.0f) {
        float t1x = 36.5f, t1y = 35.0f, t2x = 40.6f + sw * 0.5f, t2y = 31.0f, t3x = 40.0f + sw * 1.6f, t3y = 25.6f;
        float d1 = fminf(d_seg(u, v, 31.0f, 33.5f, t1x, t1y, 2.5f), d_seg(u, v, t1x, t1y, t2x, t2y, 2.5f));
        float d2 = d_seg(u, v, t2x, t2y, t3x, t3y, 2.5f);
        over(&o, OL, cov(fminf(d1, d2), pp));
        over(&o, FUR, cov(d1 + EDGE, pp));
        over(&o, ORANGE, cov(d2 + EDGE, pp));
    }
    /* 몸과 발 */
    if (v > 21.0f) {
        float d = d_ell(u, v, 21.5f, 31.3f, 12.6f, 8.0f);
        blob(&o, d, FUR, pp);
        over(&o, GREY, cov(d_ell(u, v, 30.0f, 35.0f, 5.2f, 4.2f), pp) * cov(d + EDGE, pp));       /* 엉덩이의 회색 무늬 */
        blob(&o, d_ell(u, v, 15.6f, 38.2f, 3.5f, 1.7f), FUR, pp);
        blob(&o, d_ell(u, v, 25.8f, 38.2f, 3.5f, 1.7f), FUR, pp);
    }
    if (p->paw) {                                                    /* 들어 올린 앞발 */
        float d = fminf(d_seg(u, v, 32.0f, 27.5f, 37.6f, 22.4f, 2.1f), d_ell(u, v, 38.0f, 21.6f, 2.7f, 2.5f));
        blob(&o, d, FUR, pp);
    }
    if (v < 28.0f) {
        /* 귀와 머리. 무늬는 머리와 귀의 안쪽에만 칠한다 */
        float dl = d_tri(u, v, ear_l) - 0.8f, dr = d_tri(u, v, ear_r) - 0.8f, dh = d_ell(u, v, 19.8f, 15.4f, 17.3f, 11.3f);
        blob(&o, dl, FUR, pp);
        blob(&o, dr, FUR, pp);
        blob(&o, dh, FUR, pp);
        float inside = cov(fminf(dh, fminf(dl, dr)) + EDGE, pp);
        over(&o, ORANGE, cov(d_ell(u, v, 7.0f, 6.8f, 10.0f, 8.6f), pp) * inside);
        over(&o, GREY, cov(d_ell(u, v, 32.0f, 7.0f, 8.8f, 7.8f), pp) * inside);
        over(&o, PINK, cov(d_tri(u, v, in_l) - 0.5f, pp) * 0.9f);
        over(&o, PINK, cov(d_tri(u, v, in_r) - 0.5f, pp) * 0.9f);
        /* 얼굴 */
        float ey = p->blink ? 0.38f : 2.0f;
        over(&o, EYE, cov(fminf(d_ell(u, v, 13.0f, 16.6f, 1.3f, ey), d_ell(u, v, 26.6f, 16.6f, 1.3f, ey)), pp));
        if (!p->blink) over(&o, FUR, cov(fminf(d_ell(u, v, 13.4f, 15.8f, 0.45f, 0.55f), d_ell(u, v, 27.0f, 15.8f, 0.45f, 0.55f)), pp));
        over(&o, PINK, cov(fminf(d_ell(u, v, 7.0f, 20.4f, 2.4f, 1.3f), d_ell(u, v, 32.6f, 20.4f, 2.4f, 1.3f)), pp) * 0.85f);
        over(&o, (col_t){ 226, 112, 146 }, cov(d_ell(u, v, 19.8f, 19.3f, 1.0f, 0.68f), pp));
        if (p->mouth > 0.05f) {
            over(&o, (col_t){ 112, 44, 58 }, cov(d_ell(u, v, 19.8f, 21.4f, 1.25f, 0.5f + 0.75f * p->mouth), pp));
        } else {
            float d = fminf(d_seg(u, v, 19.8f, 20.0f, 19.8f, 20.8f, 0.26f),
                            fminf(d_seg(u, v, 19.8f, 20.8f, 18.4f, 21.5f, 0.26f), d_seg(u, v, 19.8f, 20.8f, 21.2f, 21.5f, 0.26f)));
            over(&o, EYE, cov(d, pp));
        }
        float w = fminf(fminf(d_seg(u, v, 3.2f, 17.6f, -0.4f, 16.7f, 0.2f), d_seg(u, v, 3.0f, 19.4f, -0.6f, 19.8f, 0.2f)),
                        fminf(d_seg(u, v, 36.4f, 17.6f, 40.0f, 16.7f, 0.2f), d_seg(u, v, 36.6f, 19.4f, 40.2f, 19.8f, 0.2f)));
        over(&o, WHISK, cov(w, pp));
    }
    return o;
}

typedef struct { image_t *im; float x, y, k; int x0, x1, y0; pose_t pose; } cat_job_t;

static void cat_rows(int from, int to, void *arg)
{
    const cat_job_t *j = arg;
    for (int y = j->y0 + from; y < j->y0 + to; y++) {
        if (y < 0 || y >= j->im->h) continue;
        float v = ((float)y + 0.5f - j->y) / j->k;
        for (int x = j->x0; x < j->x1; x++) {
            if (x < 0 || x >= j->im->w) continue;
            acc_t o = cat_point(((float)x + 0.5f - j->x) / j->k, v, j->k, &j->pose);
            if (o.a <= 0.003f) continue;
            uint8_t *p = j->im->px + ((size_t)y * (size_t)j->im->w + (size_t)x) * 3;
            float keep = 1.0f - o.a;
            p[0] = (uint8_t)((float)p[0] * keep + o.r); p[1] = (uint8_t)((float)p[1] * keep + o.g); p[2] = (uint8_t)((float)p[2] * keep + o.b);
        }
    }
}

/* 시프봇을 (x, y)에 그린다(44x40 그림의 왼쪽 위가 놓일 자리, 픽셀). k는 그림의 좌표 1이 몇 픽셀인가 */
static void draw_cat(image_t *im, float x, float y, float k, const pose_t *pose)
{
    cat_job_t job = { im, x, y, k, (int)floorf(x - 2.0f * k), (int)ceilf(x + 46.0f * k), (int)floorf(y - 1.0f * k), *pose };
    par_for((int)ceilf(42.0f * k), 8, cat_rows, &job);
}

/* ---- 말풍선 ------------------------------------------------------------------------- */
/* 세모 하나를 칠한다(가장자리는 부드럽게) */
static void fill_tri(image_t *im, const float p[6], rgb_t c)
{
    float x0 = fminf(p[0], fminf(p[2], p[4])), x1 = fmaxf(p[0], fmaxf(p[2], p[4]));
    float y0 = fminf(p[1], fminf(p[3], p[5])), y1 = fmaxf(p[1], fmaxf(p[3], p[5]));
    for (int y = (int)floorf(y0) - 1; y <= (int)ceilf(y1); y++)
        for (int x = (int)floorf(x0) - 1; x <= (int)ceilf(x1); x++)
            hd_blend(im, x, y, c, cov(d_tri((float)x + 0.5f, (float)y + 0.5f, p), 1.0f));
}

/* 모서리가 둥근 검은 상자에 흰 테두리, 아래로 내려오는 꼬리. 자리는 캔버스의 좌표이고 n배로 키운다 */
static void draw_bubble(image_t *im, const ui_text_item_t *it, int n)
{
    float k = (float)n, x = (float)it->x * k, y = (float)it->y * k, w = (float)it->w * k, h = (float)it->h * k, line = k;
    rgb_t white = hd_rgb(255, 255, 255), black = hd_rgb(0, 0, 0);
    hd_rrect(im, x, y, w, h, 3.5f * k, white, 1.0f);
    if (it->a >= 0) {                                /* 꼬리의 자리 */
        float tx = (float)it->a * k, by = y + h - line * 0.5f;
        float outer[6] = { tx - 0.5f * k, by, tx + 4.5f * k, by, tx + 2.0f * k, by + 5.4f * k };
        float inner[6] = { tx + 0.9f * k, by - line, tx + 3.1f * k, by - line, tx + 2.0f * k, by + 3.4f * k };
        fill_tri(im, outer, white);
        hd_rrect(im, x + line, y + line, w - 2.0f * line, h - 2.0f * line, 2.6f * k, black, 1.0f);
        fill_tri(im, inner, black);
    } else {
        hd_rrect(im, x + line, y + line, w - 2.0f * line, h - 2.0f * line, 2.6f * k, black, 1.0f);
    }
}

/* ---- 받아 둔 것들을 그린다 ------------------------------------------------------------ */
void hd_items(image_t *im, const ui_text_item_t *items, int n_items, int n)
{
    if (!im || !items || n < 1) return;
    float k = (float)n;
    for (int i = 0; i < n_items; i++) {
        const ui_text_item_t *it = &items[i];
        if (it->kind == UI_HD_TEXT) {
            hd_text(im, (float)it->x * k, (float)it->y * k, it->s, hd_color(it->col), (float)UI_KFONT_H * k, 1.0f);
        } else if (it->kind == UI_HD_BUBBLE) {
            draw_bubble(im, it, n);
        } else if (it->kind == UI_HD_SPRITE) {
            /* 시프봇의 그림들: 서 있기(6장), 말하기(3장), 안내하기(3장). 장수에 맞춰 눈 깜빡임, 입, 꼬리를 움직인다.
             * 그림마다 폭이 달라서(44, 50) 가운데를 맞춘다 */
            int talk = strcmp(it->s, "sipbot_talk") == 0, guide = strcmp(it->s, "sipbot_guide") == 0, f = it->a;
            pose_t pose = { .blink = !talk && !guide && f == 4, .mouth = talk ? (float)f * 0.5f : (guide ? (float)(f % 2) * 0.6f : 0.0f),
                            .sway = sinf((float)f * 1.05f), .paw = guide };
            draw_cat(im, ((float)it->x + (float)(it->w - 44) * 0.5f) * k, ((float)it->y + (float)(it->h - 40)) * k, k, &pose);
        } else if (it->kind == UI_HD_COUNT) {
            /* 카운트다운: 시프봇을 크게 그리고 옆에 3, 2, 1, GO!를 쓴다 */
            static const char *const word[4] = { "3", "2", "1", "GO!" };
            int step = it->a < 0 ? 0 : (it->a > 3 ? 3 : it->a);
            pose_t pose = { .blink = 0, .mouth = step == 3 ? 1.0f : 0.0f, .sway = (float)(step % 2) * 2.0f - 1.0f, .paw = step == 3 };
            float cell = 60.0f * k, tw = hd_text_w(word[step], cell);
            draw_cat(im, 34.0f * k, 30.0f * k, 2.0f * k, &pose);
            hd_text(im, 178.0f * k - tw * 0.5f, 42.0f * k, word[step], hd_color(step == 3 ? C_GOOD : C_YELLOW), cell, 1.0f);
        }
    }
}
