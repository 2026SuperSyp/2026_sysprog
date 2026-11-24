/* 글자로 찍는 그림: 입장 화면, 지난 랭킹, 로비(픽셀 그림으로 보낼 때). 세 화면이 같은 그림을 쓰고 오른쪽의 글자만 다르다.
 *
 * 화면을 작은 글자 칸(가로 400, 세로 134)으로 나누고, 칸마다 "얼마나 밝은가"와 색을 정한 뒤 밝기에 맞는 글자를
 * 그 색으로 찍는다. 밝은 곳에는 획이 많은 글자(@, #)가, 어두운 곳에는 점(. :)이 찍혀서 멀리서 보면 명암이 있는
 * 그림이 되고 가까이서 보면 글자다. 터미널에서 글자만으로 그리는 그림(코드 아트)의 방식이다.
 *
 * 그리는 것: 받침 위의 유리 돔. 돔 안에는 진행자 시프봇이 서 있고(그림 파일의 픽셀 하나를 글자 두 칸으로 옮긴다),
 * 그 둘레를 초파리가 돈다(상자 몇 개로 된 모형의 겉을 점으로 찍는다). 돔 안팎으로 색종이가 눈처럼 내린다.
 * 칸마다 깊이를 적어 두어 초파리가 시프봇의 뒤로 돌아가면 가려진다. 오른쪽에는 참가자와 안내를 작은 글자로 적는다.
 * 움직임은 모두 시각 t의 식이라 상태를 들고 있지 않다. */
#include "ui/hd.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

#define COLS 400
#define ROWS 134
#define FAR  (-1e9f)

typedef struct { float r, g, b, lum, z; } acell_t;

static acell_t grid[ROWS][COLS];
static float GW, GH;                 /* 글자 칸의 크기(픽셀) */

static inline float clamp01(float v) { return v < 0.0f ? 0.0f : (v > 1.0f ? 1.0f : v); }
static inline float smooth(float a, float b, float v) { float t = clamp01((v - a) / (b - a)); return t * t * (3.0f - 2.0f * t); }

/* 수 둘을 고르게 섞은 0~1의 값 */
static float hash2(int a, int b)
{
    uint32_t h = (uint32_t)a * 374761393u + (uint32_t)b * 668265263u;
    h = (h ^ (h >> 13)) * 1274126177u;
    return (float)((h ^ (h >> 16)) & 0xffffu) / 65535.0f;
}

/* 칸 하나를 칠한다. z가 더 가까운(큰) 것이 남는다 */
static inline void put(int col, int row, float z, float r, float g, float b, float lum)
{
    if (col < 0 || row < 0 || col >= COLS || row >= ROWS) return;
    acell_t *c = &grid[row][col];
    if (z < c->z) return;
    *c = (acell_t){ r, g, b, lum, z };
}

/* 화면의 점(픽셀)을 칠한다 */
static inline void put_px(float x, float y, float z, float r, float g, float b, float lum)
{
    put((int)floorf(x / GW), (int)floorf(y / GH), z, r, g, b, lum);
}

/* ---- 초파리 ------------------------------------------------------------------------- */
/* 상자의 겉을 점으로 찍는다. c는 가운데, h는 반 크기(초파리의 좌표: 앞 +x, 오른쪽 +y, 위 +z), roll은 x축으로 돌린
 * 각도다. 초파리는 (ox, oy)에 있고(화면 픽셀, oz는 깊이), yaw만큼 돌아 있고, 한 단위가 s픽셀이다.
 * kind 0: 몸(뒤쪽에 짙은 줄), 1: 눈, 2: 날개 */
