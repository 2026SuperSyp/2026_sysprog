/* 보스전 그리기(픽셀 그림). 화면을 Sixel 그림으로 보낼 때 쓴다. 글자 칸으로 보낼 때는 boss_view.c가 그린다.
 *
 * 그리는 것은 boss_view.c와 같다. 다른 점은 색과 해상도가 넉넉하다는 것이다. 그래서
 *   - 3D 화면이 그림 전체를 쓰고, 상태 표시는 그 위에 작게 띄운다(둠 같은 1인칭 게임의 배치).
 *   - 벽과 바닥에 무늬를 넣고(돌, 널빤지, 타일), 멀수록 어둠에 묻히게 한다. 무늬는 그림 파일이 아니라 식으로 만든다.
 *     돌과 나무와 타일에는 결(자리마다 정해진 무늬를 여러 크기로 겹친 것), 깨진 금, 모서리의 입체감(위와 왼쪽은
 *     밝게, 아래와 오른쪽은 어둡게)을 넣는다. 무늬는 벽과 바닥의 자리에 붙어 있어서 움직여도 따라 움직인다.
 *     화면 가운데가 조금 더 밝다(손전등처럼).
 *   - 바닥과 천장은 줄마다 "그 줄이 눈에서 얼마나 먼가"를 구해 픽셀이 가리키는 바닥의 자리를 찾는다(바닥 캐스팅).
 *   - 가장자리는 픽셀이 덮인 만큼만 칠한다. 벽의 위아래 끝, 초파리와 참가자의 윤곽, 상태 표시의 도형이 모두 그렇다.
 *   - 초파리는 타원과 선분 몇 개로 된 모양에 빛을 입힌다. 식으로 그리므로 가까이 가도 계단이 생기지 않는다.
 *   - 글자는 큰 글꼴로 그린다(ui/hd.c).
 * 상태 표시의 자리는 화면을 960x536으로 본 단위(U)로 적는다. 배율 10(2400x1340)에서 한 단위가 2.5픽셀이다.
 * 바닥의 줄들, 벽의 세로줄들, 초파리의 줄들은 서로 무관해서 여러 스레드에 나눠 그린다(par_for). */
#include "boss/boss_view.h"
#include "brain.h"
#include "ui/hd.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

#define PLANE       0.66f            /* 시야의 넓이(약 66도) */
#define TWO_PI      6.2831853f
#define UNIT_PER_W  (104.0f / 240.0f)    /* 거리 1에 있는 벽의 높이 = 화면 너비 x 이 값(boss_view.c와 같다) */
#define CELL        12.0f            /* 글자 한 칸의 크기(U) */
#define FOG_K       0.075f           /* 멀수록 어둠에 묻히는 빠르기 */

typedef struct { float r, g, b; } col_t;
typedef struct { float r, g, b, a; } acc_t;          /* 겹쳐 칠한 결과. 색에는 덮는 정도가 곱해져 있다 */

static const col_t FOG = { 12.0f, 15.0f, 27.0f };
static const rgb_t INK = { 235, 235, 225 }, DIM = { 150, 155, 170 }, KEY = { 250, 220, 70 }, SKY = { 70, 200, 220 };
static const rgb_t PCOL[BOSS_PLAYERS] = { { 70, 200, 220 }, { 60, 180, 75 }, { 245, 130, 180 } };

static uint8_t wall[BOSS_MAP][BOSS_MAP];
static int     have_map;
static float   zbuf[SCREEN_W * HD_MAX_SCALE];
/* 지금 그리는 그림에서: 상태 표시의 한 단위의 픽셀 수(U), 무기의 한 단위의 픽셀 수(G), 3D 화면의 위쪽 끝과 높이,
 * 지평선의 높이, 거리 1에 있는 벽의 높이 */
static float   U, G;
static int     VY, VH, VW;
static float   MID, UNIT;

static void load_map(void)
{
    static boss_state_t tmp;
    boss_cfg_t c;
    boss_cfg_default(&c);
    boss_init(&tmp, &c, 0, 1, NULL);
    for (int y = 0; y < BOSS_MAP; y++)
        for (int x = 0; x < BOSS_MAP; x++) wall[y][x] = (uint8_t)boss_solid(&tmp, x, y);
    have_map = 1;
}

static int solid(int cx, int cy)
{
    return cx < 0 || cy < 0 || cx >= BOSS_MAP || cy >= BOSS_MAP || wall[cy][cx];
}

static inline float clamp01(float v) { return v < 0.0f ? 0.0f : (v > 1.0f ? 1.0f : v); }
static inline float minf(float a, float b) { return a < b ? a : b; }
static inline float maxf(float a, float b) { return a > b ? a : b; }
static inline float smooth(float a, float b, float v) { float t = clamp01((v - a) / (b - a)); return t * t * (3.0f - 2.0f * t); }
static inline col_t cmix(col_t a, col_t b, float t) { return (col_t){ a.r + (b.r - a.r) * t, a.g + (b.g - a.g) * t, a.b + (b.b - a.b) * t }; }
static inline col_t cmul(col_t a, float k) { return (col_t){ a.r * k, a.g * k, a.b * k }; }
static inline uint8_t to8(float v) { return (uint8_t)(v < 0.0f ? 0.0f : (v > 255.0f ? 255.0f : v)); }

/* 두 수를 고르게 섞은 0~1의 값. 돌과 널빤지마다 밝기를 조금씩 다르게 하는 데 쓴다 */
static float hash2(int a, int b)
{
    uint32_t h = (uint32_t)a * 374761393u + (uint32_t)b * 668265263u;
    h = (h ^ (h >> 13)) * 1274126177u;
    return (float)((h ^ (h >> 16)) & 0xffffu) / 65535.0f;
}

/* 부드럽게 이어지는 무늬(0~1). 정수 자리마다 정해진 값을 두고 그 사이를 매끄럽게 잇는다 */
static float noise(float x, float y, int seed)
{
    float gx = floorf(x), gy = floorf(y), fx = x - gx, fy = y - gy;
    int ix = (int)gx, iy = (int)gy;
    fx = fx * fx * (3.0f - 2.0f * fx);
    fy = fy * fy * (3.0f - 2.0f * fy);
    float a = hash2(ix + seed * 57, iy), b = hash2(ix + 1 + seed * 57, iy);
    float c = hash2(ix + seed * 57, iy + 1), d = hash2(ix + 1 + seed * 57, iy + 1);
    return (a + (b - a) * fx) * (1.0f - fy) + (c + (d - c) * fx) * fy;
}

/* 결: 같은 무늬를 크기를 줄여 가며 겹친다(0~1). 큰 얼룩 위에 잔 알갱이가 얹힌다 */
static float grain(float x, float y, int seed)
{
    return (noise(x, y, seed) * 0.5f + noise(x * 2.3f, y * 2.3f, seed + 1) * 0.3f + noise(x * 5.1f, y * 5.1f, seed + 2) * 0.2f);
}

/* 화면 가운데가 더 밝다(손전등). x는 화면의 가로 자리 */
static inline float lamp(int x) { float r = ((float)x / (float)VW - 0.5f) * 2.0f; return 1.12f - 0.34f * r * r; }

static inline void px_set(image_t *im, int x, int y, col_t c)
{
    uint8_t *p = im->px + ((size_t)y * (size_t)im->w + (size_t)x) * 3;
    p[0] = to8(c.r); p[1] = to8(c.g); p[2] = to8(c.b);
}

static inline void px_mix(image_t *im, int x, int y, col_t c, float a)
{
    if (a <= 0.0f) return;
    uint8_t *p = im->px + ((size_t)y * (size_t)im->w + (size_t)x) * 3;
    if (a >= 1.0f) { p[0] = to8(c.r); p[1] = to8(c.g); p[2] = to8(c.b); return; }
    p[0] = to8((float)p[0] + (c.r - (float)p[0]) * a);
    p[1] = to8((float)p[1] + (c.g - (float)p[1]) * a);
    p[2] = to8((float)p[2] + (c.b - (float)p[2]) * a);
}

/* 겹쳐 칠한 것(acc)을 픽셀에 얹는다 */
static inline void px_over(image_t *im, int x, int y, const acc_t *o)
{
    if (o->a <= 0.003f) return;
    uint8_t *p = im->px + ((size_t)y * (size_t)im->w + (size_t)x) * 3;
    float k = 1.0f - o->a;
    p[0] = to8((float)p[0] * k + o->r); p[1] = to8((float)p[1] * k + o->g); p[2] = to8((float)p[2] * k + o->b);
}

/* c색을 cov만큼 위에 덧칠한다 */
static inline void over(acc_t *o, col_t c, float cov)
{
    if (cov <= 0.0f) return;
    if (cov > 1.0f) cov = 1.0f;
    o->r += (c.r - o->r) * cov; o->g += (c.g - o->g) * cov; o->b += (c.b - o->b) * cov;
    o->a += (1.0f - o->a) * cov;
}

