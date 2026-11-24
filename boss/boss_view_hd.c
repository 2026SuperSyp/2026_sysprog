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
 *   - 초파리는 상자 몇 개로 된 모형이다(처음 구상한 네모난 초파리). 화면의 점마다 선을 그어 닿는 상자를 찾아
 *     그리므로 어느 쪽에서 보든 이어진 모습이 나오고 가까이 가도 계단이 생기지 않는다.
 *   - 벽이 높다(6칸). 눈높이는 0.5칸이고 뛰어오르면 그만큼 올라가서 바닥이 멀어지고 벽이 내려간다.
 *   - 글자는 큰 글꼴로 그린다(ui/hd.c).
 * 상태 표시의 자리는 화면을 960x536으로 본 단위(U)로 적는다. 배율 10(2400x1340)에서 한 단위가 2.5픽셀이다.
 * 바닥의 줄들, 벽의 세로줄들, 초파리의 줄들은 서로 무관해서 여러 스레드에 나눠 그린다(par_for).
 * 배경(바닥, 천장, 벽)은 눈이 움직이는 동안에는 절반 해상도로 그려 두 배로 키우고, 눈이 멈추면 제 해상도로 한 번
 * 그려 두었다가 다시 쓴다(draw_background). 초파리와 상태 표시는 늘 제 해상도로 그린다. */
#include "boss/boss_view.h"
#include "brain.h"
#include "ui/hd.h"

#include <math.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

#define PLANE       0.66f            /* 시야의 넓이(약 66도) */
#define TWO_PI      6.2831853f
#define UNIT_PER_W  (104.0f / 240.0f)    /* 거리 1에 있는 벽의 높이 = 화면 너비 x 이 값(boss_view.c와 같다) */
#define CELL        12.0f            /* 글자 한 칸의 크기(U) */
#define FOG_K       0.075f           /* 멀수록 어둠에 묻히는 빠르기 */
#define WALL_H      BOSS_VIEW_WALL_H /* 벽의 높이(칸) */
#define STILL_MS    120              /* 눈이 이만큼 멈춰 있으면 배경을 제 해상도로 그린다 */
#define COURSES     18               /* 바깥 벽의 돌이 쌓인 줄 수. 한 줄의 높이가 1/3칸이다(WALL_H x 3) */

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
static float   MID, UNIT, EZ;       /* EZ: 눈의 높이(칸). 서 있으면 BOSS_VIEW_EYE_H이고 뛰어오르면 높아진다 */

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

/* 결: 같은 무늬를 크기를 줄여 가며 세 층 겹친다(0~1). 큰 얼룩 위에 잔 알갱이가 얹힌다.
 * px는 무늬의 좌표 1이 화면에서 몇 픽셀인가다. 픽셀만큼 잘아진 층은 뺀다. 그런 층은 멀리서 어른거리기만 하고
 * 그림을 보내는 양만 늘린다 */