static void fly_box(const float c[3], const float h[3], float roll, float ox, float oy, float oz, float yaw, float s, int kind)
{
    static const float col[3][3] = { { 226, 170, 88 }, { 228, 48, 54 }, { 196, 226, 246 } };
    float cy = cosf(yaw), sy = sinf(yaw), cr = cosf(roll), sr = sinf(roll), step = 0.45f * GW / s;
    for (int ax = 0; ax < 3; ax++) {
        int ua = (ax + 1) % 3, va = (ax + 2) % 3;
        for (int sg = -1; sg <= 1; sg += 2) {
            /* 면의 바깥쪽이 받는 빛(왼쪽 위 앞에서 온다). 뒤를 향한 면은 가려지므로 찍지 않아도 된다 */
            float n[3] = { 0, 0, 0 };
            n[ax] = (float)sg;
            float ny = cr * n[1] - sr * n[2], nz = sr * n[1] + cr * n[2];
            float wx = n[0] * cy - ny * sy, wd = n[0] * sy + ny * cy;      /* 화면의 오른쪽, 화면 쪽(가까운 쪽) */
            float lum = 0.42f + 0.58f * clamp01(-0.35f * wx + 0.62f * nz + 0.55f * wd);
            for (float u = -h[ua]; u <= h[ua]; u += step)
                for (float v = -h[va]; v <= h[va]; v += step) {
                    float p[3];
                    p[ax] = (float)sg * h[ax]; p[ua] = u; p[va] = v;
                    float k = 1.0f;
                    if (kind == 0 && ax != 0 && ((p[0] > -0.41f && p[0] < -0.28f) || (p[0] > -0.16f && p[0] < -0.03f))) k = 0.3f;
                    float py = c[1] + cr * p[1] - sr * p[2], pz = c[2] + sr * p[1] + cr * p[2], px = c[0] + p[0];
                    float X = px * cy - py * sy, D = px * sy + py * cy;
                    put_px(ox + X * s, oy - (pz + 0.22f * D) * s, oz + D * s,
                           col[kind][0] * k, col[kind][1] * k, col[kind][2] * k, kind == 2 ? 0.62f : lum * (0.5f + 0.5f * k));
                }
        }
    }
}

static void draw_fly(float ox, float oy, float oz, float yaw, float s, double t)
{
    float flap = sinf((float)t * 46.0f) > 0.0f ? 0.85f : 0.10f;
    fly_box((float[3]){ 0, 0, 0 }, (float[3]){ 0.50f, 0.40f, 0.34f }, 0.0f, ox, oy, oz, yaw, s, 0);
    for (int e = -1; e <= 1; e += 2) {
        fly_box((float[3]){ 0.47f, (float)e * 0.27f, 0.09f }, (float[3]){ 0.07f, 0.15f, 0.16f }, 0.0f, ox, oy, oz, yaw, s, 1);
        float roll = (float)e * flap, w = (float)e * 0.22f;
        fly_box((float[3]){ -0.07f, (float)e * 0.17f + cosf(roll) * w, 0.36f + sinf(roll) * w }, (float[3]){ 0.28f, 0.22f, 0.012f },
                roll, ox, oy, oz, yaw, s, 2);
    }
}

/* ---- 시프봇 ------------------------------------------------------------------------- */
/* 그림(.spr)의 픽셀 하나를 글자 두 칸(가로)으로 옮긴다. 글자 칸이 세로로 길어서 그래야 비율이 맞는다.
 * (cx, by)는 발밑 가운데의 화면 자리(픽셀).
 * 그림은 윤곽선만 있고 몸 안쪽이 비어 있어서, 글자로 찍으면 내리는 색종이에 묻힌다. 그래서 윤곽선으로 둘러싸인
 * 안쪽을 찾아 옅은 글자로 채운다(윤곽선과 무늬는 그대로 밝다). 안쪽은 "그림의 바깥 테두리에서 빈 픽셀을 따라가도
 * 닿지 못하는 빈 픽셀"이다. 윤곽선에 한두 픽셀씩 끊긴 데가 있어서, 따라갈 때는 윤곽선 바로 옆의 픽셀도 막힌
 * 것으로 친다 */