/* ---- 바닥과 천장 -------------------------------------------------------------------- */
/* 화면의 줄 하나는 바닥(또는 천장)에서 눈으로부터 같은 거리에 있는 곳들이다. 지평선에서 p픽셀 떨어진 줄은
 * UNIT / 2 / p 만큼 멀다(눈높이가 벽 높이의 절반). 그 줄의 왼쪽 끝과 오른쪽 끝이 가리키는 자리를 구해 고르게 나누면
 * 픽셀마다 바닥의 자리가 나온다. 바닥은 한 칸짜리 돌판이고 천장은 두 칸짜리 판에 들보가 지나간다.
 * 돌판마다 빛깔이 조금 다르고 결과 금이 있다. 가장자리는 위와 왼쪽이 밝고 아래와 오른쪽이 어두워 도드라져 보인다.
 * 멀리서는 무늬가 픽셀보다 잘아져서 어른거리므로 무늬를 흐리게 한다. */
typedef struct { image_t *im; float px, py, dx, dy; } view_job_t;

static void ground_rows(int from, int to, void *arg)
{
    static const col_t slab = { 86, 104, 124 }, slab2 = { 70, 86, 108 }, grout = { 24, 32, 46 };
    static const col_t panel = { 50, 54, 78 }, beam = { 74, 54, 40 }, gap = { 20, 22, 34 };
    const view_job_t *j = arg;
    image_t *im = j->im;
    float px = j->px, py = j->py, plx = -j->dy * PLANE, ply = j->dx * PLANE;
    float lx = j->dx - plx, ly = j->dy - ply, rx = j->dx + plx, ry = j->dy + ply;
    for (int y = VY + from; y < VY + to; y++) {
        float p = (float)y + 0.5f - MID, ap = fabsf(p) < 0.5f ? 0.5f : fabsf(p);
        float dist = 0.5f * UNIT / ap, ppu = UNIT / dist;
        float sx = dist * (rx - lx) / (float)VW, sy = dist * (ry - ly) / (float)VW;
        float wx = px + dist * lx + sx * 0.5f, wy = py + dist * ly + sy * 0.5f;
        float vis = expf(-dist * FOG_K), detail = clamp01((ppu - 4.0f) / 22.0f);
        int floor_row = p > 0.0f;
        uint8_t *o = im->px + (size_t)y * (size_t)im->w * 3;
        for (int x = 0; x < VW; x++, wx += sx, wy += sy, o += 3) {
            col_t c;
            if (floor_row) {
                /* 돌판의 줄은 칸의 경계가 아니라 칸의 가운데를 지난다. 참가자는 칸의 경계에 서서 시작하는데, 줄이 바로
                 * 발밑으로 지나가면 화면 가운데에 굵은 쐐기로 보인다 */
                float tx = wx + 0.5f, ty = wy + 0.5f, gx = floorf(tx), gy = floorf(ty), fx = tx - gx, fy = ty - gy;
                int ix = (int)gx, iy = (int)gy;
                float tone = 0.84f + 0.3f * hash2(ix * 7 + 3, iy * 13 + 1);
                float g = grain(wx * 7.0f, wy * 7.0f, 3);
                c = cmul(((ix + iy) & 1) ? slab : slab2, tone * (1.0f + (g - 0.5f) * 0.55f * detail));
                /* 금: 돌판 넷에 하나꼴로, 구불구불한 선 */
                if (hash2(ix + 91, iy + 17) > 0.74f) {
                    float bend = fx + (noise(fy * 5.0f + (float)ix, (float)iy, 9) - 0.5f) * 0.3f - 0.5f;
                    c = cmul(c, 1.0f - 0.5f * detail * clamp01(0.5f + (0.012f - fabsf(bend)) * ppu));
                }
                /* 가장자리의 입체감과 줄눈 */
                float e = minf(minf(fx, 1.0f - fx), minf(fy, 1.0f - fy));
                float lit_edge = clamp01(0.5f + (0.05f - minf(fx, fy)) * ppu * 0.5f) * detail;
                float dark_edge = clamp01(0.5f + (0.05f - minf(1.0f - fx, 1.0f - fy)) * ppu * 0.5f) * detail;
                c = cmul(c, 1.0f + 0.2f * lit_edge - 0.26f * dark_edge);
                c = cmix(c, grout, clamp01(0.5f + (0.014f - e) * ppu) * (0.4f + 0.6f * detail));
            } else {
                float hx = (wx + 1.0f) * 0.5f, hy = (wy + 1.0f) * 0.5f, fx = hx - floorf(hx), fy = hy - floorf(hy);
                float g = grain(wx * 5.0f, wy * 5.0f, 21);
                c = cmul(panel, 1.0f + (g - 0.5f) * 0.4f * detail);
                float e = minf(minf(fx, 1.0f - fx), minf(fy, 1.0f - fy)) * 2.0f;
                c = cmix(c, gap, clamp01(0.5f + (0.02f - e) * ppu) * (0.4f + 0.6f * detail));
                /* 들보: 네 칸마다 한 줄, 나무결 */
                float bx = (wy + 1.0f) * 0.25f, fb = bx - floorf(bx), be = fabsf(fb - 0.5f) * 4.0f;
                float on = clamp01(0.5f + (0.16f - be) * ppu);
                if (on > 0.0f) {
                    float wood = grain(wx * 2.0f, wy * 26.0f, 33);
                    c = cmix(c, cmul(beam, (0.8f + 0.5f * wood) * (1.0f - 0.35f * clamp01(be / 0.16f))), on);
                }
            }
            c = cmix(FOG, cmul(c, lamp(x)), vis);
            o[0] = to8(c.r); o[1] = to8(c.g); o[2] = to8(c.b);
        }
    }
}

/* ---- 벽 ---------------------------------------------------------------------------- */
/* 벽의 한 점을 정하는 데 필요한 것: 칸 안에서의 가로 자리 at(0~1), 벽이 놓인 칸과 면, 한 칸이 화면에서 몇 픽셀인가(ppu) */
typedef struct { float at, ppu, px, py; int mx, my, side, face; } hit_t;     /* face: 벽면의 고정된 좌표(x 또는 y) */

/* 바깥 벽: 세 줄로 쌓은 돌이고 줄마다 반 칸씩 어긋난다. v는 위에서 아래로(0~1), u는 벽을 따라 잰 자리(칸).
 * 돌마다 빛깔이 다르고, 결과 얼룩과 금이 있고, 가장자리가 깎여 도드라져 보인다. 아래쪽은 습기로 어둡다 */
static col_t stone(const hit_t *h, float v, float u)
{
    static const col_t base = { 158, 164, 182 }, warm = { 172, 160, 150 }, mortar = { 58, 62, 78 }, moss = { 70, 92, 74 };
    float vr = v * 3.0f, ppu = h->ppu;
    int row = vr >= 3.0f ? 2 : (int)vr;
    float uu = u * 2.0f + ((row & 1) ? 0.5f : 0.0f), block = floorf(uu), fu = uu - block, fv = vr - (float)row;
    int id = (int)block * 3 + row + h->face * 131 + h->side * 17;
    float t = hash2(id, 5), g = grain(u * 9.0f, v * 9.0f + (float)h->face, 7);
    col_t c = cmul(cmix(base, warm, hash2(id, 9) * 0.6f), (0.8f + 0.34f * t) * (1.0f + (g - 0.5f) * 0.6f));
    if (hash2(id, 23) > 0.7f) {                      /* 금 */
        float bend = fu + (noise(fv * 4.0f, (float)id, 11) - 0.5f) * 0.35f - 0.3f - 0.4f * hash2(id, 31);
        c = cmul(c, 1.0f - 0.55f * clamp01(0.5f + (0.008f - fabsf(bend) * 0.5f) * ppu));
    }
    /* 가장자리: 위와 왼쪽은 빛을 받고 아래와 오른쪽은 그늘진다. 칸 단위의 거리로 잰다 */
    float el = fu * 0.5f, er = (1.0f - fu) * 0.5f, et = fv / 3.0f, eb = (1.0f - fv) / 3.0f;
    c = cmul(c, 1.0f + 0.24f * clamp01(0.5f + (0.03f - minf(el, et)) * ppu * 0.6f)
                     - 0.3f * clamp01(0.5f + (0.03f - minf(er, eb)) * ppu * 0.6f));
    c = cmix(c, moss, smooth(0.62f, 1.0f, v + (g - 0.5f) * 0.5f) * 0.38f);
    c = cmul(c, 1.0f - 0.3f * smooth(0.8f, 1.0f, v) - 0.14f * smooth(0.15f, 0.0f, v));          /* 바닥과 천장에 닿는 곳의 그늘 */
    return cmix(c, mortar, clamp01(0.5f + (0.016f - minf(minf(el, er), minf(et, eb))) * ppu));
}