static float grain(float x, float y, int seed, float px)
{
    float w1 = clamp01((px - 2.0f) / 4.0f), w2 = clamp01((px / 2.3f - 2.0f) / 4.0f), w3 = clamp01((px / 5.1f - 2.0f) / 4.0f);
    float v = 0.5f;
    if (w1 > 0.0f) v += (noise(x, y, seed) - 0.5f) * 0.5f * w1;
    if (w2 > 0.0f) v += (noise(x * 2.3f, y * 2.3f, seed + 1) - 0.5f) * 0.3f * w2;
    if (w3 > 0.0f) v += (noise(x * 5.1f, y * 5.1f, seed + 2) - 0.5f) * 0.2f * w3;
    return v;
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
/* 화면의 줄 하나는 바닥(또는 천장)에서 눈으로부터 같은 거리에 있는 곳들이다. 지평선에서 p픽셀 아래의 줄은
 * 눈높이 x UNIT / p 만큼 멀고, p픽셀 위의 줄은 (벽 높이 - 눈높이) x UNIT / p 만큼 멀다. 그래서 뛰어오르면 바닥이
 * 멀어지고 천장이 가까워진다. 그 줄의 왼쪽 끝과 오른쪽 끝이 가리키는 자리를 구해 고르게 나누면
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
        int floor_row = p > 0.0f;
        float dist = (floor_row ? EZ : WALL_H - EZ) * UNIT / ap, ppu = UNIT / dist;
        float sx = dist * (rx - lx) / (float)VW, sy = dist * (ry - ly) / (float)VW;
        float wx = px + dist * lx + sx * 0.5f, wy = py + dist * ly + sy * 0.5f;
        float vis = expf(-dist * FOG_K), detail = clamp01((ppu - 4.0f) / 22.0f);
        float gpx = minf(ppu, 4.0f * ap / dist);     /* 바닥은 비스듬히 보여서 먼 쪽으로는 픽셀 하나가 더 넓은 자리를 덮는다 */
        uint8_t *o = im->px + (size_t)y * (size_t)im->w * 3;
        for (int x = 0; x < VW; x++, wx += sx, wy += sy, o += 3) {
            col_t c;
            if (floor_row) {
                /* 돌판의 줄은 칸의 경계에서 1/4칸 비켜 있다. 참가자는 칸의 경계나 가운데에 서서 시작하고 반 칸씩
                 * 걷는데, 줄이 바로 발밑으로 지나가면 화면 가운데에 굵은 쐐기로 보인다 */
                float tx = wx + 0.25f, ty = wy + 0.25f, gx = floorf(tx), gy = floorf(ty), fx = tx - gx, fy = ty - gy;
                int ix = (int)gx, iy = (int)gy;
                float tone = 0.84f + 0.3f * hash2(ix * 7 + 3, iy * 13 + 1);
                float g = grain(wx * 7.0f, wy * 7.0f, 3, gpx / 7.0f);
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
                float g = grain(wx * 5.0f, wy * 5.0f, 21, gpx / 5.0f);
                c = cmul(panel, 1.0f + (g - 0.5f) * 0.4f * detail);
                float e = minf(minf(fx, 1.0f - fx), minf(fy, 1.0f - fy)) * 2.0f;
                c = cmix(c, gap, clamp01(0.5f + (0.02f - e) * ppu) * (0.4f + 0.6f * detail));
                /* 들보: 네 칸마다 한 줄, 나무결 */
                float bx = (wy + 1.0f) * 0.25f, fb = bx - floorf(bx), be = fabsf(fb - 0.5f) * 4.0f;
                float on = clamp01(0.5f + (0.16f - be) * ppu);
                if (on > 0.0f) {
                    float wood = grain(wx * 2.0f, wy * 26.0f, 33, gpx / 26.0f);
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

/* 바깥 벽: 돌을 열여덟 줄로 쌓았고 줄마다 반 칸씩 어긋난다. v는 위에서 아래로(0~1), u는 벽을 따라 잰 자리(칸).
 * 돌마다 빛깔이 다르고, 결과 얼룩과 금이 있고, 가장자리가 깎여 도드라져 보인다. 아래쪽은 습기로 어둡고 이끼가 낀다.
 * 바닥에서 두 칸과 네 칸 남짓한 높이에 밝은 돌로 띠를 둘렀고(그 밑에 그늘이 진다), 위로 갈수록 빛이 덜 닿아 어둡다 */
static col_t stone(const hit_t *h, float v, float u)
{
    static const col_t base = { 158, 164, 182 }, warm = { 172, 160, 150 }, mortar = { 58, 62, 78 }, moss = { 70, 92, 74 };
    static const col_t ledge = { 196, 198, 210 };
    float vr = v * (float)COURSES, ppu = h->ppu, up = (1.0f - v) * WALL_H, down = v * WALL_H;    /* 바닥에서, 천장에서(칸) */
    int row = vr >= (float)COURSES ? COURSES - 1 : (int)vr, lvl = COURSES - 1 - row;             /* lvl: 바닥에서 몇째 줄인가 */
    int band = lvl == 6 || lvl == 13;                                                            /* 띠를 두른 줄 */
    float uu = u * (band ? 1.0f : 2.0f) + ((row & 1) ? 0.5f : 0.0f), block = floorf(uu), fu = uu - block, fv = vr - (float)row;
    int id = (int)block * COURSES + row + h->face * 131 + h->side * 17;
    float t = hash2(id, 5), g = grain(u * 9.0f, down * 9.0f + (float)h->face, 7, ppu / 9.0f);
    col_t c = cmul(cmix(base, warm, hash2(id, 9) * 0.6f), (0.8f + 0.34f * t) * (1.0f + (g - 0.5f) * 0.5f));
    if (band) c = cmix(c, ledge, 0.42f);
    else if (hash2(id, 23) > 0.7f) {                 /* 금 */
        float bend = fu + (noise(fv * 4.0f, (float)id, 11) - 0.5f) * 0.35f - 0.3f - 0.4f * hash2(id, 31);
        c = cmul(c, 1.0f - 0.55f * clamp01(0.5f + (0.008f - fabsf(bend) * 0.5f) * ppu));
    }
    /* 가장자리: 위와 왼쪽은 빛을 받고 아래와 오른쪽은 그늘진다. 칸 단위의 거리로 잰다 */
    float wide = band ? 1.0f : 0.5f, el = fu * wide, er = (1.0f - fu) * wide, et = fv / 3.0f, eb = (1.0f - fv) / 3.0f;
    c = cmul(c, 1.0f + 0.24f * clamp01(0.5f + (0.03f - minf(el, et)) * ppu * 0.6f)
                     - 0.3f * clamp01(0.5f + (0.03f - minf(er, eb)) * ppu * 0.6f));
    if (lvl == 5 || lvl == 12) c = cmul(c, 1.0f - 0.3f * smooth(0.5f, 0.0f, fv));                /* 띠 밑의 그늘 */
    c = cmix(c, moss, smooth(0.38f, 0.0f, up - (g - 0.5f) * 0.5f) * 0.38f);
    c = cmul(c, 1.0f - 0.3f * smooth(0.2f, 0.0f, up) - 0.14f * smooth(0.15f, 0.0f, down));       /* 바닥과 천장에 닿는 곳의 그늘 */
    c = cmul(c, 1.0f - 0.5f * smooth(1.0f, WALL_H, up));                                         /* 위로 갈수록 어둡다 */
    return cmix(c, mortar, clamp01(0.5f + (0.016f - minf(minf(el, er), minf(et, eb))) * ppu));
}

/* 기둥: 세로 널빤지 넷이 천장까지 닿는다. 나무결이 세로로 흐르고 옹이가 있다. 위와 아래, 그리고 1.5칸마다 쇠띠를
 * 두르고 못을 박았다 */
static col_t plank(const hit_t *h, float v, float u)
{
    static const col_t wood = { 196, 136, 84 }, dark = { 96, 60, 36 }, iron = { 96, 102, 116 }, rivet = { 196, 200, 210 };
    float uu = h->at * 4.0f, board = floorf(uu), fu = uu - board, ppu = h->ppu, up = (1.0f - v) * WALL_H, down = v * WALL_H;
    if (board > 3.0f) board = 3.0f;
    int id = (int)board + h->mx * 31 + h->my * 17 + h->side * 7;
    float g = grain(u * 30.0f + (float)id, down * 2.2f, 41, ppu / 30.0f), ring = noise(u * 9.0f + (float)id * 3.0f, down * 1.2f, 43);
    col_t c = cmul(cmix(wood, dark, ring * 0.45f), (0.84f + 0.3f * hash2(id, 3)) * (0.78f + 0.5f * g));
    /* 옹이: 널빤지마다 하나꼴 */
    if (hash2(id, 13) > 0.3f) {
        float ku = fu - 0.3f - 0.4f * hash2(id, 14), kv = (down - 0.35f - (WALL_H - 0.8f) * hash2(id, 15)) * 3.0f, r = sqrtf(ku * ku + kv * kv);
        c = cmix(c, dark, clamp01(1.0f - r * 7.0f) * 0.8f);
    }
    float e = minf(fu, 1.0f - fu) * 0.25f;
    c = cmul(c, 1.0f + 0.16f * clamp01(0.5f + (0.012f - fu * 0.25f) * ppu) - 0.2f * clamp01(0.5f + (0.012f - (1.0f - fu) * 0.25f) * ppu));
    c = cmix(c, cmul(dark, 0.5f), clamp01(0.5f + (0.007f - e) * ppu));
    /* 쇠띠와 못 */
    float bt = fabsf(down - 0.09f), bb = fabsf(up - 0.09f);
    float bm = up > 0.75f && down > 0.75f ? fabsf(up - 1.5f * roundf(up / 1.5f)) : 9.0f, band = minf(bt, minf(bb, bm));
    float on = clamp01(0.5f + (0.045f - band) * ppu);
    if (on > 0.0f) {
        float sheen = 0.8f + 0.5f * clamp01(1.0f - band / 0.045f) * (bb <= band ? 0.7f : 1.0f);
        col_t m = cmul(iron, sheen * (0.86f + 0.3f * noise(u * 40.0f, down * 40.0f, 51)));
        float ru = fu - 0.5f, rr = sqrtf(ru * ru * 0.0625f + band * band);
        m = cmix(m, rivet, clamp01(0.5f + (0.014f - rr) * ppu));
        c = cmix(c, m, on);
    }
    return cmul(c, (1.0f - 0.26f * smooth(0.16f, 0.0f, up)) * (1.0f - 0.5f * smooth(1.0f, WALL_H, up)));
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
        float yt = MID - (WALL_H - EZ) * UNIT / d, yb = MID + EZ * UNIT / d;     /* 벽의 위 끝과 아래 끝 */
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

/* ---- 배경을 빨리 그리기 ---------------------------------------------------------------- */
/* 배경을 dst의 크기에 맞춰 그린다. 그리는 함수들이 보는 전역 값(화면의 크기, 지평선, 거리 1의 높이)을 dst에 맞췄다가
 * 되돌린다. zbuf에는 dst의 세로줄마다 벽까지의 거리가 남는다 */
static void render_background(image_t *dst, float px, float py, float dx, float dy)
{
    int vw = VW, vh = VH;
    float mid = MID, unit = UNIT;
    view_job_t job = { dst, px, py, dx, dy };
    VW = dst->w; VH = dst->h; MID = (float)dst->h * 0.5f; UNIT = (float)dst->w * UNIT_PER_W;
    par_for(VH, 8, ground_rows, &job);
    par_for(VW, 16, wall_columns, &job);
    VW = vw; VH = vh; MID = mid; UNIT = unit;
}

typedef struct { image_t *dst; const image_t *src; } double_job_t;

/* src의 줄들을 가로세로 두 배로 dst에 옮긴다 */
static void double_rows(int from, int to, void *arg)
{
    const double_job_t *j = arg;
    int w = j->src->w;
    for (int y = from; y < to; y++) {
        const uint8_t *s = j->src->px + (size_t)y * (size_t)w * 3;
        uint8_t *d = j->dst->px + (size_t)y * 2 * (size_t)j->dst->w * 3;
        for (int x = 0; x < w; x++, s += 3, d += 6) { d[0] = d[3] = s[0]; d[1] = d[4] = s[1]; d[2] = d[5] = s[2]; }
        d -= (size_t)w * 6;
        memcpy(d + (size_t)j->dst->w * 3, d, (size_t)w * 6);
    }
}

/* 배경을 im에 채운다. 배경은 눈의 자리, 방향, 높이로만 정해진다.
 *   눈이 지난 프레임과 달라졌다(움직이는 중) -> 절반 해상도로 그려 두 배로 키운다. 그릴 픽셀이 1/4이라 빠르고,
 *                                            같은 색이 두 픽셀씩 이어져서 보낼 양도 준다. 움직이는 동안은 거친 것이 보이지 않는다
 *   같은 눈으로 STILL_MS가 지났다(멈췄다)  -> 제 해상도로 그려서 따로 적어 둔다. 그 전까지는 절반 해상도의 것을 다시 쓴다
 *                                            (걷는 동안에도 틱 사이에는 눈이 같아서, 곧바로 제 해상도로 그리면 안 된다)
 *   적어 둔 것과 같다                      -> 적어 둔 것을 그대로 옮긴다(초파리만 움직일 때)
 * 적어 둘 그림을 못 만들면 늘 제 해상도로 그린다 */
static void draw_background(image_t *im, float px, float py, float ang, float dx, float dy)
{
    static image_t *kept, *half;
    static float k_px, k_py, k_ang, k_ez;
    static int k_ok, k_full;
    static double k_at;                              /* 눈이 마지막으로 달라진 때(ms) */
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    double now = (double)ts.tv_sec * 1000.0 + (double)ts.tv_nsec / 1e6;
    size_t bytes = (size_t)im->w * (size_t)im->h * 3;
    if (kept && (kept->w != im->w || kept->h != im->h)) { image_free(kept); image_free(half); kept = half = NULL; k_ok = 0; }
    if (!kept) kept = image_new(im->w, im->h);
    if (!half) half = image_new(im->w / 2, im->h / 2);
    if (!kept || !half || (im->w & 1) || (im->h & 1)) { render_background(im, px, py, dx, dy); return; }
    int same = k_ok && k_px == px && k_py == py && k_ang == ang && k_ez == EZ;
    if (same && k_full) {
        memcpy(im->px, kept->px, bytes);             /* zbuf도 그때 것 그대로다 */
    } else if (same && now - k_at >= STILL_MS) {
        render_background(im, px, py, dx, dy);
        memcpy(kept->px, im->px, bytes);
        k_full = 1;
    } else {
        double_job_t job = { im, half };
        if (!same) {
            render_background(half, px, py, dx, dy);
            for (int x = im->w - 1; x >= 0; x--) zbuf[x] = zbuf[x / 2];
            k_px = px; k_py = py; k_ang = ang; k_ez = EZ;
            k_at = now;
            k_ok = 1;
            k_full = 0;
        }
        par_for(half->h, 16, double_rows, &job);
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

/* ---- 세워 그리는 것들 ---------------------------------------------------------------- */
typedef struct { float x, y, depth, side; int kind, idx; } thing_t;   /* kind 0: 초파리, 1: 참가자, 2: 끈끈이 */

/* 바닥에 깔리는 둥근 얼룩(그림자, 끈끈이 웅덩이). (cx, fy)는 발밑, rx는 가로 반지름(픽셀) */
static void blot(image_t *im, const thing_t *t, float cx, float fy, float rx, col_t col, float strength)
{
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
            if (f < 1.0f) px_mix(im, x, y, col, strength * (1.0f - f));
        }
    }
}

static void shadow(image_t *im, const thing_t *t, float cx, float fy, float rx, float strength)
{
    blot(im, t, cx, fy, rx, (col_t){ 0, 0, 0 }, strength);
}

/* ---- 초파리 -------------------------------------------------------------------------- */
/* 초파리는 상자 몇 개로 된 모형이다: 몸통, 눈 둘, 입, 더듬이 둘, 다리 여섯, 날개 둘. 처음 구상한 그림
 * (ui/assets/fly_idle.spr: 네모난 몸, 양옆의 붉은 눈, 위로 뻗은 날개, 배의 줄무늬)을 그대로 입체로 옮긴 것이다.
 *
 * 그리는 법: 화면의 점마다 보는 방향으로 곧은 선을 긋고 가장 먼저 닿는 상자를 찾는다. 선들은 서로 나란하다(모형이
 * 작아서 원근을 넣지 않아도 어색하지 않다). 초파리가 향한 쪽과 내가 보는 쪽의 차이만큼 모형이 돌아가 있으므로 어느
 * 쪽에서 보든 이어진 모습이 나온다. 등과 날개가 보이도록 조금 위에서 내려다본다(FLY_TILT).
 * 좌표: 초파리의 앞이 +x, 오른쪽이 +y, 위가 +z이고 원점은 발밑이다. 단위는 칸.
 * 초파리는 평소에 낮게 떠서 날아다닌다(높이는 boss_view_fly_lift). 떠 있을 때는 다리를 접고 날개를 빠르게 친다.
 * 가장자리: 픽셀마다 두 점을 떠 보고, 둘이 다른 것에 닿았을 때만 네 점을 떠서 섞는다. */
#define FLY_TILT   0.20f             /* 같은 높이에서 볼 때 내려다보는 각도(라디안) */
#define FLY_BOXES  14

enum { FK_BODY = 0, FK_EYE, FK_MOUTH, FK_FEELER, FK_LEG, FK_WING };

typedef struct {
    int   kind, side;            /* side: 왼쪽 -1, 오른쪽 1(눈과 날개에서 쓴다) */
    float h[3];                  /* 반 크기 */
    /* 상자의 좌표로 옮겨 둔 것: 화면 가운데(발밑)를 지나는 선의 한 점, 화면의 오른쪽과 위, 선의 방향과 그 역수 */
    float o[3], ra[3], ub[3], d[3], inv[3];
    int   x0, x1, y0, y1;        /* 화면에서 차지하는 네모 */
} fbox_t;

typedef struct {
    image_t *im;
    float depth, cx, fy, S;      /* 거리, 발밑의 화면 자리, 한 칸의 픽셀 수 */
    int   n, x0, x1, y0, y1, stun, tick;
    float charge, flash, vis, lamp;
    fbox_t box[FLY_BOXES];
} fly_job_t;

/* 한 점에서 닿은 것. solid는 닿은 상자(없으면 -1), wing[k]는 날개 k에 닿았는가(날개는 비쳐 보여서 따로 둔다) */
typedef struct {
    int   solid, face, wing[2], wface[2];
    float t, q[3], wt[2], wq[2][3];
} fsample_t;

/* 상자 하나를 놓는다. c는 가운데, h는 반 크기, roll은 x축을 따라 돌린 각도다(날개가 들리는 각도).
 * rv, up, dir은 모형의 좌표로 본 화면의 오른쪽, 위, 보는 방향이다 */
static void fly_box(fly_job_t *j, int kind, int side, const float c[3], const float h[3], float roll,
                    const float rv[3], const float up[3], const float dir[3])
{
    fbox_t *B = &j->box[j->n++];
    float cr = cosf(roll), sr = sinf(roll);
    B->kind = kind;
    B->side = side;
    #define TO_BOX(dst, v) do { (dst)[0] = (v)[0]; (dst)[1] = cr * (v)[1] + sr * (v)[2]; (dst)[2] = -sr * (v)[1] + cr * (v)[2]; } while (0)
    float neg[3] = { -c[0], -c[1], -c[2] };
    TO_BOX(B->o, neg);
    TO_BOX(B->ra, rv);
    TO_BOX(B->ub, up);
    TO_BOX(B->d, dir);
    #undef TO_BOX
    float lo_x = 1e9f, hi_x = -1e9f, lo_y = 1e9f, hi_y = -1e9f;
    for (int i = 0; i < 3; i++) {
        B->h[i] = h[i];
        B->inv[i] = fabsf(B->d[i]) < 1e-6f ? 0.0f : 1.0f / B->d[i];
    }
    for (int k = 0; k < 8; k++) {                    /* 여덟 꼭짓점이 화면의 어디에 오는가 */
        float bx = (k & 1) ? h[0] : -h[0], by = (k & 2) ? h[1] : -h[1], bz = (k & 4) ? h[2] : -h[2];
        float p[3] = { c[0] + bx, c[1] + cr * by - sr * bz, c[2] + sr * by + cr * bz };
        float a = p[0] * rv[0] + p[1] * rv[1] + p[2] * rv[2], b = p[0] * up[0] + p[1] * up[1] + p[2] * up[2];
        float sx = j->cx + a * j->S, sy = j->fy - b * j->S;
        if (sx < lo_x) lo_x = sx;
        if (sx > hi_x) hi_x = sx;
        if (sy < lo_y) lo_y = sy;
        if (sy > hi_y) hi_y = sy;
    }
    B->x0 = (int)floorf(lo_x) - 1; B->x1 = (int)ceilf(hi_x) + 1;
    B->y0 = (int)floorf(lo_y) - 1; B->y1 = (int)ceilf(hi_y) + 1;
    if (B->x0 < j->x0) j->x0 = B->x0;
    if (B->x1 > j->x1) j->x1 = B->x1;
    if (B->y0 < j->y0) j->y0 = B->y0;
    if (B->y1 > j->y1) j->y1 = B->y1;
}

/* 화면의 점 (sx, sy)에서 그은 선이 닿는 것을 찾는다. (px, py)는 그 점이 든 픽셀이다.
 * 무엇에 닿았는지를 한 수로 돌려준다(아무것도 없으면 0). 두 점의 수가 같으면 같은 면을 보고 있는 것이다 */
static int fly_cast(const fly_job_t *j, int px, int py, float sx, float sy, fsample_t *s)
{
    float a = (sx - j->cx) / j->S, b = (j->fy - sy) / j->S;
    s->solid = -1;
    s->face = 0;
    s->t = 1e9f;
    s->wing[0] = s->wing[1] = 0;
    for (int k = 0; k < j->n; k++) {
        const fbox_t *B = &j->box[k];
        if (px < B->x0 || px >= B->x1 || py < B->y0 || py >= B->y1) continue;
        float o[3], tn = -1e9f, tf = 1e9f;
        int ax = 0, miss = 0;
        for (int i = 0; i < 3 && !miss; i++) {
            o[i] = a * B->ra[i] + b * B->ub[i] + B->o[i];
            if (B->inv[i] == 0.0f) { miss = fabsf(o[i]) > B->h[i]; continue; }
            float t1 = (-B->h[i] - o[i]) * B->inv[i], t2 = (B->h[i] - o[i]) * B->inv[i];
            if (t1 > t2) { float tmp = t1; t1 = t2; t2 = tmp; }
            if (t1 > tn) { tn = t1; ax = i; }
            if (t2 < tf) tf = t2;
            miss = tn > tf;
        }
        if (miss) continue;
        int face = ax * 2 + (B->d[ax] < 0.0f);       /* 들어간 면: 축과, 그 축의 +쪽 면인가 */
        if (B->kind == FK_WING) {
            int w = B->side > 0;
            s->wing[w] = 1;
            s->wface[w] = face;
            s->wt[w] = tn;
            for (int i = 0; i < 3; i++) s->wq[w][i] = o[i] + tn * B->d[i];
        } else if (tn < s->t) {
            s->solid = k;
            s->face = face;
            s->t = tn;
            for (int i = 0; i < 3; i++) s->q[i] = o[i] + tn * B->d[i];
        }
    }
    return (s->solid + 1) * 64 + s->face * 4 + s->wing[0] + s->wing[1] * 2;
}

/* 닿은 면이 받는 빛. 빛은 화면에서 볼 때 왼쪽 위 앞에서 온다 */
static inline float fly_light(const fbox_t *B, int face)
{
    int ax = face >> 1;
    float sg = (face & 1) ? 1.0f : -1.0f;
    float diff = sg * (-0.42f * B->ra[ax] + 0.68f * B->ub[ax] - 0.60f * B->d[ax]);
    return 0.50f + 0.60f * (diff > 0.0f ? diff : 0.0f);
}

/* x가 a와 b 사이에 있는 정도(0~1). fw는 픽셀 하나의 크기(칸)다 */
static inline float within(float x, float a, float b, float fw)
{
    return clamp01(0.5f + (x - a) / fw) * clamp01(0.5f + (b - x) / fw);
}

/* 한 점의 색. 색에는 덮는 정도가 곱해져 있다 */
static acc_t fly_shade(const fly_job_t *j, const fsample_t *s)
{
    static const col_t amber = { 218, 164, 86 }, cream = { 244, 218, 164 }, dark = { 60, 42, 32 }, pink = { 246, 150, 150 };
    static const col_t red = { 216, 40, 46 }, red_rim = { 118, 16, 26 }, red_in = { 164, 22, 32 }, hot = { 255, 92, 52 };
    static const col_t limb = { 50, 36, 30 }, lip = { 104, 64, 44 }, grey = { 128, 128, 136 }, white = { 255, 255, 255 };
    static const col_t glass = { 212, 234, 248 }, vein = { 146, 180, 208 };
    acc_t o = { 0, 0, 0, 0 };
    float fw = 1.0f / j->S;
    if (s->solid >= 0) {
        const fbox_t *B = &j->box[s->solid];
        int ax = s->face >> 1, pos = s->face & 1, ua = (ax + 1) % 3, va = (ax + 2) % 3;
        float e = minf(B->h[ua] - fabsf(s->q[ua]), B->h[va] - fabsf(s->q[va]));      /* 면의 가장자리까지 */
        float light = fly_light(B, s->face), x = s->q[0], y = s->q[1], z = s->q[2];
        col_t c;
        if (B->kind == FK_BODY) {
            c = ax == 2 ? (pos ? amber : cream) : cmix(amber, cream, smooth(-0.08f, -0.26f, z));     /* 배 쪽은 밝다 */
            if (ax != 0)                                 /* 뒤쪽 절반에 짙은 줄 둘. 등, 옆, 배를 한 바퀴 돈다 */
                c = cmix(c, dark, maxf(within(x, -0.41f, -0.28f, fw), within(x, -0.16f, -0.03f, fw)) * 0.94f);
            else if (!pos)
                c = cmix(c, dark, 0.86f);                /* 꽁무니 */
            else {                                       /* 얼굴: 눈 아래의 볼 */
                float dy = fabsf(fabsf(y) - 0.21f) - 0.065f, dz = fabsf(z + 0.14f) - 0.032f;
                c = cmix(c, pink, clamp01(0.5f - maxf(dy, dz) / fw) * 0.8f);
            }
            if (j->stun) c = cmix(c, grey, 0.5f);
            c = cmul(c, 1.0f - 0.5f * clamp01(0.5f + (0.02f - e) / fw));              /* 모서리의 짙은 선 */
        } else if (B->kind == FK_EYE) {
            c = red;
            if (ax != 2) {                               /* 가운데가 짙고, 바깥 위쪽에 반짝이는 점 */
                float in_u = ax == 0 ? y : x, sgn = ax == 0 ? (float)B->side : 1.0f;
                float di = maxf(fabsf(in_u) - B->h[ua < va ? ua : va] * 0.55f, fabsf(z + 0.01f) - B->h[2] * 0.55f);
                c = cmix(c, red_in, clamp01(0.5f - di / fw));
                if (j->stun) {                           /* 붙었거나 막혔다: 눈이 X가 된다 */
                    float k = B->h[2] / B->h[ua < va ? ua : va], cross = minf(fabsf(in_u * k - z), fabsf(in_u * k + z));
                    c = cmix(grey, dark, clamp01(0.5f + (0.022f - cross * 0.7f) / fw));
                } else {
                    float dh = maxf(fabsf(in_u - sgn * 0.045f) - 0.032f, fabsf(z - 0.065f) - 0.036f);
                    c = cmix(c, white, clamp01(0.5f - dh / fw) * 0.92f);
                }
            }
            if (!j->stun) c = cmix(c, hot, j->charge);
            c = cmix(c, j->stun ? dark : red_rim, clamp01(0.5f + (0.024f - e) / fw));
            if (j->charge > 0.0f) light = light + (1.15f - light) * j->charge;       /* 돌진하려 할 때는 눈이 빛난다 */
        } else if (B->kind == FK_MOUTH) {
            c = lip;
        } else {
            c = limb;
        }
        if (B->kind != FK_EYE) c = cmix(c, hot, j->charge * 0.38f);
        c = cmul(c, light * j->lamp);
        c = cmix(c, white, j->flash);
        c = cmix(FOG, c, j->vis);
        o = (acc_t){ c.r, c.g, c.b, 1.0f };
    }
    /* 날개는 비쳐 보인다. 몸보다 앞에 있는 것만, 먼 것부터 얹는다 */
    int first = s->wing[0] && s->wing[1] && s->wt[0] < s->wt[1] ? 1 : 0;
    for (int n = 0; n < 2; n++) {
        int w = n == 0 ? first : 1 - first;
        if (!s->wing[w] || (s->solid >= 0 && s->wt[w] >= s->t)) continue;
        const fbox_t *B = &j->box[j->n - 2 + w];             /* 날개 둘은 맨 끝에 놓는다(왼쪽, 오른쪽) */
        int ax = s->wface[w] >> 1;
        float x = s->wq[w][0], y = s->wq[w][1], alpha = 0.46f;
        col_t c = glass;
        if (ax == 2) {
            float e = minf(B->h[0] - fabsf(x), B->h[1] - fabsf(y));
            float rim = clamp01(0.5f + (0.022f - e) / fw);
            float line = maxf(clamp01(0.5f + (0.008f - fabsf(x - 0.09f - y * (float)B->side * 0.25f)) / fw),
                              clamp01(0.5f + (0.008f - fabsf(x + 0.10f + y * (float)B->side * 0.25f)) / fw));
            c = cmix(c, vein, maxf(rim, line * 0.8f));
            alpha += 0.40f * rim + 0.22f * line;
        } else {
            c = vein;
            alpha = 0.85f;
        }
        c = cmul(c, (0.74f + 0.34f * fly_light(B, s->wface[w])) * j->lamp);
        c = cmix(c, white, j->flash);
        over(&o, cmix(FOG, c, j->vis), alpha);
    }
    return o;
}

static void fly_rows(int from, int to, void *arg)
{
    static const col_t hot = { 255, 80, 50 };
    const fly_job_t *j = arg;
    float mid_y = j->fy - 0.62f * j->S, glow_r = 1.05f * j->S;
    for (int y = j->y0 + from; y < j->y0 + to; y++) {
        for (int x = j->x0; x < j->x1; x++) {
            if (j->depth >= zbuf[x]) continue;
            fsample_t a, b;
            acc_t o = { 0, 0, 0, 0 };
            int ia = fly_cast(j, x, y, (float)x + 0.25f, (float)y + 0.25f, &a);
            int ib = fly_cast(j, x, y, (float)x + 0.75f, (float)y + 0.75f, &b);
            if (ia == ib) {
                if (ia) o = fly_shade(j, &a);
            } else {                                 /* 가장자리: 네 점을 섞는다 */
                fsample_t c, d;
                fly_cast(j, x, y, (float)x + 0.75f, (float)y + 0.25f, &c);
                fly_cast(j, x, y, (float)x + 0.25f, (float)y + 0.75f, &d);
                acc_t p[4] = { fly_shade(j, &a), fly_shade(j, &b), fly_shade(j, &c), fly_shade(j, &d) };
                for (int k = 0; k < 4; k++) { o.r += p[k].r * 0.25f; o.g += p[k].g * 0.25f; o.b += p[k].b * 0.25f; o.a += p[k].a * 0.25f; }
            }
            if (j->charge > 0.0f && o.a < 1.0f) {     /* 돌진하려 한다: 몸 둘레에 붉은 기운 */
                float gx = ((float)x + 0.5f - j->cx) / glow_r, gy = ((float)y + 0.5f - mid_y) / glow_r, r = sqrtf(gx * gx + gy * gy);
                if (r < 1.0f) {
                    float g = (1.0f - r) * 0.5f * j->charge * (1.0f - o.a);
                    o.r += hot.r * g; o.g += hot.g * g; o.b += hot.b * g; o.a += g;
                }
            }
            px_over(j->im, x, y, &o);
        }
    }
}

static void draw_fly(image_t *im, const boss_snap_t *b, const boss_view_t *v, const thing_t *t, float eye_x, float eye_y)
{
    static int stun_full;                            /* 이번 멈춤의 처음 길이(남은 틱에서 알아낸다) */
    float scale = UNIT / t->depth, cx = (float)VW * 0.5f * (1.0f + t->side / t->depth), fy = MID + EZ * scale;
    int stun = b->fly.stun, tick = b->tick;
    stun_full = stun > stun_full ? stun : (stun == 0 ? 0 : stun_full);
    float lift = boss_view_fly_lift(b);              /* 떠 있는 높이 */
    int flying = lift > 0.08f;
    shadow(im, t, cx, fy, scale * (0.62f - 0.12f * clamp01(lift)), 0.5f - 0.2f * clamp01(lift));
    if (stun > 0 && stun_full > 30)                  /* 끈끈이에 붙었다(막혀서 멈춘 것은 짧다) */
        blot(im, t, cx, fy, scale * 0.8f, (col_t){ 240, 140, 40 }, 0.9f);

    /* 모형의 좌표로 본 화면의 오른쪽, 위, 보는 방향. 초파리의 앞은 (hx, hy), 오른쪽은 (-hy, hx)다 */
    float fa = (float)b->fly.ang * TWO_PI / 65536.0f, hx = cosf(fa), hy = sinf(fa);
    float vx = t->x - eye_x, vy = t->y - eye_y, len = sqrtf(vx * vx + vy * vy);
    if (len < 1e-4f) { vx = 1.0f; vy = 0.0f; len = 1.0f; }
    vx /= len; vy /= len;
    /* 내려다보는 각도: 기본 기울기에, 내 눈이 초파리보다 높은 만큼을 더한다(내가 뛰어오르면 등이 더 보이고,
     * 초파리가 떠오르면 배가 보인다) */
    float tilt = FLY_TILT + atan2f(EZ - BOSS_VIEW_EYE_H - lift, t->depth);
    tilt = tilt < -0.7f ? -0.7f : (tilt > 0.9f ? 0.9f : tilt);
    float dvx = vx * hx + vy * hy, dvy = -vx * hy + vy * hx, cp = cosf(tilt), sp = sinf(tilt);
    const float dir[3] = { dvx * cp, dvy * cp, -sp }, rv[3] = { -dvy, dvx, 0.0f }, up[3] = { dvx * sp, dvy * sp, cp };

    static fly_job_t job;
    int charging = b->fly.windup || b->fly.charging;
    job.im = im;
    job.depth = t->depth;
    job.cx = cx;
    job.fy = fy - lift * scale;
    job.S = scale;
    job.n = 0;
    job.x0 = job.y0 = 1 << 30;
    job.x1 = job.y1 = -(1 << 30);
    job.stun = stun > 0;
    job.tick = tick;
    job.charge = charging ? 0.55f + 0.45f * sinf((float)tick * 1.6f) : 0.0f;
    job.flash = v->hit_ms >= 0 && v->hit_ms < 160 ? 0.7f * (1.0f - (float)v->hit_ms / 160.0f) : 0.0f;
    job.vis = 0.35f + 0.65f * expf(-t->depth * FOG_K);
    job.lamp = lamp((int)cx < 0 ? 0 : ((int)cx >= VW ? VW - 1 : (int)cx));

    /* 상자들. 몸통의 가운데는 높이 0.64에 있다(다리가 0.30) */
    const float bz = 0.64f, wig = stun ? 0.0f : 0.035f * sinf((float)tick * 0.9f);
    float leg = flying ? 0.14f : 0.30f;              /* 떠 있을 때는 다리를 접는다 */
    fly_box(&job, FK_BODY, 0, (float[3]){ 0.0f, 0.0f, bz }, (float[3]){ 0.50f, 0.40f, 0.34f }, 0.0f, rv, up, dir);
    for (int s = -1; s <= 1; s += 2)
        fly_box(&job, FK_EYE, s, (float[3]){ 0.47f, (float)s * 0.27f, bz + 0.09f }, (float[3]){ 0.07f, 0.15f, 0.16f }, 0.0f, rv, up, dir);
    fly_box(&job, FK_MOUTH, 0, (float[3]){ 0.52f, 0.0f, bz - 0.18f }, (float[3]){ 0.03f, 0.07f, 0.055f }, 0.0f, rv, up, dir);
    for (int s = -1; s <= 1; s += 2)
        fly_box(&job, FK_FEELER, s, (float[3]){ 0.44f, (float)s * 0.11f, bz + 0.34f + 0.07f }, (float[3]){ 0.016f, 0.016f, 0.07f }, 0.0f, rv, up, dir);
    for (int k = 0; k < 6; k++) {
        float lx = 0.30f - 0.30f * (float)(k / 2) + ((k / 2 + k) & 1 ? wig : -wig), ly = (k & 1) ? 0.31f : -0.31f;
        fly_box(&job, FK_LEG, 0, (float[3]){ lx, ly, bz - 0.34f - leg * 0.5f }, (float[3]){ 0.045f, 0.045f, leg * 0.5f }, 0.0f, rv, up, dir);
    }
    /* 날개: 등의 양옆에 붙어 바깥으로 뻗는다. 떠 있을 때는 빠르게 치고(틱마다 올렸다 내렸다), 내려앉으면 접는다 */
    float rise = stun ? 0.05f : (charging ? ((tick & 1) ? 0.70f : 0.40f) : (flying ? ((tick & 1) ? 0.85f : 0.10f) : 0.24f));
    for (int s = -1; s <= 1; s += 2) {
        float roll = (float)s * rise, cr = cosf(roll), sr = sinf(roll), oy = (float)s * 0.22f;
        const float hinge[3] = { -0.07f, (float)s * 0.17f, bz + 0.34f + 0.014f };
        fly_box(&job, FK_WING, s, (float[3]){ hinge[0], hinge[1] + cr * oy, hinge[2] + sr * oy }, (float[3]){ 0.28f, 0.22f, 0.012f }, roll, rv, up, dir);
    }

    if (job.x0 < 0) job.x0 = 0;
    if (job.x1 > VW) job.x1 = VW;
    if (job.y0 < VY) job.y0 = VY;
    if (job.y1 > VY + VH) job.y1 = VY + VH;
    if (job.charge > 0.0f) {                         /* 붉은 기운이 모형보다 넓게 번진다 */
        int gx0 = (int)(cx - 1.05f * scale), gx1 = (int)(cx + 1.05f * scale) + 1;
        int gy0 = (int)(job.fy - 1.67f * scale), gy1 = (int)(job.fy + 0.43f * scale) + 1;
        if (gx0 < job.x0) job.x0 = gx0 < 0 ? 0 : gx0;
        if (gx1 > job.x1) job.x1 = gx1 > VW ? VW : gx1;
        if (gy0 < job.y0) job.y0 = gy0 < VY ? VY : gy0;
        if (gy1 > job.y1) job.y1 = gy1 > VY + VH ? VY + VH : gy1;
    }
    if (job.x1 > job.x0 && job.y1 > job.y0) par_for(job.y1 - job.y0, 8, fly_rows, &job);
}

/* 다른 참가자: 색 옷을 입은 둥근 기둥에 머리. 쓰러졌으면 바닥에 납작하게 */
static void draw_player(image_t *im, const boss_snap_t *b, const thing_t *t)
{
    static const col_t skin = { 235, 188, 140 }, down = { 110, 115, 130 };
    float scale = UNIT / t->depth, cx = (float)VW * 0.5f * (1.0f + t->side / t->depth), fy = MID + EZ * scale;
    int alive = b->p[t->idx].alive;
    float by = fy - (alive ? (float)b->p[t->idx].z / 100.0f : 0.0f) * scale;        /* 뛰어올랐으면 그만큼 위에 그린다 */
    rgb_t pc = PCOL[t->idx % BOSS_PLAYERS];
    col_t cloth = { (float)pc.r, (float)pc.g, (float)pc.b };
    shadow(im, t, cx, fy, 0.3f * scale, 0.45f);
    int x0 = (int)floorf(cx - 0.45f * scale), x1 = (int)ceilf(cx + 0.45f * scale);
    int y0 = (int)floorf(by - 1.1f * scale), y1 = (int)ceilf(fy + 1.0f);
    if (x0 < 0) x0 = 0;
    if (x1 > VW) x1 = VW;
    if (y0 < VY) y0 = VY;
    if (y1 > VY + VH) y1 = VY + VH;
    for (int x = x0; x < x1; x++) {
        if (t->depth >= zbuf[x]) continue;
        float u = ((float)x + 0.5f - cx) / scale;                    /* 칸 단위. 발밑이 (0, 0)이고 위가 음수다 */
        for (int y = y0; y < y1; y++) {
            float w = ((float)y + 0.5f - by) / scale, spec;
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
    float scale = UNIT / t->depth, cx = (float)VW * 0.5f * (1.0f + t->side / t->depth), cy = MID + (EZ - 0.4f) * scale;
    float r = maxf(0.12f * scale, 1.5f);
    shadow(im, t, cx, MID + EZ * scale, r * 0.9f, 0.4f);
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
    const float cell = 76.8f / BOSS_MAP, size = BOSS_MAP * cell, x = HW - size - 14.0f, y = 30.0f;
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
    float lw = text(im, 20.0f, y + 21.0f, "R", KEY, 0.75f, 1.0f) + 3.0f;
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
    static const char *const part[][2] = { { "WASD", "이동" }, { "마우스", "시선" }, { "클릭", "쓰기" }, { "SPACE", "점프" },
                                           { "R", "막기" }, { "C", "살금살금" }, { "H", "안내" } };
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
    if (eye >= 0 && b->fly.windup && b->fly.target == eye) { note = "돌진이 온다!  R 막기  SPACE 점프"; note_col = hd_rgb(255, 90, 80); }
    else if (v->block_ms >= 0 && v->block_ms < 700) { note = "막았다!"; note_col = hd_rgb(85, 200, 120); }
    else if (v->dodge_ms >= 0 && v->dodge_ms < 700) { note = "뛰어넘었다!"; note_col = hd_rgb(85, 200, 120); }
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
    EZ = BOSS_VIEW_EYE_H;
    if (eye >= 0) {
        float px = (float)b->p[eye].x / BOSS_FIX, py = (float)b->p[eye].y / BOSS_FIX;
        /* 내 눈이면 마우스로 돌린 방향을 쓴다. 서버가 알려 준 방향은 한두 틱 늦다 */
        eye_ang = (float)(eye == v->me && v->look_on ? v->look_ang : b->p[eye].ang) * TWO_PI / 65536.0f;
        EZ += (float)b->p[eye].z / 100.0f;           /* 뛰어오른 만큼 눈이 올라간다 */
        float dx = cosf(eye_ang), dy = sinf(eye_ang);
        draw_background(im, px, py, eye_ang, dx, dy);
        draw_things(im, b, v, eye, px, py, dx, dy);
    } else {
        hd_fill(im, hd_rgb(14, 16, 28));                 /* 볼 사람이 없다 */
    }
    draw_hud(im, b, v, eye, eye_ang);
}