#define CAT_MAX (96 * 96)
static void draw_cat(float cx, float by, double t)
{
    static uint8_t near_ink[CAT_MAX], outside[CAT_MAX];
    static int queue[CAT_MAX];
    int w = 0, h = 0;
    sprite_id_t id = ui_sprite_load("sipbot_idle");
    int frames = ui_sprite_frames(id);
    const color_t *px = ui_sprite_pixels(id, frames > 0 ? (int)(t * 3.0) % frames : 0, &w, &h);
    if (!px || w * h > CAT_MAX) return;
    int c0 = (int)(cx / GW) - w, r0 = (int)(by / GH) - h, head = 0, tail = 0;
    for (int y = 0; y < h; y++)
        for (int x = 0; x < w; x++) {
            int ink = 0;
            for (int q = 0; q < 9; q++) {
                int nx = x + q % 3 - 1, ny = y + q / 3 - 1;
                ink |= nx >= 0 && ny >= 0 && nx < w && ny < h && px[ny * w + nx] != C_NONE;
            }
            near_ink[y * w + x] = (uint8_t)ink;
            outside[y * w + x] = 0;
            if ((x == 0 || y == 0 || x == w - 1 || y == h - 1) && !ink) { outside[y * w + x] = 1; queue[tail++] = y * w + x; }
        }
    while (head < tail) {                            /* 바깥에서 빈 픽셀을 따라 퍼진다 */
        int at = queue[head++], x = at % w, y = at / w;
        for (int q = 0; q < 4; q++) {
            int nx = x + (q == 0) - (q == 1), ny = y + (q == 2) - (q == 3);
            if (nx < 0 || ny < 0 || nx >= w || ny >= h || outside[ny * w + nx] || near_ink[ny * w + nx]) continue;
            outside[ny * w + nx] = 1;
            queue[tail++] = ny * w + nx;
        }
    }
    for (int y = 0; y < h; y++)
        for (int x = 0; x < w; x++) {
            color_t c = px[y * w + x];
            if (c == C_NONE) {                           /* 안쪽이면 옅게 채운다. 바깥과 맞닿은 픽셀은 바깥이다 */
                int edge = outside[y * w + x] || x == 0 || y == 0 || x == w - 1 || y == h - 1;
                for (int q = 0; q < 9 && !edge; q++) edge = outside[(y + q / 3 - 1) * w + x + q % 3 - 1];
                if (!edge)
                    for (int d = 0; d < 2; d++) put(c0 + x * 2 + d, r0 + y, 0.0f, 196, 204, 226, 0.22f + 0.08f * hash2(x * 2 + d, y));
                continue;
            }
            rgb_t k = hd_color(c);
            float lum = c == C_WHITE ? 1.0f : (c == C_GRAY3 ? 0.78f : 0.86f);
            for (int d = 0; d < 2; d++)
                put(c0 + x * 2 + d, r0 + y, 0.0f, (float)k.r, (float)k.g, (float)k.b, lum * (0.9f + 0.1f * hash2(x * 2 + d, y)));
        }
}

/* ---- 화면 --------------------------------------------------------------------------- */
static const rgb_t ink = { 226, 228, 236 }, dim = { 120, 126, 146 }, key = { 250, 215, 70 }, sky = { 70, 200, 220 }, good = { 110, 220, 150 };
#define PANEL_X 600.0f               /* 오른쪽 글자들의 왼쪽 끝(화면을 960으로 본 단위) */

static void text(image_t *im, float u, float x, float y, const char *s, rgb_t c, float k, float a)
{
    hd_text(im, x * u, y * u, s, c, 12.0f * k * u, a);
}