/* 기둥: 세로 널빤지 넷. 나무결이 세로로 흐르고 옹이가 있다. 위아래에 쇠띠를 두르고 못을 박았다 */
static col_t plank(const hit_t *h, float v, float u)
{
    static const col_t wood = { 196, 136, 84 }, dark = { 96, 60, 36 }, iron = { 96, 102, 116 }, rivet = { 196, 200, 210 };
    float uu = h->at * 4.0f, board = floorf(uu), fu = uu - board, ppu = h->ppu;
    if (board > 3.0f) board = 3.0f;
    int id = (int)board + h->mx * 31 + h->my * 17 + h->side * 7;
    float g = grain(u * 30.0f + (float)id, v * 2.2f, 41), ring = noise(u * 9.0f + (float)id * 3.0f, v * 1.2f, 43);
    col_t c = cmul(cmix(wood, dark, ring * 0.45f), (0.84f + 0.3f * hash2(id, 3)) * (0.78f + 0.5f * g));
    /* 옹이: 널빤지 셋에 하나꼴 */
    if (hash2(id, 13) > 0.62f) {
        float ku = fu - 0.3f - 0.4f * hash2(id, 14), kv = (v - 0.25f - 0.5f * hash2(id, 15)) * 3.0f, r = sqrtf(ku * ku + kv * kv);
        c = cmix(c, dark, clamp01(1.0f - r * 7.0f) * 0.8f);
    }
    float e = minf(fu, 1.0f - fu) * 0.25f;
    c = cmul(c, 1.0f + 0.16f * clamp01(0.5f + (0.012f - fu * 0.25f) * ppu) - 0.2f * clamp01(0.5f + (0.012f - (1.0f - fu) * 0.25f) * ppu));
    c = cmix(c, cmul(dark, 0.5f), clamp01(0.5f + (0.007f - e) * ppu));
    /* 쇠띠와 못 */
    float bt = fabsf(v - 0.09f), bb = fabsf(v - 0.91f), band = minf(bt, bb);
    float on = clamp01(0.5f + (0.045f - band) * ppu);
    if (on > 0.0f) {
        float sheen = 0.8f + 0.5f * clamp01(1.0f - band / 0.045f) * (bt < bb ? 1.0f : 0.7f);
        col_t m = cmul(iron, sheen * (0.86f + 0.3f * noise(u * 40.0f, v * 40.0f, 51)));
        float ru = fu - 0.5f, rv = (bt < bb ? v - 0.09f : v - 0.91f), rr = sqrtf(ru * ru * 0.0625f + rv * rv);
        m = cmix(m, rivet, clamp01(0.5f + (0.014f - rr) * ppu));
        c = cmix(c, m, on);
    }
    return cmul(c, 1.0f - 0.26f * smooth(0.84f, 1.0f, v));
}

static void wall_columns(int from, int to, void *arg)
{
    const view_job_t *j = arg;
    image_t *im = j->im;
    float px = j->px, py = j->py, dx = j->dx, dy = j->dy, plx = -dy * PLANE, ply = dx * PLANE;
    for (int x = from; x < to; x++) {
        float cam = 2.0f * ((float)x + 0.5f) / (float)VW - 1.0f;
        float rx = dx + plx * cam, ry = dy + ply * cam;
        int mx = (int)px, my = (int)py, sx, sy, side = 0;
        float ddx = fabsf(rx) < 1e-6f ? 1e6f : fabsf(1.0f / rx), ddy = fabsf(ry) < 1e-6f ? 1e6f : fabsf(1.0f / ry);
        float sdx, sdy;
        if (rx < 0) { sx = -1; sdx = (px - (float)mx) * ddx; } else { sx = 1; sdx = ((float)mx + 1.0f - px) * ddx; }
        if (ry < 0) { sy = -1; sdy = (py - (float)my) * ddy; } else { sy = 1; sdy = ((float)my + 1.0f - py) * ddy; }
        for (int k = 0; k < 2 * BOSS_MAP; k++) {
            if (sdx < sdy) { sdx += ddx; mx += sx; side = 0; }
            else           { sdy += ddy; my += sy; side = 1; }
            if (solid(mx, my)) break;
        }
        float d = side == 0 ? sdx - ddx : sdy - ddy;
        if (d < 0.05f) d = 0.05f;
        zbuf[x] = d;
        float at = side == 0 ? py + d * ry : px + d * rx;
        at -= floorf(at);
        float half = 0.5f * UNIT / d, yt = MID - half, yb = MID + half;
        int y0 = (int)floorf(yt), y1 = (int)ceilf(yb);
        if (y0 < VY) y0 = VY;
        if (y1 > VY + VH) y1 = VY + VH;
        int pillar = mx > 0 && my > 0 && mx < BOSS_MAP - 1 && my < BOSS_MAP - 1;
        /* 벽면의 좌표: 광선이 +쪽으로 가다 닿았으면 그 칸의 앞면(mx), -쪽이면 뒷면(mx + 1) */
        hit_t h = { at, UNIT / d, px, py, mx, my, side, side == 0 ? (sx > 0 ? mx : mx + 1) : (sy > 0 ? my : my + 1) };
        float along_u = (float)(side == 0 ? my : mx) + at;          /* 벽을 따라 잰 자리(칸) */
        float vis = expf(-d * FOG_K), shade = (side ? 0.72f : 1.0f) * lamp(x);
        for (int y = y0; y < y1; y++) {
            float v = clamp01(((float)y + 0.5f - yt) / (yb - yt));
            float cov = minf(minf((float)(y + 1) - yt, yb - (float)y), 1.0f);     /* 위아래 끝의 픽셀은 덮인 만큼만 */
            col_t c = pillar ? plank(&h, v, along_u) : stone(&h, v, along_u);
            px_mix(im, x, y, cmix(FOG, cmul(c, shade), vis), cov);
        }
    }
}

/* ---- 모양 --------------------------------------------------------------------------- */
/* 타원이 그 점을 덮는 정도. (u, v)와 타원은 같은 단위이고 pp는 그 단위 하나의 픽셀 수다 */
static inline float in_ell(float u, float v, float cx, float cy, float rx, float ry, float pp)
{
    float nx = (u - cx) / rx, ny = (v - cy) / ry, f2 = nx * nx + ny * ny;
    if (f2 > 4.0f) return 0.0f;
    if (f2 < 0.04f) return 1.0f;
    float f = sqrtf(f2), g = sqrtf(nx * nx / (rx * rx) + ny * ny / (ry * ry)) / f;
    return clamp01(0.5f - (f - 1.0f) / g * pp);
}

/* 굵기 2r인 선분이 그 점을 덮는 정도 */
static inline float in_seg(float u, float v, float ax, float ay, float bx, float by, float r, float pp)
{
    float vx = bx - ax, vy = by - ay, wx = u - ax, wy = v - ay, len2 = vx * vx + vy * vy;
    float t = len2 > 1e-9f ? clamp01((wx * vx + wy * vy) / len2) : 0.0f;
    float ex = wx - vx * t, ey = wy - vy * t;
    return clamp01(0.5f - (sqrtf(ex * ex + ey * ey) - r) * pp);
}

/* 둥근 덩어리에 왼쪽 위 앞에서 빛을 비춘다. nx, ny는 덩어리의 가운데에서 가장자리까지를 -1~1로 본 자리다.
 * 밝기를 돌려주고, 번쩍이는 점의 세기를 spec에 준다 */
static inline float lit(float nx, float ny, float *spec)
{
    float nz2 = 1.0f - nx * nx - ny * ny, nz = nz2 > 0.0f ? sqrtf(nz2) : 0.0f;
    float diff = -0.42f * nx - 0.55f * ny + 0.72f * nz, h = -0.2265f * nx - 0.2967f * ny + 0.9277f * nz;
    if (spec) { h = h > 0.0f ? h * h : 0.0f; h *= h; h *= h; h *= h; *spec = h * h; }     /* 32제곱 */
    return 0.40f + 0.74f * (diff > 0.0f ? diff : 0.0f);
}

/* ---- 초파리 -------------------------------------------------------------------------- */
typedef struct {
    int stun, air, tick;
    float charge;                /* 돌진하려 할 때 0~1로 깜빡인다 */
    float flash;                 /* 방금 맞았으면 0~1 */
} fly_look_t;

/* 날개 하나. (du, dv)는 날개 뿌리에서 잰 자리, (c, s)는 날개가 뻗는 방향이다. 길이의 반이 rx, 폭의 반이 ry다 */
static void wing(acc_t *o, float du, float dv, float c, float s, float rx, float ry, float alpha, float pp)
{
    static const col_t glass = { 204, 228, 242 }, vein = { 120, 150, 172 };
    float lu = du * c + dv * s - rx * 0.85f, lv = -du * s + dv * c, cov = in_ell(lu, lv, 0.0f, 0.0f, rx, ry, pp);
    if (cov <= 0.0f) return;
    float f = sqrtf(lu * lu / (rx * rx) + lv * lv / (ry * ry));
    float line = maxf(clamp01(0.5f + (0.012f - fabsf(lv)) * pp), clamp01(0.5f + (0.01f - fabsf(fabsf(lv) - ry * 0.5f - lu * 0.1f)) * pp));
    over(o, cmix(glass, vein, maxf(line * 0.8f, smooth(0.8f, 1.0f, f) * 0.7f)), cov * (alpha + 0.25f * smooth(0.75f, 1.0f, f)));
}

/* 초파리의 한 점. u, v는 -1~1(가운데가 0, v는 아래가 +), pp는 u가 1 변할 때의 픽셀 수다.
 * face: 0 정면(눈 둘), 1 뒤(날개가 등을 덮음), 2 옆(turn이 -1이면 왼쪽을 본다) */
static acc_t fly_point(float u, float v, float pp, int face, int turn, const fly_look_t *L)
{
    static const col_t leg = { 30, 26, 26 }, amber = { 164, 120, 76 }, dark = { 44, 34, 30 }, thorax = { 116, 84, 58 };
    static const col_t head = { 132, 94, 58 }, eye = { 200, 34, 38 }, hot = { 255, 70, 50 }, goo = { 240, 140, 40 };
    static const col_t grey = { 122, 122, 128 }, white = { 255, 255, 255 };
    acc_t o = { 0, 0, 0, 0 };
    float au = fabsf(u), spec, wig = 0.03f * sinf((float)L->tick * 0.9f);
    float air = L->air ? 1.0f : 0.0f, flap = L->air ? ((L->tick & 1) ? 0.25f : -0.2f) : 0.0f;

    if (L->charge > 0.0f) {                              /* 돌진하려 한다: 붉은 기운 */
        float r = sqrtf(u * u + v * v);
        if (r < 1.0f) over(&o, hot, (1.0f - r) * 0.45f * L->charge);
    }
    #define TONE(c) cmix(cmix((c), grey, L->stun ? 0.6f : 0.0f), hot, L->charge * 0.4f)

    if (face == 2) {                                     /* 옆모습: 머리가 turn 쪽에 있다 */
        float uu = u * (float)turn;
        if (v > 0.2f)                                    /* 다리 셋 */
            for (int i = 0; i < 3; i++) {
                float ax = 0.22f - 0.24f * (float)i, w = (i & 1) ? -wig : wig;
                over(&o, leg, maxf(in_seg(uu, v, ax, 0.3f, ax + 0.08f, 0.55f, 0.03f, pp),
                                   in_seg(uu, v, ax + 0.08f, 0.55f, ax + 0.2f + w, 0.86f, 0.026f, pp)));
            }
        float cov = in_ell(uu, v, -0.12f, 0.12f, 0.58f, 0.3f, pp);
        if (cov > 0.0f) {                                /* 몸: 앞쪽이 가슴, 뒤쪽이 줄무늬 배 */
            float nx = (uu + 0.12f) / 0.58f, ny = (v - 0.12f) / 0.3f, light = lit(nx, ny, &spec);
            float bandv = smooth(0.55f, 0.85f, 0.5f - 0.5f * cosf((uu + 0.7f) * 24.0f));
            col_t c = cmix(cmix(amber, dark, bandv), thorax, smooth(-0.05f, 0.1f, uu));
            c = cmul(TONE(c), light);
            c.r += spec * 70.0f; c.g += spec * 70.0f; c.b += spec * 70.0f;
            over(&o, c, cov);
        }
        float tilt = 0.2f + 0.5f * air + flap, c = cosf(tilt), s = sinf(tilt);
        wing(&o, uu - 0.08f, v + 0.1f + 0.05f * air, -c, -s, 0.42f + 0.06f * air, 0.15f, 0.5f - 0.15f * air, pp);
        cov = in_ell(uu, v, 0.56f, -0.08f, 0.25f, 0.25f, pp);
        if (cov > 0.0f) {
            float light = lit((uu - 0.56f) / 0.25f, (v + 0.08f) / 0.25f, NULL);
            over(&o, cmul(TONE(head), light), cov);
        }
        cov = in_ell(uu, v, 0.62f, -0.13f, 0.15f, 0.19f, pp);
        if (cov > 0.0f) {
            float light = lit((uu - 0.62f) / 0.15f, (v + 0.13f) / 0.19f, &spec);
            col_t c = cmul(cmix(eye, hot, L->charge), light * (pp > 70.0f ? 0.92f + 0.08f * cosf(u * 80.0f) * cosf(v * 80.0f) : 1.0f));
            c.r += spec * 150.0f; c.g += spec * 150.0f; c.b += spec * 150.0f;
            over(&o, c, cov);
        }
        over(&o, dark, in_seg(uu, v, 0.66f, 0.1f, 0.7f, 0.27f, 0.03f, pp));          /* 입 */
        over(&o, dark, in_seg(uu, v, 0.74f, -0.2f, 0.87f, -0.31f, 0.012f, pp));      /* 더듬이 */
    } else {                                             /* 정면과 뒤: 좌우가 같아서 한쪽(au)만 본다 */
        if (v > -0.08f && au > 0.14f)                    /* 다리 셋씩 */
            for (int i = 0; i < 3; i++) {
                float fi = (float)i, w = (i & 1) ? -wig : wig;
                over(&o, leg, maxf(in_seg(au, v, 0.2f, 0.12f + 0.13f * fi, 0.52f + 0.08f * fi, 0.02f + 0.2f * fi, 0.03f, pp),
                                   in_seg(au, v, 0.52f + 0.08f * fi, 0.02f + 0.2f * fi, 0.66f + 0.08f * fi, 0.54f + 0.15f * fi + w, 0.026f, pp)));
            }
        if (face == 0 || L->air) {                       /* 펼친 날개 */
            float tilt = 0.38f + 0.3f * air + flap, c = cosf(tilt), s = sinf(tilt);
            wing(&o, au - 0.2f, v + 0.12f, c, -s, 0.4f + 0.08f * air, 0.17f + 0.04f * air, 0.5f - 0.15f * air, pp);
        }
        float cov = in_ell(u, v, 0.0f, 0.17f, 0.37f, 0.5f, pp);
        if (cov > 0.0f) {                                /* 몸: 위쪽이 가슴, 아래쪽이 줄무늬 배 */
            float nx = u / 0.37f, ny = (v - 0.17f) / 0.5f, light = lit(nx, ny, &spec);
            float bandv = smooth(0.55f, 0.85f, 0.5f - 0.5f * cosf((v - 0.12f) * 26.0f));
            col_t c = cmix(thorax, cmix(amber, dark, bandv), smooth(0.06f, 0.18f, v));
            c = cmul(TONE(c), light);
            c.r += spec * 70.0f; c.g += spec * 70.0f; c.b += spec * 70.0f;
            over(&o, c, cov);
        }
        if (face == 1 && !L->air) {                      /* 등을 덮은 날개 둘 */
            float c = cosf(0.14f), s = sinf(0.14f);
            wing(&o, au - 0.1f, v + 0.15f, s, c, 0.36f, 0.17f, 0.5f, pp);
        }
        float hr = face == 0 ? 0.27f : 0.23f;
        cov = in_ell(u, v, 0.0f, -0.43f, hr, hr, pp);
        if (cov > 0.0f) over(&o, cmul(TONE(head), lit(u / hr, (v + 0.43f) / hr, NULL) * (face == 0 ? 1.0f : 0.8f)), cov);
        if (face == 0) {
            cov = in_ell(au, v, 0.185f, -0.47f, 0.15f, 0.2f, pp);
            if (cov > 0.0f) {                            /* 겹눈: 가까이서는 낱눈의 결이 보인다 */
                float sgn = u < 0.0f ? -1.0f : 1.0f, light = lit(sgn * (au - 0.185f) / 0.15f, (v + 0.47f) / 0.2f, &spec);
                col_t c = cmul(cmix(eye, hot, L->charge), light * (pp > 70.0f ? 0.92f + 0.08f * cosf(u * 80.0f) * cosf(v * 80.0f) : 1.0f));
                c.r += spec * 150.0f; c.g += spec * 150.0f; c.b += spec * 150.0f;
                over(&o, c, cov);
            }
            over(&o, dark, in_ell(u, v, 0.0f, -0.2f, 0.055f, 0.1f, pp));                         /* 입 */
            over(&o, dark, in_seg(au, v, 0.05f, -0.63f, 0.13f, -0.78f, 0.012f, pp));             /* 더듬이 */
        }
    }
    #undef TONE
    if (L->stun) {                                       /* 끈끈이에 붙었다 */
        float cov = in_ell(u, v, 0.0f, 0.82f, 0.72f, 0.17f, pp);
        if (cov > 0.0f) {
            float light = lit(u / 0.72f, (v - 0.82f) / 0.17f, &spec);
            col_t c = cmul(goo, light);
            c.r += spec * 90.0f; c.g += spec * 90.0f; c.b += spec * 90.0f;
            over(&o, c, cov * 0.95f);
        }
    }
    if (L->flash > 0.0f) {                               /* 방금 맞았다: 하얗게 번쩍인다 */
        o.r += (white.r * o.a - o.r) * L->flash; o.g += (white.g * o.a - o.g) * L->flash; o.b += (white.b * o.a - o.b) * L->flash;
    }
    return o;
}

/* ---- 세워 그리는 것들 ---------------------------------------------------------------- */
typedef struct { float x, y, depth, side; int kind, idx; } thing_t;   /* kind 0: 초파리, 1: 참가자, 2: 끈끈이 */

/* 바닥에 깔리는 그림자. (cx, fy)는 발밑, rx는 가로 반지름(픽셀) */
static void shadow(image_t *im, const thing_t *t, float cx, float fy, float rx, float strength)
{
    static const col_t black = { 0, 0, 0 };
    float ry = rx * 0.22f;
    int x0 = (int)floorf(cx - rx), x1 = (int)ceilf(cx + rx), y0 = (int)floorf(fy - ry), y1 = (int)ceilf(fy + ry);
    if (x0 < 0) x0 = 0;
    if (x1 > VW) x1 = VW;
    if (y0 < VY) y0 = VY;
    if (y1 > VY + VH) y1 = VY + VH;
    for (int x = x0; x < x1; x++) {
        if (t->depth >= zbuf[x]) continue;
        float nx = ((float)x + 0.5f - cx) / rx;
        for (int y = y0; y < y1; y++) {
            float ny = ((float)y + 0.5f - fy) / ry, f = nx * nx + ny * ny;
            if (f < 1.0f) px_mix(im, x, y, black, strength * (1.0f - f));
        }
    }
}