/* 가운데 그림(유리 돔)과 돔 아래의 한 줄을 그린다. 화면을 960으로 본 단위의 픽셀 수를 돌려준다. 그리지 못했으면 0 */
static float art_scene(image_t *im, double t)
{
    static const char ramp[] = " .':-~=+*xo%#8@";
    static const float confetti[5][3] = { { 236, 238, 244 }, { 250, 215, 70 }, { 245, 130, 180 }, { 70, 200, 220 }, { 150, 230, 190 } };
    static float gw_of[sizeof ramp], cell_of;
    const int levels = (int)sizeof ramp - 1;
    if (!im || im->w < 400) return 0.0f;
    GW = (float)im->w / COLS;
    GH = (float)im->h / ROWS;
    float W = (float)im->w, H = (float)im->h, u = W / 960.0f;
    float gx = W * 0.31f, gy = H * 0.43f, R = H * 0.33f;                /* 돔의 가운데와 반지름 */
    float floor_y = gy + 0.62f * R, spin = (float)t * 0.5f;

    hd_fill(im, hd_rgb(5, 7, 14));
    for (int k = 0; k < 6; k++)                                         /* 돔 뒤의 옅은 빛 */
        hd_ellipse(im, gx, gy, R * (1.9f - 0.2f * (float)k), R * (1.7f - 0.18f * (float)k), hd_rgb(40, 60, 120), 0.035f);
    for (int r = 0; r < ROWS; r++)
        for (int c = 0; c < COLS; c++) grid[r][c] = (acell_t){ 0, 0, 0, 0, FAR };

    /* 받침: 위가 좁은 둥근 통. 가운데에 금빛 띠 */
    for (int r = (int)((gy + 0.80f * R) / GH); r <= (int)((gy + 1.44f * R) / GH); r++)
        for (int c = (int)((gx - 0.9f * R) / GW); c <= (int)((gx + 0.9f * R) / GW); c++) {
            float x = ((float)c + 0.5f) * GW - gx, y = ((float)r + 0.5f) * GH - gy, v = (y - 0.86f * R) / (0.5f * R);
            float half = (0.60f + 0.20f * clamp01(v)) * R, nx = x / half;
            float top_e = 0.86f * R - 0.07f * R * sqrtf(clamp01(1.0f - nx * nx));       /* 윗면의 둥근 앞 가장자리 */
            float bot_e = 1.36f * R + 0.08f * R * sqrtf(clamp01(1.0f - nx * nx));
            if (fabsf(nx) >= 1.0f || y < top_e || y > bot_e) continue;
            float lum = 0.30f + 0.62f * powf(1.0f - fabsf(nx + 0.25f) / 1.25f, 1.6f);
            int band = v > 0.36f && v < 0.52f;
            if (band) put(c, r, -R, 250, 205, 70, clamp01(lum + 0.25f));
            else put(c, r, -R, 92, 84, 206, lum);
        }

    /* 돔 안의 바닥: 색종이가 쌓인 둔덕 */
    for (int r = (int)((floor_y - 0.2f * R) / GH); r <= (int)((gy + 0.86f * R) / GH); r++)
        for (int c = (int)((gx - R) / GW); c <= (int)((gx + R) / GW); c++) {
            float x = ((float)c + 0.5f) * GW - gx, y = ((float)r + 0.5f) * GH - gy;
            float hill = floor_y - gy - 0.07f * R * cosf(x / R * 2.2f) - 0.04f * R * hash2(c / 3, 7);
            if (x * x + y * y > 0.93f * 0.93f * R * R || y < hill) continue;
            float d = (y - hill) / (0.3f * R);
            int k = hash2(c, r) > 0.86f ? 1 + (int)(hash2(r, c) * 3.99f) : 0;
            put(c, r, -0.5f * R, confetti[k][0], confetti[k][1], confetti[k][2], 0.86f - 0.5f * clamp01(d));
        }

    draw_cat(gx - 0.06f * R, floor_y + 0.02f * R, t);

    /* 초파리: 시프봇의 머리 위를 돌고 위아래로 흔들린다. 가는 쪽을 본다 */
    float fa = (float)t * 0.85f, fr = 0.52f * R;
    draw_fly(gx + cosf(fa) * fr, gy - 0.50f * R + sinf((float)t * 2.3f) * 0.04f * R - sinf(fa) * fr * 0.22f, sinf(fa) * fr,
             fa + 1.5708f, 0.30f * R, t);

    /* 돔 안에 내리는 색종이. 돔과 함께 천천히 돈다 */
    for (int i = 0; i < 420; i++) {
        float a = hash2(i, 1) * 6.2832f + spin, rad = sqrtf(hash2(i, 2)) * 0.9f * R, span = 1.5f * R;
        float fall = fmodf(hash2(i, 3) * span + (float)t * (0.10f + 0.12f * hash2(i, 4)) * R, span);
        float x = cosf(a) * rad + sinf((float)t * 1.3f + (float)i) * 0.02f * R, z = sinf(a) * rad, y = -0.9f * R + fall;
        if (x * x + y * y + z * z > 0.9f * 0.9f * R * R || y > floor_y - gy) continue;
        int k = hash2(i, 5) > 0.6f ? 1 + (int)(hash2(i, 6) * 3.99f) : 0;
        put_px(gx + x, gy + y + 0.22f * z * 0.3f, z - 3.0f * R, confetti[k][0], confetti[k][1], confetti[k][2], 0.35f + 0.6f * hash2(i, 8));
    }

    /* 유리: 가장자리로 갈수록 밝고, 왼쪽 위에 빛이 맺힌다. 안의 것을 가리지 않고 밝기만 더한다 */
    for (int r = (int)((gy - R) / GH); r <= (int)((gy + R) / GH); r++)
        for (int c = (int)((gx - R) / GW); c <= (int)((gx + R) / GW); c++) {
            if (c < 0 || r < 0 || c >= COLS || r >= ROWS) continue;
            float x = ((float)c + 0.5f) * GW - gx, y = ((float)r + 0.5f) * GH - gy, d = sqrtf(x * x + y * y) / R;
            if (d > 1.0f) continue;
            /* 맺힌 빛: 왼쪽 위(각도 -135도 둘레)에서 가장자리를 따라 휜 띠 */
            float ang = atan2f(y, x) + 2.356f, arc = clamp01(1.0f - fabsf(ang) / 0.55f) * clamp01(1.0f - fabsf(d - 0.78f) / 0.07f);
            float lum = 0.75f * smooth(0.90f, 1.0f, d) + 0.10f * smooth(0.6f, 0.95f, d) * hash2(c, r) + 0.7f * arc;
            acell_t *g = &grid[r][c];
            if (lum < 0.06f || lum < g->lum * 0.8f) continue;
            if (g->lum > 0.0f && d < 0.9f) { g->lum = clamp01(g->lum + lum * 0.5f); continue; }
            *g = (acell_t){ 214, 226, 244, lum, g->z };
        }

    /* 돔 밖에 흩날리는 것들 */
    for (int i = 0; i < 150; i++) {
        float x = fmodf(hash2(i, 11) * W + sinf((float)t * 0.4f + (float)i) * 12.0f * u + W, W);
        float y = fmodf(hash2(i, 12) * H + (float)t * (6.0f + 16.0f * hash2(i, 13)) * u, H);
        int c = (int)(x / GW), r = (int)(y / GH), k = hash2(i, 14) > 0.75f ? 1 + (int)(hash2(i, 15) * 3.99f) : 0;
        if (c < 0 || r < 0 || c >= COLS || r >= ROWS || grid[r][c].lum > 0.0f) continue;
        grid[r][c] = (acell_t){ confetti[k][0], confetti[k][1], confetti[k][2], 0.12f + 0.3f * hash2(i, 16), FAR };
    }

    /* 칸마다 밝기에 맞는 글자를 찍는다. 글자가 조금씩 바뀌어 살아 있는 것처럼 보인다 */
    float cell = GH * 1.2f;
    if (cell != cell_of) {
        for (int i = 0; i < levels; i++) { const char s[2] = { ramp[i], '\0' }; gw_of[i] = hd_text_w(s, cell); }
        cell_of = cell;
    }
    int beat = (int)(t * 6.0);
    for (int r = 0; r < ROWS; r++)
        for (int c = 0; c < COLS; c++) {
            const acell_t *g = &grid[r][c];
            if (g->lum < 0.04f) continue;
            int i = (int)(g->lum * (float)(levels - 1) + 0.5f + (hash2(c * 31 + beat, r) - 0.5f) * 1.6f);
            i = i < 1 ? 1 : (i >= levels ? levels - 1 : i);
            const char s[2] = { ramp[i], '\0' };
            float k = 0.45f + 0.55f * g->lum;
            hd_text(im, ((float)c + 0.5f) * GW - gw_of[i] * 0.5f, ((float)r + 0.5f) * GH - cell * 7.0f / 12.0f, s,
                    hd_rgb((int)(g->r * k), (int)(g->g * k), (int)(g->b * k)), cell, 1.0f);
        }

    /* 돔 아래의 한 줄: 터미널에서 돌리는 프로그램이라는 표시 */
    float bx = gx / u - 150.0f, by = H / u - 34.0f;
    text(im, u, bx, by, "$ ./client", ink, 0.8f, 0.9f);
    if (((int)(t * 2.0) & 1) == 0) hd_rect(im, (bx + 54.0f) * u, (by + 2.0f) * u, 4.0f * u, 8.0f * u, ink, 0.9f);
    hd_ellipse(im, (bx + 232.0f) * u, (by + 6.0f) * u, 2.4f * u, 2.4f * u, good, 1.0f);
    text(im, u, bx + 239.0f, by, "running", dim, 0.8f, 1.0f);

    return u;
}