typedef struct { image_t *im; float depth, cx, cy, half; int face, turn; const fly_look_t *look; int x0, x1, y0; } fly_job_t;

static void fly_rows(int from, int to, void *arg)
{
    const fly_job_t *j = arg;
    for (int y = j->y0 + from; y < j->y0 + to; y++) {
        float v = ((float)y + 0.5f - j->cy) / j->half;
        for (int x = j->x0; x < j->x1; x++) {
            if (j->depth >= zbuf[x]) continue;
            acc_t o = fly_point(((float)x + 0.5f - j->cx) / j->half, v, j->half, j->face, j->turn, j->look);
            px_over(j->im, x, y, &o);
        }
    }
}

static void draw_fly(image_t *im, const boss_snap_t *b, const boss_view_t *v, const thing_t *t, float eye_x, float eye_y)
{
    float scale = UNIT / t->depth, half = maxf(0.6f * scale, 2.0f);
    float cx = (float)VW * 0.5f * (1.0f + t->side / t->depth), fy = MID + 0.5f * scale, cy = fy - 0.55f * scale;
    shadow(im, t, cx, fy, half * (b->fly.air ? 0.6f : 0.85f), b->fly.air ? 0.3f : 0.5f);
    if (b->fly.air) cy -= 0.55f * scale;                             /* 떠 있다 */
    /* 초파리가 나를 보고 있는가, 등을 돌렸는가, 옆을 보는가 */
    float fa = (float)b->fly.ang * TWO_PI / 65536.0f, hx = cosf(fa), hy = sinf(fa);
    float tx = eye_x - t->x, ty = eye_y - t->y, len = sqrtf(tx * tx + ty * ty);
    float front = len > 1e-4f ? (hx * tx + hy * ty) / len : 1.0f, cross = len > 1e-4f ? (hx * ty - hy * tx) / len : 0.0f;
    int face = front > 0.5f ? 0 : (front < -0.5f ? 1 : 2), turn = cross > 0.0f ? -1 : 1;
    fly_look_t look = { .stun = b->fly.stun > 0, .air = b->fly.air > 0, .tick = b->tick,
                        .charge = (b->fly.windup || b->fly.charging) ? 0.55f + 0.45f * sinf((float)b->tick * 1.6f) : 0.0f,
                        .flash = v->hit_ms >= 0 && v->hit_ms < 160 ? 0.7f * (1.0f - (float)v->hit_ms / 160.0f) : 0.0f };
    fly_job_t job = { im, t->depth, cx, cy, half, face, turn, &look,
                      (int)floorf(cx - half * 1.05f), (int)ceilf(cx + half * 1.05f), (int)floorf(cy - half * 1.05f) };
    int y1 = (int)ceilf(cy + half * 1.05f);
    if (job.x0 < 0) job.x0 = 0;
    if (job.x1 > VW) job.x1 = VW;
    if (job.y0 < VY) job.y0 = VY;
    if (y1 > VY + VH) y1 = VY + VH;
    par_for(y1 - job.y0, 8, fly_rows, &job);
}

/* 다른 참가자: 색 옷을 입은 둥근 기둥에 머리. 쓰러졌으면 바닥에 납작하게 */
static void draw_player(image_t *im, const boss_snap_t *b, const thing_t *t)
{
    static const col_t skin = { 235, 188, 140 }, down = { 110, 115, 130 };
    float scale = UNIT / t->depth, cx = (float)VW * 0.5f * (1.0f + t->side / t->depth), fy = MID + 0.5f * scale;
    int alive = b->p[t->idx].alive;
    rgb_t pc = PCOL[t->idx % BOSS_PLAYERS];
    col_t cloth = { (float)pc.r, (float)pc.g, (float)pc.b };
    shadow(im, t, cx, fy, 0.3f * scale, 0.45f);
    int x0 = (int)floorf(cx - 0.45f * scale), x1 = (int)ceilf(cx + 0.45f * scale);
    int y0 = (int)floorf(fy - 1.1f * scale), y1 = (int)ceilf(fy + 1.0f);
    if (x0 < 0) x0 = 0;
    if (x1 > VW) x1 = VW;
    if (y0 < VY) y0 = VY;
    if (y1 > VY + VH) y1 = VY + VH;
    for (int x = x0; x < x1; x++) {
        if (t->depth >= zbuf[x]) continue;
        float u = ((float)x + 0.5f - cx) / scale;                    /* 칸 단위. 발밑이 (0, 0)이고 위가 음수다 */
        for (int y = y0; y < y1; y++) {
            float w = ((float)y + 0.5f - fy) / scale, spec;
            acc_t o = { 0, 0, 0, 0 };
            if (!alive) {
                float cov = in_ell(u, w, 0.0f, -0.07f, 0.4f, 0.07f, scale);
                if (cov > 0.0f) over(&o, cmul(down, lit(u / 0.4f, (w + 0.07f) / 0.07f, NULL)), cov);
            } else {
                /* 몸통: 위아래가 둥근 기둥 = 굵은 선분 */
                float cov = in_seg(u, w, 0.0f, -0.2f, 0.0f, -0.6f, 0.2f, scale);
                if (cov > 0.0f) {
                    float light = lit(u / 0.2f, 0.0f, &spec);
                    col_t c = cmul(cloth, light * (1.05f + 0.25f * (w + 0.4f)));
                    c.r += spec * 60.0f; c.g += spec * 60.0f; c.b += spec * 60.0f;
                    over(&o, c, cov);
                }
                cov = in_ell(u, w, 0.0f, -0.93f, 0.14f, 0.14f, scale);
                if (cov > 0.0f) {
                    col_t c = cmul(skin, lit(u / 0.14f, (w + 0.93f) / 0.14f, &spec));
                    c.r += spec * 50.0f; c.g += spec * 50.0f; c.b += spec * 50.0f;
                    over(&o, c, cov);
                }
            }
            px_over(im, x, y, &o);
        }
    }
}

static void draw_trap(image_t *im, const thing_t *t)
{
    static const col_t goo = { 240, 140, 40 };
    float scale = UNIT / t->depth, cx = (float)VW * 0.5f * (1.0f + t->side / t->depth), cy = MID + 0.1f * scale;
    float r = maxf(0.12f * scale, 1.5f);
    shadow(im, t, cx, MID + 0.5f * scale, r * 0.9f, 0.4f);
    int x0 = (int)floorf(cx - r - 1.0f), x1 = (int)ceilf(cx + r + 1.0f), y0 = (int)floorf(cy - r - 1.0f), y1 = (int)ceilf(cy + r + 1.0f);
    if (x0 < 0) x0 = 0;
    if (x1 > VW) x1 = VW;
    if (y0 < VY) y0 = VY;
    if (y1 > VY + VH) y1 = VY + VH;
    for (int x = x0; x < x1; x++) {
        if (t->depth >= zbuf[x]) continue;
        for (int y = y0; y < y1; y++) {
            float nx = ((float)x + 0.5f - cx) / r, ny = ((float)y + 0.5f - cy) / r, spec;
            float cov = in_ell(nx, ny, 0.0f, 0.0f, 1.0f, 1.0f, r);
            if (cov <= 0.0f) continue;
            col_t c = cmul(goo, lit(nx, ny, &spec));
            c.r += spec * 120.0f; c.g += spec * 120.0f; c.b += spec * 120.0f;
            px_mix(im, x, y, c, cov);
        }
    }
}

static void draw_things(image_t *im, const boss_snap_t *b, const boss_view_t *v, int eye, float px, float py, float dx, float dy)
{
    thing_t list[1 + BOSS_PLAYERS + BOSS_PROJ];
    float plx = -dy * PLANE, ply = dx * PLANE, inv = 1.0f / (plx * dy - dx * ply);
    int n = 0;
    list[n++] = (thing_t){ (float)b->fly.x / BOSS_FIX, (float)b->fly.y / BOSS_FIX, 0, 0, 0, 0 };
    for (int i = 0; i < b->n_players && i < BOSS_PLAYERS; i++)
        if (i != eye) list[n++] = (thing_t){ (float)b->p[i].x / BOSS_FIX, (float)b->p[i].y / BOSS_FIX, 0, 0, 1, i };
    for (int k = 0; k < b->n_proj && k < BOSS_PROJ; k++)
        list[n++] = (thing_t){ (float)b->proj[k].x / BOSS_FIX, (float)b->proj[k].y / BOSS_FIX, 0, 0, 2, k };
    for (int i = 0; i < n; i++) {                    /* 눈 기준 좌표: side는 오른쪽으로, depth는 앞으로 */
        float rx = list[i].x - px, ry = list[i].y - py;
        list[i].side = inv * (dy * rx - dx * ry);
        list[i].depth = inv * (-ply * rx + plx * ry);
    }
    for (int i = 1; i < n; i++)                      /* 먼 것부터 */
        for (int j = i; j > 0 && list[j].depth > list[j - 1].depth; j--) {
            thing_t tmp = list[j]; list[j] = list[j - 1]; list[j - 1] = tmp;
        }
    for (int i = 0; i < n; i++) {
        if (list[i].depth < 0.2f) continue;          /* 등 뒤에 있다 */
        if (list[i].kind == 0) draw_fly(im, b, v, &list[i], px, py);
        else if (list[i].kind == 1) draw_player(im, b, &list[i]);
        else draw_trap(im, &list[i]);
    }
}