/* 오른쪽 위의 제목과 꼬리표. 그 아래 첫 줄의 높이를 돌려준다 */
static float art_head(image_t *im, float u, const char *tag)
{
    const float x = PANEL_X, y = 96.0f, tw = (float)ui_text_w(tag) * 0.75f + 14.0f;
    text(im, u, x, y, "슈퍼 시프 파티", key, 1.9f, 1.0f);
    hd_rrect(im, x * u, (y + 34.0f) * u, tw * u, 14.0f * u, 7.0f * u, hd_rgb(84, 132, 240), 1.0f);
    text(im, u, x + 7.0f, y + 36.5f, tag, hd_rgb(255, 255, 255), 0.75f, 1.0f);
    text(im, u, x + tw + 8.0f, y + 36.5f, "초파리의 뇌와 겨루는 파티", dim, 0.75f, 1.0f);
    return y + 76.0f;
}

/* 역대 순위를 max줄까지 적는다 */
static void art_top(image_t *im, float u, float y, const char *const top[], int n_top, int max)
{
    const float x = PANEL_X;
    text(im, u, x, y, "역대 순위", dim, 0.75f, 1.0f);
    hd_rect(im, x * u, (y + 13.0f) * u, 210.0f * u, 0.6f * u, hd_rgb(60, 64, 84), 1.0f);
    for (int i = 0; i < n_top && i < max; i++) text(im, u, x, y + 18.0f + 14.0f * (float)i, top[i], i == 0 ? ink : dim, 0.75f, 1.0f);
}