/* ---- 손에 든 무기 --------------------------------------------------------------------- */
/* 무기는 3D 화면의 일부라서 크게 그린다. 자리는 화면 너비를 480으로 나눈 단위(G)로 적는다 */
static void draw_weapon(image_t *im, const boss_snap_t *b, int me)
{
    int item = b->p[me].item, used = b->p[me].cool >= 6;         /* 방금 썼다 */
    float bx = (float)VW * 0.5f + 92.0f * G, by = (float)(VY + VH) + 2.0f * G;      /* 손이 있는 자리(화면 아래 오른쪽) */
    if (item == BOSS_SWATTER) {
        float tx = bx - (used ? 46.0f : 16.0f) * G, ty = by - (used ? 96.0f : 62.0f) * G;         /* 채의 가운데 */
        float w = (used ? 54.0f : 46.0f) * G, h = (used ? 58.0f : 50.0f) * G;
        hd_line(im, bx + 6.0f * G, by + 6.0f * G, tx, ty + h * 0.45f, 6.5f * G, hd_rgb(70, 44, 26), 1.0f);
        hd_line(im, bx + 6.0f * G, by + 6.0f * G, tx, ty + h * 0.45f, 4.0f * G, hd_rgb(150, 100, 62), 1.0f);
        hd_rrect(im, tx - w * 0.5f, ty - h * 0.5f, w, h, 7.0f * G, hd_rgb(96, 102, 120), 1.0f);
        hd_rrect(im, tx - w * 0.5f + 2.0f * G, ty - h * 0.5f + 2.0f * G, w - 4.0f * G, h - 4.0f * G, 5.5f * G, hd_rgb(186, 192, 206), 0.9f);
        for (int k = 1; k < 9; k++) {                             /* 그물 */
            float gx = tx - w * 0.5f + w * (float)k / 9.0f, gy = ty - h * 0.5f + h * (float)k / 9.0f;
            hd_line(im, gx, ty - h * 0.5f + 3.0f * G, gx, ty + h * 0.5f - 3.0f * G, 0.7f * G, hd_rgb(104, 110, 128), 0.9f);
            hd_line(im, tx - w * 0.5f + 3.0f * G, gy, tx + w * 0.5f - 3.0f * G, gy, 0.7f * G, hd_rgb(104, 110, 128), 0.9f);
        }
    } else if (item == BOSS_SPRAY) {
        float x = bx - 22.0f * G, y = by - (used ? 62.0f : 54.0f) * G;
        hd_vgrad(im, x, y + 10.0f * G, 26.0f * G, 60.0f * G, 5.0f * G, hd_rgb(84, 212, 232), hd_rgb(36, 128, 160), 1.0f);
        hd_rrect(im, x + 3.0f * G, y + 26.0f * G, 20.0f * G, 13.0f * G, 2.0f * G, hd_rgb(236, 240, 244), 0.92f);
        hd_rrect(im, x + 6.0f * G, y + 1.0f * G, 14.0f * G, 11.0f * G, 3.0f * G, hd_rgb(226, 230, 236), 1.0f);
        hd_rrect(im, x + 2.0f * G, y + 3.0f * G, 6.0f * G, 4.0f * G, 1.5f * G, hd_rgb(60, 64, 78), 1.0f);
        if (used)                                                 /* 뿜는 물줄기: 화면 가운데 쪽으로 퍼진다 */
            for (int k = 0; k < 12; k++) {
                float t = (float)k / 11.0f;
                hd_ellipse(im, x - t * 70.0f * G, y + 4.0f * G - t * 42.0f * G, (3.0f + 11.0f * t) * G, (2.5f + 8.0f * t) * G,
                           hd_rgb(220, 244, 250), 0.42f * (1.0f - t * 0.7f));
            }
    } else {
        float x = bx - 14.0f * G, y = by - (used ? 46.0f : 32.0f) * G, r = 17.0f * G;
        hd_ellipse(im, x, y, r, r * 0.9f, hd_rgb(180, 92, 20), 1.0f);
        hd_ellipse(im, x - r * 0.08f, y - r * 0.1f, r * 0.9f, r * 0.8f, hd_rgb(240, 140, 40), 1.0f);
        hd_ellipse(im, x - r * 0.35f, y - r * 0.4f, r * 0.3f, r * 0.2f, hd_rgb(255, 226, 170), 0.8f);
    }
}

/* ---- 상태 표시 ------------------------------------------------------------------------ */
/* 상태 표시는 3D 화면 위에 작게 띄운다(화면을 가리는 띠를 두지 않는다). 둠 같은 1인칭 게임의 배치를 따랐다:
 * 맨 위 가운데에 상대의 체력, 왼쪽 아래에 내 체력, 오른쪽 아래에 무기와 남은 수, 가운데에 조준 표시.
 * 자리는 화면을 960x536으로 본 단위(U)로 적는다. 글자 한 줄이 화면 높이의 2%쯤이다(2400x1340에서 30픽셀). */
#define HW 960.0f
#define HH 536.0f

/* 글자. 자리는 U 단위, k는 글자 크기의 배율(1이면 12U), a는 진하기. 폭(U)을 돌려준다 */
static float text(image_t *im, float x, float y, const char *s, rgb_t c, float k, float a)
{
    hd_text(im, x * U, y * U, s, c, CELL * k * U, a);
    return (float)ui_text_w(s) * k;
}

static void plate(image_t *im, float x, float y, float w, float h, float a)
{
    hd_rrect(im, x * U, y * U, w * U, h * U, 5.0f * U, hd_rgb(8, 10, 18), a);
}

/* 막대 하나. 값(0~1)만큼 차고, mark가 0보다 크면 그 자리에 세로줄(기준)을 긋는다 */
static void bar(image_t *im, float x, float y, float w, float h, float v, float mark, rgb_t c)
{
    hd_rrect(im, x * U, y * U, w * U, h * U, h * 0.5f * U, hd_rgb(40, 44, 60), 0.9f);
    if (v > 0.01f) hd_rrect(im, x * U, y * U, maxf(w * clamp01(v), h) * U, h * U, h * 0.5f * U, c, 1.0f);
    if (mark > 0.0f) hd_rrect(im, (x + w * clamp01(mark) - 0.5f) * U, (y - 2.0f) * U, 1.0f * U, (h + 4.0f) * U, 0.5f * U, hd_rgb(255, 255, 255), 1.0f);
}

/* 초파리 뇌: 왼쪽 위. 그림은 미니게임 화면의 뇌 표시(ui/compose.c)를 그대로 쓰고 그 안의 글자만 다시 그린다.
 * "자극"은 참가자가 다가가서 초파리에게 주는 다가옴 자극이고 "도약"은 뇌의 달아남 반응이다. 도약 막대가 세로줄
 * (뛰는 기준)을 넘으면 초파리가 뛰어올라 달아난다. 차지한 자리의 오른쪽 끝(U)을 돌려준다 */
static float draw_brain(image_t *im, const boss_snap_t *b, const boss_view_t *v)
{
    static canvas_t *small;
    static const float zero[BRAIN_GROUP_COUNT];
    const float *stim = v->brain_stim ? v->brain_stim : zero, *rate = v->brain_rate;
    if (!rate) return 0.0f;
    if (!small) small = canvas_new(BRAIN_PANEL_W + 2, BRAIN_PANEL_H + 2);
    if (!small) return 0.0f;
    int n = im->w / SCREEN_W / 2 > 0 ? im->w / SCREEN_W / 2 : 1;                 /* 그림의 픽셀 하나가 몇 픽셀인가 */
    float pw = (float)((BRAIN_PANEL_W + 2) * n) / U, ph = (float)((BRAIN_PANEL_H + 2) * n) / U, x = 10.0f, y = 10.0f, w = pw + 10.0f;
    plate(im, x, y, w, 13.0f + ph + 27.0f, 0.72f);
    text(im, x + 5.0f, y + 3.0f, "초파리 뇌", SKY, 0.8f, 1.0f);

    const ui_text_item_t *items;
    int ox = (int)((x + 5.0f) * U), oy = (int)((y + 13.0f) * U);
    ui_clear(small, C_BLACK);
    ui_text_capture_begin(small);
    compose_brain_panel(small, 1, 1, stim, rate, v->brain_map, b->tick / 2);
    int k = ui_text_capture_end(&items);
    hd_canvas(im, ox, oy, small, n);
    for (int i = 0; i < k; i++)
        if (items[i].kind == UI_HD_TEXT)
            hd_text(im, (float)(ox + items[i].x * n), (float)(oy + items[i].y * n), items[i].s, hd_color(items[i].col), (float)(UI_KFONT_H * n), 1.0f);

    float gy = y + 13.0f + ph + 3.0f, loom = stim[BR_LOOM_L] > stim[BR_LOOM_R] ? stim[BR_LOOM_L] : stim[BR_LOOM_R];
    int jump = v->takeoff > 0.0f && rate[BR_ESCAPE] >= v->takeoff;
    float lw = text(im, x + 5.0f, gy, "자극", DIM, 0.75f, 1.0f) + 5.0f;
    bar(im, x + 5.0f + lw, gy + 3.0f, w - 10.0f - lw, 4.0f, loom, 0.0f, SKY);
    text(im, x + 5.0f, gy + 11.0f, "도약", DIM, 0.75f, 1.0f);
    bar(im, x + 5.0f + lw, gy + 14.0f, w - 10.0f - lw, 4.0f, rate[BR_ESCAPE], v->takeoff, jump ? hd_rgb(245, 190, 60) : SKY);
    return x + w;
}