/* 로비: 참가자, 안내, 역대 순위. 글자는 작게 */
void art_lobby(image_t *im, const art_seat_t *seat, int n_seats, const char *const top[], int n_top, double t)
{
    float u = art_scene(im, t);
    if (u <= 0.0f) return;
    float x = PANEL_X, y = art_head(im, u, "LOBBY");
    int ready = 0, me_host = 0;
    char line[64];
    for (int i = 0; i < n_seats; i++) {
        const art_seat_t *s = &seat[i];
        rgb_t c = s->me ? sky : ink;
        ready += s->ready;
        me_host |= s->me && s->host;
        snprintf(line, sizeof line, "%d", i + 1);
        text(im, u, x, y, line, dim, 0.9f, 1.0f);
        text(im, u, x + 16.0f, y, s->name ? s->name : "", c, 0.9f, 1.0f);
        if (s->host) text(im, u, x + 122.0f, y, "방장", key, 0.75f, 1.0f);
        hd_ellipse(im, (x + 166.0f) * u, (y + 6.5f) * u, 3.0f * u, 3.0f * u, s->ready ? good : hd_rgb(70, 74, 92), 1.0f);
        text(im, u, x + 174.0f, y, s->ready ? "준비" : "대기", s->ready ? good : dim, 0.9f, 1.0f);
        y += 19.0f;
    }
    if (n_seats == 0) { text(im, u, x, y, "아직 아무도 없어요", dim, 0.9f, 1.0f); y += 19.0f; }
    y += 8.0f;
    snprintf(line, sizeof line, "참가 %d   준비 %d", n_seats, ready);
    text(im, u, x, y, line, dim, 0.75f, 1.0f);
    y += 22.0f;
    text(im, u, x, y, "SPACE", key, 0.8f, 1.0f);
    text(im, u, x + 34.0f, y, "준비", ink, 0.8f, 1.0f);
    text(im, u, x + 70.0f, y, "F", key, 0.8f, 1.0f);
    text(im, u, x + 80.0f, y, me_host ? "시작" : "시작(방장)", ink, 0.8f, 1.0f);
    text(im, u, x + 150.0f, y, "Q", key, 0.8f, 1.0f);
    text(im, u, x + 160.0f, y, "나가기", ink, 0.8f, 1.0f);
    if (me_host) {                                   /* 방장에게만: 미니게임을 건너뛰는 키 */
        text(im, u, x, y + 16.0f, "B", key, 0.75f, 0.8f);
        text(im, u, x + 10.0f, y + 16.0f, "미니게임 건너뛰고 보스전으로", dim, 0.75f, 1.0f);
        y += 16.0f;
    }
    if (n_top > 0) art_top(im, u, y + 36.0f, top, n_top, 6);
}