/* 작은 지도: 오른쪽 위. my_ang은 내가 보는 방향이다(me가 0 이상일 때). 왼쪽 끝(U)을 돌려준다 */
static float draw_minimap(image_t *im, const boss_snap_t *b, int me, float my_ang)
{
    const float cell = 3.2f, size = BOSS_MAP * cell, x = HW - size - 14.0f, y = 30.0f;
    plate(im, x - 4.0f, y - 4.0f, size + 8.0f, size + 8.0f, 0.72f);
    for (int cy = 0; cy < BOSS_MAP; cy++)
        for (int cx = 0; cx < BOSS_MAP; cx++) {
            if (!wall[cy][cx]) continue;
            int edge = cx == 0 || cy == 0 || cx == BOSS_MAP - 1 || cy == BOSS_MAP - 1;
            hd_rect(im, (x + (float)cx * cell) * U, (y + (float)cy * cell) * U, cell * U + 0.5f, cell * U + 0.5f,
                    edge ? hd_rgb(74, 80, 98) : hd_rgb(196, 146, 100), 1.0f);
        }
    float fx = (x + (float)b->fly.x * cell / BOSS_FIX) * U, fy = (y + (float)b->fly.y * cell / BOSS_FIX) * U;
    rgb_t fc = b->fly.stun ? hd_rgb(240, 140, 40) : (b->fly.air ? hd_rgb(250, 220, 70) : hd_rgb(232, 60, 54));
    if (b->fly.windup || b->fly.charging) hd_ring(im, fx, fy, (4.0f + (float)(b->tick % 6)) * U, 0.9f * U, fc, 0.7f);
    hd_ellipse(im, fx, fy, 2.6f * U, 2.6f * U, fc, 1.0f);
    for (int i = 0; i < b->n_players && i < BOSS_PLAYERS; i++) {
        if (!b->p[i].alive) continue;
        float pxx = (x + (float)b->p[i].x * cell / BOSS_FIX) * U, pyy = (y + (float)b->p[i].y * cell / BOSS_FIX) * U;
        float a = i == me ? my_ang : (float)b->p[i].ang * TWO_PI / 65536.0f;
        rgb_t c = i == me ? hd_rgb(255, 255, 255) : PCOL[i];
        if (i == me)                                              /* 내가 보는 범위 */
            for (int s = -1; s <= 1; s += 2)
                hd_line(im, pxx, pyy, pxx + cosf(a + (float)s * 0.58f) * 12.0f * U, pyy + sinf(a + (float)s * 0.58f) * 12.0f * U,
                        0.7f * U, c, 0.35f);
        hd_line(im, pxx, pyy, pxx + cosf(a) * 4.2f * U, pyy + sinf(a) * 4.2f * U, 1.4f * U, c, 1.0f);
        hd_ellipse(im, pxx, pyy, 2.1f * U, 2.1f * U, c, 1.0f);
    }
    return x - 4.0f;
}

/* 하트 하나. (cx, cy)는 가운데, s는 반 너비(픽셀). 식 (x^2 + y^2 - 1)^3 < x^2 y^3 의 안쪽을 아홉 군데 떠서 칠한다 */
static void heart(image_t *im, float cx, float cy, float s, rgb_t c)
{
    for (int py = (int)(cy - s * 1.3f); py <= (int)(cy + s * 1.3f); py++)
        for (int px = (int)(cx - s * 1.3f); px <= (int)(cx + s * 1.3f); px++) {
            int in = 0;
            for (int k = 0; k < 9; k++) {
                float x = ((float)px + 0.17f + 0.33f * (float)(k % 3) - cx) / s * 1.2f;
                float y = -((float)py + 0.17f + 0.33f * (float)(k / 3) - cy) / s * 1.2f + 0.15f;
                float q = x * x + y * y - 1.0f;
                in += q * q * q - x * x * y * y * y < 0.0f;
            }
            hd_blend(im, px, py, c, (float)in / 9.0f);
        }
}

/* 맨 위 가운데: 초파리의 체력. 오른쪽 위 구석: 남은 시간 */
static void draw_boss_bar(image_t *im, const boss_snap_t *b)
{
    char line[32];
    const float w = 360.0f, x = (HW - w) * 0.5f, y = 14.0f;
    plate(im, x - 62.0f, y - 6.0f, w + 124.0f, 19.0f, 0.6f);
    text(im, x - 54.0f, y - 3.0f, "초파리", INK, 0.9f, 1.0f);
    hd_rrect(im, x * U, y * U, w * U, 7.0f * U, 3.5f * U, hd_rgb(44, 48, 64), 1.0f);
    if (b->fly.hp > 0 && b->fly.hp_max > 0)
        hd_vgrad(im, x * U, y * U, maxf(w * (float)b->fly.hp / (float)b->fly.hp_max, 7.0f) * U, 7.0f * U, 3.5f * U,
                 hd_rgb(244, 92, 76), hd_rgb(176, 34, 36), 1.0f);
    snprintf(line, sizeof line, "%d/%d", b->fly.hp, b->fly.hp_max);
    text(im, x + w + 8.0f, y - 1.5f, line, INK, 0.85f, 1.0f);
    int sec = b->ticks_left / 20;
    snprintf(line, sizeof line, "%d:%02d", sec / 60, sec % 60);
    float tw = (float)ui_text_w(line) * 1.1f;
    plate(im, HW - tw - 24.0f, 8.0f, tw + 14.0f, 17.0f, 0.6f);
    text(im, HW - tw - 17.0f, 11.0f, line, sec < 30 ? hd_rgb(245, 190, 60) : INK, 1.1f, 1.0f);
}

/* 왼쪽 아래: 내 체력과 막기. 오른쪽 아래: 고른 무기와 남은 수, 그 아래에 무기 셋과 고르는 숫자 키 */
static void draw_status(image_t *im, const boss_snap_t *b, const boss_view_t *v, int eye)
{
    static const char *item_name[BOSS_ITEMS] = { "파리채", "스프레이", "끈끈이" };
    char line[64];
    const float y = HH - 46.0f;
    if (eye < 0) {
        plate(im, 12.0f, y + 14.0f, 120.0f, 20.0f, 0.65f);
        text(im, 20.0f, y + 18.0f, "모두 쓰러졌다", DIM, 1.0f, 1.0f);
        return;
    }
    if (eye != v->me) {
        snprintf(line, sizeof line, "관전 %.10s   TAB 다른 사람", v->name[eye] ? v->name[eye] : "");
        plate(im, 12.0f, y + 14.0f, (float)ui_text_w(line) + 16.0f, 20.0f, 0.65f);
        text(im, 20.0f, y + 18.0f, line, PCOL[eye], 1.0f, 1.0f);
        return;
    }
    /* 체력(하트)과 막기. 막기는 쉬는 동안 막대가 차오르고, 다 차면 다시 쓸 수 있다 */
    int hp = b->p[eye].hp < 5 ? b->p[eye].hp : 5, cool = b->p[eye].guard_cool;
    plate(im, 12.0f, y, 132.0f, 34.0f, 0.65f);
    for (int k = 0; k < 3 || k < hp; k++)
        heart(im, (26.0f + (float)k * 17.0f) * U, (y + 11.0f) * U, 6.6f * U, k < hp ? hd_rgb(236, 72, 80) : hd_rgb(56, 58, 74));
    float lw = text(im, 20.0f, y + 21.0f, "E", KEY, 0.75f, 1.0f) + 3.0f;
    lw += text(im, 20.0f + lw, y + 21.0f, "막기", cool ? DIM : INK, 0.75f, 1.0f) + 6.0f;
    bar(im, 20.0f + lw, y + 24.5f, 112.0f - lw, 3.6f, 1.0f - (float)cool / 50.0f, 0.0f, cool ? hd_rgb(110, 115, 130) : SKY);
    if (b->p[eye].sneak) {
        plate(im, 150.0f, y + 16.0f, 62.0f, 18.0f, 0.65f);
        text(im, 157.0f, y + 20.0f, "살금살금", SKY, 0.8f, 1.0f);
    }

    /* 무기 */
    int item = b->p[eye].item;
    const float rw = 196.0f, rx = HW - rw - 12.0f;
    plate(im, rx, y, rw, 34.0f, 0.65f);
    text(im, rx + 9.0f, y + 4.0f, item_name[item], KEY, 1.05f, 1.0f);
    if (item != BOSS_SWATTER) {
        snprintf(line, sizeof line, "%d", item == BOSS_SPRAY ? b->p[eye].ammo : b->p[eye].traps);
        float nw = (float)ui_text_w(line) * 1.5f;
        text(im, rx + rw - nw - 10.0f, y + 2.5f, line, INK, 1.5f, 1.0f);
    }
    float sx = rx + 9.0f;
    for (int k = 0; k < BOSS_ITEMS; k++) {
        const char key[2] = { (char)('1' + k), '\0' };               /* 무기 k를 고르는 키는 SK_1 + k다 */
        sx += text(im, sx, y + 21.0f, key, k == item ? KEY : DIM, 0.75f, 1.0f) + 2.0f;
        sx += text(im, sx, y + 21.0f, item_name[k], k == item ? INK : hd_rgb(110, 115, 130), 0.75f, 1.0f) + 8.0f;
    }
}

/* 아래 가운데에 작게 한 줄: 키 안내. 조작이 바뀌면 boss_view.c의 draw_guide, draw_legend와 docs/guide.md를 같이 고친다 */
static void draw_hint(image_t *im)
{
    static const char *const part[][2] = { { "WASD", "이동" }, { "마우스", "시선" }, { "클릭", "쓰기" }, { "E", "막기" },
                                           { "C", "살금살금" }, { "H", "안내" } };
    const int n = (int)(sizeof part / sizeof part[0]);
    float w = 0.0f, k = 0.7f;
    for (int i = 0; i < n; i++) w += (float)(ui_text_w(part[i][0]) + ui_text_w(part[i][1])) * k + 3.0f + (i + 1 < n ? 10.0f : 0.0f);
    float x = (HW - w) * 0.5f, y = HH - 12.0f;
    for (int i = 0; i < n; i++) {
        x += text(im, x, y, part[i][0], KEY, k, 0.8f) + 3.0f;
        x += text(im, x, y, part[i][1], INK, k, 0.7f) + 10.0f;
    }
}

/* 보스전이 끝났으면 가운데에 결과를 알린다 */
static void draw_outcome(image_t *im, const boss_snap_t *b)
{
    if (b->outcome == BOSS_GOING) return;
    int win = b->outcome == BOSS_PLAYERS_WIN, alive = 0;
    for (int i = 0; i < b->n_players && i < BOSS_PLAYERS; i++) alive += b->p[i].alive;
    const char *head = win ? "초파리를 잡았다!" : (alive ? "시간이 다 됐다" : "모두 쓰러졌다");
    const char *sub  = win ? "살아남은 사람은 점수를 더 받아요" : "초파리가 이겼어요";
    rgb_t col = win ? hd_rgb(85, 200, 120) : hd_rgb(230, 80, 80);
    float hw = (float)ui_text_w(head) * 2.2f, sw = (float)ui_text_w(sub);
    float bw = maxf(hw, sw) + 48.0f, bh = 66.0f, bx = (HW - bw) * 0.5f, by = HH * 0.5f + 18.0f;
    hd_rrect(im, (bx - 1.5f) * U, (by - 1.5f) * U, (bw + 3.0f) * U, (bh + 3.0f) * U, 9.0f * U, col, 1.0f);
    hd_rrect(im, bx * U, by * U, bw * U, bh * U, 8.0f * U, hd_rgb(8, 9, 16), 1.0f);
    text(im, (HW - hw) * 0.5f, by + 10.0f, head, col, 2.2f, 1.0f);
    text(im, (HW - sw) * 0.5f, by + 44.0f, sub, INK, 1.0f, 1.0f);
}

static void draw_hud(image_t *im, const boss_snap_t *b, const boss_view_t *v, int eye, float eye_ang)
{
    float mid_x = (float)im->w * 0.5f;
    if (eye >= 0 && eye == v->me) {
        /* 가운데 표시. 방금 맞혔으면 빨갛고, 살금살금이 켜져 있으면 하늘색이다 */
        int hit = v->hit_ms >= 0 && v->hit_ms < 250;
        rgb_t c = hit ? hd_rgb(255, 80, 70) : (b->p[eye].sneak ? SKY : hd_rgb(255, 255, 255));
        draw_weapon(im, b, eye);
        for (int k = 0; k < 4; k++) {
            float ax = (float)((k == 0) - (k == 1)), ay = (float)((k == 2) - (k == 3));
            hd_line(im, mid_x + ax * 5.0f * U, MID + ay * 5.0f * U, mid_x + ax * 11.0f * U, MID + ay * 11.0f * U, 1.6f * U, c, 0.95f);
        }
        hd_ellipse(im, mid_x, MID, 1.2f * U, 1.2f * U, c, 0.95f);
        if (hit) hd_ring(im, mid_x, MID, (9.0f + (float)v->hit_ms * 0.07f) * U, 1.6f * U, c, 1.0f - (float)v->hit_ms / 250.0f);
        /* 막는 자세: 가운데에 방패 고리. 막아 냈으면 고리가 하얗게 퍼진다 */
        if (b->p[eye].guard > 0) {
            hd_ring(im, mid_x, MID, 56.0f * U, 5.0f * U, SKY, 0.75f);
            hd_ring(im, mid_x, MID, 62.0f * U, 1.2f * U, hd_rgb(255, 255, 255), 0.8f);
        }
        if (v->block_ms >= 0 && v->block_ms < 400)
            hd_ring(im, mid_x, MID, (56.0f + (float)v->block_ms * 0.3f) * U, 4.0f * U, hd_rgb(255, 255, 255), 1.0f - (float)v->block_ms / 400.0f);
    }
    float right = draw_minimap(im, b, eye, eye_ang), left = draw_brain(im, b, v);
    (void)right;
    (void)left;

    /* 알림: 내가 맞았다(가장자리가 붉어진다), 돌진이 나를 겨눈다, 날갯짓, 끈끈이에 붙었다 */
    if (v->hurt_ms >= 0 && v->hurt_ms < 300) {
        float a = 0.55f * (1.0f - (float)v->hurt_ms / 300.0f), t = 52.0f * U;
        int band = (int)t;
        for (int y = VY; y < VY + VH; y++) {
            int inner = y - VY >= band && VY + VH - 1 - y >= band;       /* 가운데 줄들은 양 끝만 칠한다 */
            for (int x = 0; x < VW; x++) {
                if (inner && x == band) x = VW - band;
                float e = minf(minf((float)x, (float)(VW - 1 - x)), minf((float)(y - VY), (float)(VY + VH - 1 - y)));
                if (e < t) px_mix(im, x, y, (col_t){ 230, 40, 40 }, a * (1.0f - e / t));
            }
        }
    }
    const char *note = NULL;
    rgb_t note_col = hd_rgb(245, 190, 60);
    if (eye >= 0 && b->fly.windup && b->fly.target == eye) { note = "돌진이 온다!  E 막기"; note_col = hd_rgb(255, 90, 80); }
    else if (v->block_ms >= 0 && v->block_ms < 700) { note = "막았다!"; note_col = hd_rgb(85, 200, 120); }
    else if (v->wave_ms >= 0 && v->wave_ms < 500) note = "날갯짓!";
    else if (b->fly.stun) note = "멈췄다! 지금이다";
    else if (b->fly.air) note = "달아난다!  C 살금살금";
    if (note && eye >= 0 && b->outcome == BOSS_GOING) {
        float w = (float)ui_text_w(note) * 1.1f, x = (HW - w) * 0.5f;
        plate(im, x - 10.0f, 38.0f, w + 20.0f, 19.0f, 0.7f);
        text(im, x, 41.0f, note, note_col, 1.1f, 1.0f);
    }
    draw_boss_bar(im, b);
    draw_status(im, b, v, eye);
    if (eye >= 0 && eye == v->me && b->outcome == BOSS_GOING) draw_hint(im);
    draw_outcome(im, b);
}

void boss_view_draw_hd(image_t *im, const boss_snap_t *b, const boss_view_t *v)
{
    int eye = -1;
    if (!im || im->w < SCREEN_W || im->w > SCREEN_W * HD_MAX_SCALE) return;
    if (!have_map) load_map();
    U = (float)im->w / HW;
    G = (float)im->w / 480.0f;
    VW = im->w;
    VY = 0;                                          /* 3D 화면이 그림 전체를 쓴다. 상태 표시는 그 위에 띄운다 */
    VH = im->h;
    MID = (float)VH * 0.5f;
    UNIT = (float)im->w * UNIT_PER_W;

    if (v->me >= 0 && v->me < b->n_players && b->p[v->me].alive) eye = v->me;
    int n = b->n_players < BOSS_PLAYERS ? b->n_players : BOSS_PLAYERS;
    for (int k = 0; eye < 0 && k < n; k++) {         /* 고른 사람부터 차례로, 살아 있는 사람을 찾는다 */
        int i = ((v->watch % n + n) % n + k) % n;
        if (b->p[i].alive) eye = i;
    }

    float eye_ang = 0.0f;
    if (eye >= 0) {
        float px = (float)b->p[eye].x / BOSS_FIX, py = (float)b->p[eye].y / BOSS_FIX;
        /* 내 눈이면 마우스로 돌린 방향을 쓴다. 서버가 알려 준 방향은 한두 틱 늦다 */
        eye_ang = (float)(eye == v->me && v->look_on ? v->look_ang : b->p[eye].ang) * TWO_PI / 65536.0f;
        float dx = cosf(eye_ang), dy = sinf(eye_ang);
        view_job_t job = { im, px, py, dx, dy };
        par_for(VH, 8, ground_rows, &job);
        par_for(VW, 16, wall_columns, &job);
        draw_things(im, b, v, eye, px, py, dx, dy);
    } else {
        hd_fill(im, hd_rgb(14, 16, 28));                 /* 볼 사람이 없다 */
    }
    draw_hud(im, b, v, eye, eye_ang);
}