/* 입장 화면: 고를 것 셋. sel은 고른 줄이다(0 입장, 1 지난 랭킹, 2 나가기) */
void art_title(image_t *im, int sel, double t)
{
    static const char *const item[3] = { "입장", "지난 랭킹", "나가기" };
    float u = art_scene(im, t);
    if (u <= 0.0f) return;
    float x = PANEL_X, y = art_head(im, u, "WELCOME") + 4.0f;
    for (int i = 0; i < 3; i++, y += 26.0f) {
        const char num[2] = { (char)('1' + i), '\0' };
        if (i == sel) {                              /* 고른 줄: 옅은 바탕과 왼쪽의 노란 막대 */
            hd_rrect(im, (x - 10.0f) * u, (y - 5.0f) * u, 190.0f * u, 22.0f * u, 6.0f * u, hd_rgb(26, 32, 56), 1.0f);
            hd_rrect(im, (x - 10.0f) * u, (y - 5.0f) * u, 3.0f * u, 22.0f * u, 1.5f * u, key, 1.0f);
        }
        text(im, u, x, y, num, i == sel ? key : dim, 1.0f, 1.0f);
        text(im, u, x + 18.0f, y, item[i], i == sel ? ink : dim, 1.0f, 1.0f);
    }
    y += 10.0f;
    text(im, u, x, y, "1 2 3", key, 0.8f, 1.0f);
    text(im, u, x + 34.0f, y, "고르기", ink, 0.8f, 1.0f);
    text(im, u, x + 84.0f, y, "SPACE", key, 0.8f, 1.0f);
    text(im, u, x + 118.0f, y, "들어가기", ink, 0.8f, 1.0f);
    text(im, u, x, y + 34.0f, "어서 와요! 파티가 곧 시작돼요", sky, 0.85f, 1.0f);
}

/* 지난 랭킹: 순위를 적는다. ok가 0이면 서버에 닿지 못했다 */
void art_past(image_t *im, const char *const top[], int n_top, int ok, double t)
{
    float u = art_scene(im, t);
    if (u <= 0.0f) return;
    float x = PANEL_X, y = art_head(im, u, "RANKING");
    if (!ok) {
        text(im, u, x, y, "서버에 닿지 않아요", ink, 0.9f, 1.0f);
        text(im, u, x, y + 18.0f, "서버가 켜져 있는지 봐 주세요", dim, 0.8f, 1.0f);
    } else if (n_top == 0) {
        text(im, u, x, y, "아직 기록이 없어요", dim, 0.9f, 1.0f);
    } else {
        art_top(im, u, y, top, n_top, 10);
    }
    text(im, u, x, 372.0f, "아무 키나 누르면 돌아가요", sky, 0.8f, 1.0f);
}
