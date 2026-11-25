/* 보스전 그리기. 화면은 위에서부터 상태 줄(초파리 체력, 남은 시간), 3D 화면, 내 상태 줄이다.
 *
 * 벽: 세로줄마다 광선의 방향을 정하고, 격자의 칸 경계를 하나씩 건너가며 처음 만나는 벽을 찾는다.
 *     벽까지의 거리는 눈에서 화면에 수직으로 잰다(비스듬히 재면 벽이 둥글게 휜다). 줄의 높이는 거리에 반비례한다.
 *     세로줄마다 그 거리를 적어 두고(zbuf), 뒤에 그리는 것들이 벽에 가려지는지 판단하는 데 쓴다.
 * 초파리와 참가자: 눈을 기준으로 한 좌표로 옮겨 화면의 가로 위치와 크기를 정하고, 먼 것부터 그린다.
 *
 * 그리는 면은 둘이다.
 *   3D(벽, 바닥, 초파리, 참가자)는 받은 화면의 픽셀 그대로 그린다. 화면이 클수록(배율이 높을수록) 촘촘하다.
 *   그 위에 얹는 것(상태 줄, 글자, 지도, 뇌 표시, 무기)은 "얹는 면"에 그린다. 얹는 면의 픽셀 하나는 화면의
 *   hud x hud 픽셀이다(배율 3 이상이면 2, 아니면 1). 글자는 늘 얹는 면의 픽셀로 그리므로, 화면이 커지면 글자와
 *   상태 줄이 화면에 비해 작아지고 3D 화면이 넓어진다.
 * 얹는 면의 크기는 240x134(배율 1)부터 720x402(배율 6)까지 달라진다. 그래서 자리는 모두 가장자리에서 재고,
 * 그림(지도, 뇌 표시, 무기)은 면이 넓으면 2배로 그린다(BIG). */
#include "boss/boss_view.h"
#include "brain.h"
#include "ui/ui_internal.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

#define BAR_TOP     14               /* 위 상태 줄의 높이(얹는 면의 픽셀) */
#define BAR_BOT     16               /* 아래 상태 줄의 높이 */
#define PLANE       0.66f            /* 시야의 넓이(약 66도) */
#define TWO_PI      6.2831853f
#define UNIT_PER_W  (104.0f / 240.0f)    /* 거리 1에 있는 벽의 높이 = 화면 너비 x 이 값. 배율 1에서 104픽셀이다 */
#define SHADE_STEP  1.6f             /* 이만큼 멀어질 때마다 한 단계 어두워진다(칸) */

static uint8_t wall[BOSS_MAP][BOSS_MAP];
static int     have_map;
static float   zbuf[SCREEN_W * PRESENT_MAX_SCALE];
/* 지금 그리는 면: 크기(CW x CH), 그 안의 3D 화면 자리(위쪽 끝 VY, 너비 VW, 높이 VH), 거리 1에 있는 벽의 높이(UNIT),
 * 그림을 몇 배로 그리는가(BIG). set_view로 바꾼다 */
static int     CW = SCREEN_W, CH = SCREEN_H, VY = BAR_TOP, VW = SCREEN_W, VH = SCREEN_H - BAR_TOP - BAR_BOT, BIG = 1;
static float   UNIT = SCREEN_W * UNIT_PER_W;
static float   EZ = BOSS_VIEW_EYE_H;             /* 눈의 높이(칸). 뛰어오르면 높아진다 */

/* w x h인 면에 그린다. bar는 상태 줄을 그 면의 픽셀로 몇 배로 치는가(3D를 그릴 때는 hud, 얹는 면에서는 1) */
static void set_view(int w, int h, int bar)
{
    CW = w;
    CH = h;
    VY = BAR_TOP * bar;
    VW = w;
    VH = h - (BAR_TOP + BAR_BOT) * bar;
    UNIT = (float)w * UNIT_PER_W;
    BIG = w / bar >= 440 ? 2 : 1;
}
static const color_t pcol[BOSS_PLAYERS] = { C_CYAN, C_GREEN, C_PINK };

/* 벽의 배치는 계산부가 정한다. 한 번만 받아 온다 */
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

static void px_view(canvas_t *c, int x, int y, color_t col)
{
    if (x >= 0 && x < VW && y >= VY && y < VY + VH) ui_px(c, x, y, col);
}

static float clamp01(float v) { return v < 0.0f ? 0.0f : (v > 1.0f ? 1.0f : v); }

/* 초파리가 떠 있는 높이(칸). 초파리는 평소에 땅에서 다리로 걷는다(boss/body.c). 계산부는 평면에서만 계산하므로
 * 높이는 그리는 쪽이 상태에서 정한다: 걷거나 서 있을 때는 바닥에 있고, 돌진할 때는 낮게 떠서 달려들고,
 * 달아날 때(도약)는 높이 솟아 날아가다가 내려앉는다. 틱이 바뀔 때마다 목표 높이를 조금씩 따라가서 뚝뚝 끊기지 않는다.
 * 떠 있는 동안은 위아래로 조금 흔들린다 */
float boss_view_fly_lift(const boss_snap_t *b)
{
    static float shown = 0.0f;
    static int last_tick = -1, air_full;             /* air_full: 이번 도약의 처음 길이(남은 틱에서 알아낸다) */
    int air = b->fly.air;
    float want = 0.0f;
    air_full = air > air_full ? air : (air == 0 ? 0 : air_full);
    if (b->fly.hp <= 0 || b->fly.stun || b->fly.feeding) want = 0.0f;      /* 먹을 때는 먹이에 내려앉는다 */
    else if (b->fly.charging) want = 0.12f;
    else if (air > 0 && air_full > 0) {
        /* 세 틱에 걸쳐 솟고, 날아가는 동안 그 높이로 가고, 마지막 세 틱(내려앉는 동안)에 내려온다. 언제 내려앉을지는
         * 뇌의 착지 명령이 정하므로 미리 알 수 없다 */
        float up = (float)(air_full - air + 1) / 3.0f, down = (float)air / (float)BOSS_VIEW_LAND_TICKS;
        float t = up < down ? up : down;
        want += 1.2f * (t > 1.0f ? 1.0f : t);
    }
    if ((int)b->tick != last_tick) {
        shown = last_tick < 0 || (int)b->tick < last_tick ? want : shown + (want - shown) * 0.4f;     /* 새 판이면 바로 */
        last_tick = (int)b->tick;
    }
    return shown + (want > 0.2f ? 0.06f * sinf((float)b->tick * 0.45f) : 0.0f);
}

/* ---- 벽 ---------------------------------------------------------------------------- */
/* 거리에 따른 밝기 단계(0이 가장 밝다). 단계가 n개다 */
static int shade_of(float d, int n)
{
    int k = (int)(d / SHADE_STEP);
    return k < 0 ? 0 : (k >= n ? n - 1 : k);
}

static color_t wall_color(int pillar, int side, float d, int seam)
{
    int k = shade_of(d, CX_SHADES) + side + (seam ? 1 : 0);      /* 옆면과 칸의 이음매는 한 단계 어둡게 */
    if (k >= CX_SHADES) k = CX_SHADES - 1;
    return (color_t)((pillar ? CX_PILL0 : CX_WALL0) + k);
}

static void draw_walls(canvas_t *c, float px, float py, float dx, float dy)
{
    float plx = -dy * PLANE, ply = dx * PLANE;
    /* 천장과 바닥은 화면의 줄마다 색이 정해진다(돌거나 걸어도 바뀌지 않는다). 지평선에 가까운 줄일수록 먼 곳이라
     * 어둡다. 지평선에서 r픽셀 아래의 줄은 눈높이 x UNIT / r, r픽셀 위의 줄은 (벽 높이 - 눈높이) x UNIT / r 만큼 먼 곳이다 */
    int mid = VY + VH / 2;
    for (int y = VY; y < VY + VH; y++) {
        float d = UNIT * (y < mid ? (BOSS_VIEW_WALL_H - EZ) / (float)(mid - y) : EZ / (float)(y - mid + 1));
        if (y < mid) ui_rect(c, 0, y, VW, 1, (color_t)(CX_CEIL0 + shade_of(d * 1.3f, CX_CEILS)));
        else         ui_rect(c, 0, y, VW, 1, (color_t)(CX_FLOOR0 + shade_of(d, CX_FLOORS)));
    }
    for (int x = 0; x < VW; x++) {
        float cam = 2.0f * (float)x / (float)VW - 1.0f;
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
        float at = side == 0 ? py + d * ry : px + d * rx;        /* 벽의 어디에 닿았는가(이음매를 긋는 데 쓴다) */
        at -= floorf(at);
        int y0 = mid - (int)((BOSS_VIEW_WALL_H - EZ) * UNIT / d), y1 = mid + (int)(EZ * UNIT / d);     /* 벽의 위 끝과 아래 끝 */
        if (y0 < VY) y0 = VY;
        if (y1 > VY + VH) y1 = VY + VH;
        int pillar = mx > 0 && my > 0 && mx < BOSS_MAP - 1 && my < BOSS_MAP - 1;
        ui_rect(c, x, y0, 1, y1 - y0, wall_color(pillar, side, d, at < 0.04f || at > 0.96f));
    }
}

/* ---- 세워 그리는 것들 ---------------------------------------------------------------- */
typedef struct { float x, y, depth, side; int kind, idx; } thing_t;   /* kind 0: 초파리, 1: 참가자, 2: 끈끈이 */

/* 초파리의 한 점. u, v는 -1~1(가운데가 0, v는 아래가 +). 색이 없으면 C_NONE.
 * face: 0 정면(눈 둘), 1 뒤(날개가 등을 덮음), 2 옆(turn이 -1이면 왼쪽을 본다) */
static color_t fly_pixel(float u, float v, int face, int turn, color_t body, int stuck)
{
    float au = fabsf(u);
    if (stuck && v > 0.62f && au < 0.7f && (u * u) / 0.49f + (v - 0.8f) * (v - 0.8f) / 0.04f < 1.0f) return C_ORANGE;
    if (face == 2) {                                  /* 옆모습: 머리가 한쪽 끝에 있다 */
        float hu = u - 0.55f * (float)turn, bu = u + 0.1f * (float)turn;
        if (hu * hu / 0.06f + (v + 0.1f) * (v + 0.1f) / 0.06f < 1.0f) {
            float eu = hu - 0.06f * (float)turn;
            return eu * eu + (v + 0.14f) * (v + 0.14f) < 0.02f ? C_FLY_EYE : C_BROWN;
        }
        if (bu * bu / 0.36f + (v - 0.12f) * (v - 0.12f) / 0.09f < 1.0f) return body;
        float wu = u + 0.25f * (float)turn;
        if (wu * wu / 0.3f + (v + 0.22f) * (v + 0.22f) / 0.03f < 1.0f) return C_WING;
        if (v > 0.35f && v < 0.75f && fabsf(fmodf(au * 5.0f, 1.0f) - 0.5f) < 0.08f && au < 0.55f) return C_BLACK;
        return C_NONE;
    }
    /* 정면과 뒤: 머리, 몸통, 날개, 다리 */
    if (face == 0) {
        float e = (au - 0.17f) * (au - 0.17f) + (v + 0.45f) * (v + 0.45f);
        if (e < 0.017f) return C_FLY_EYE;
        if (u * u + (v + 0.42f) * (v + 0.42f) < 0.068f) return C_BROWN;
    }
    if (u * u / 0.14f + (v - 0.15f) * (v - 0.15f) / 0.25f < 1.0f) {
        if (face == 1 && u * u / 0.09f + (v - 0.1f) * (v - 0.1f) / 0.3f < 1.0f)
            return (((int)(u * 40.0f) + (int)(v * 40.0f)) & 1) ? C_WING : body;       /* 등을 덮은 날개 */
        if (v > 0.2f && fmodf(v * 6.0f, 1.0f) < 0.18f) return C_BLACK;              /* 배의 줄무늬 */
        return body;
    }
    if (face == 1 && u * u + (v + 0.42f) * (v + 0.42f) < 0.05f) return C_BROWN;
    if ((au - 0.55f) * (au - 0.55f) / 0.18f + (v + 0.15f) * (v + 0.15f) / 0.05f < 1.0f) return C_WING;
    if (au > 0.3f && au < 0.8f && fabsf(v - (0.3f + au * 0.45f)) < 0.035f) return C_BLACK;   /* 다리 */
    if (au > 0.3f && au < 0.7f && fabsf(v - (0.05f + au * 0.5f)) < 0.03f) return C_BLACK;
    return C_NONE;
}

static void draw_fly(canvas_t *c, const boss_snap_t *b, const thing_t *t, float eye_x, float eye_y)
{
    float scale = UNIT / t->depth;                                /* 한 칸이 화면에서 몇 픽셀인가 */
    int half = (int)(0.6f * scale), cx = (int)((float)VW / 2.0f * (1.0f + t->side / t->depth));
    int floor_y = VY + VH / 2 + (int)(EZ * scale), cy = floor_y - (int)(0.55f * scale);
    if (half < 2) half = 2;
    float lift = boss_view_fly_lift(b);
    if (lift > 0.08f) {                                           /* 떠 있다: 바닥에 그림자를 남기고 위로 올린다 */
        int thick = (int)(UNIT / 52.0f) > 1 ? (int)(UNIT / 52.0f) : 1;
        for (int x = cx - half * 2 / 3; x <= cx + half * 2 / 3; x++)
            if (x >= 0 && x < VW && t->depth < zbuf[x]) ui_rect(c, x, floor_y - 1 > VY ? floor_y - 1 : VY, 1, thick, C_BLACK);
        cy -= (int)(lift * scale);
    }
    /* 초파리가 나를 보고 있는가, 등을 돌렸는가, 옆을 보는가 */
    float fa = (float)b->fly.ang * TWO_PI / 65536.0f, hx = cosf(fa), hy = sinf(fa);
    float tx = eye_x - t->x, ty = eye_y - t->y, len = sqrtf(tx * tx + ty * ty);
    float front = len > 1e-4f ? (hx * tx + hy * ty) / len : 1.0f, cross = len > 1e-4f ? (hx * ty - hy * tx) / len : 0.0f;
    int face = front > 0.5f ? 0 : (front < -0.5f ? 1 : 2), turn = cross > 0.0f ? -1 : 1;
    color_t body = b->fly.stun ? C_GRAY2 : C_FLY_BODY;
    if ((b->fly.windup || b->fly.charging) && (b->tick / 2) % 2) body = C_RED;          /* 돌진하려 한다 */
    for (int x = cx - half; x <= cx + half; x++) {
        if (x < 0 || x >= VW || t->depth >= zbuf[x]) continue;
        for (int y = cy - half; y <= cy + half; y++) {
            color_t col = fly_pixel((float)(x - cx) / (float)half, (float)(y - cy) / (float)half, face, turn, body, b->fly.stun > 0);
            if (col != C_NONE) px_view(c, x, y, col);
        }
    }
}

static void draw_player(canvas_t *c, const boss_snap_t *b, const thing_t *t)
{
    float scale = UNIT / t->depth;
    int cx = (int)((float)VW / 2.0f * (1.0f + t->side / t->depth)), floor_y = VY + VH / 2 + (int)(EZ * scale);
    int w = (int)(0.4f * scale), h = (int)(0.8f * scale), head = (int)(0.13f * scale);
    if (b->p[t->idx].alive) floor_y -= (int)((float)b->p[t->idx].z / 100.0f * scale);     /* 뛰어올랐으면 그만큼 위에 */
    color_t col = pcol[t->idx % BOSS_PLAYERS];
    if (w < 2) w = 2;
    if (!b->p[t->idx].alive) { h = (int)(0.1f * scale) + 1; col = C_GRAY2; head = 0; }   /* 쓰러졌다 */
    for (int x = cx - w / 2; x <= cx + w / 2; x++) {
        if (x < 0 || x >= VW || t->depth >= zbuf[x]) continue;
        for (int y = floor_y - h; y < floor_y; y++) px_view(c, x, y, col);
        for (int y = floor_y - h - 2 * head; y < floor_y - h; y++)
            if ((x - cx) * (x - cx) + (y - (floor_y - h - head)) * (y - (floor_y - h - head)) <= head * head) px_view(c, x, y, CX_TAN);
    }
}

static void draw_trap(canvas_t *c, const thing_t *t)
{
    float scale = UNIT / t->depth;
    int cx = (int)((float)VW / 2.0f * (1.0f + t->side / t->depth)), cy = VY + VH / 2 + (int)((EZ - 0.4f) * scale);
    int r = (int)(0.12f * scale) + 1;
    for (int x = cx - r; x <= cx + r; x++) {
        if (x < 0 || x >= VW || t->depth >= zbuf[x]) continue;
        for (int y = cy - r; y <= cy + r; y++)
            if ((x - cx) * (x - cx) + (y - cy) * (y - cy) <= r * r) px_view(c, x, y, C_ORANGE);
    }
}

/* 먹이: 바닥에 놓인 노란 덩이 */
static void draw_bait(canvas_t *c, const thing_t *t)
{
    float scale = UNIT / t->depth;
    int cx = (int)((float)VW / 2.0f * (1.0f + t->side / t->depth)), cy = VY + VH / 2 + (int)((EZ - 0.1f) * scale);
    int r = (int)(0.16f * scale) + 1;
    for (int x = cx - r; x <= cx + r; x++) {
        if (x < 0 || x >= VW || t->depth >= zbuf[x]) continue;
        for (int y = cy - r * 2 / 3; y <= cy + r * 2 / 3; y++)
            if ((x - cx) * (x - cx) * 4 + (y - cy) * (y - cy) * 9 <= r * r * 4) px_view(c, x, y, C_YELLOW);
    }
}

static void draw_things(canvas_t *c, const boss_snap_t *b, int eye, float px, float py, float dx, float dy)
{
    thing_t list[1 + BOSS_PLAYERS + BOSS_PROJ + BOSS_BAITS];
    float plx = -dy * PLANE, ply = dx * PLANE, inv = 1.0f / (plx * dy - dx * ply);
    int n = 0;
    list[n++] = (thing_t){ (float)b->fly.x / BOSS_FIX, (float)b->fly.y / BOSS_FIX, 0, 0, 0, 0 };
    for (int i = 0; i < b->n_players && i < BOSS_PLAYERS; i++)
        if (i != eye) list[n++] = (thing_t){ (float)b->p[i].x / BOSS_FIX, (float)b->p[i].y / BOSS_FIX, 0, 0, 1, i };
    for (int k = 0; k < b->n_proj && k < BOSS_PROJ; k++)
        list[n++] = (thing_t){ (float)b->proj[k].x / BOSS_FIX, (float)b->proj[k].y / BOSS_FIX, 0, 0, 2, k };
    for (int k = 0; k < b->n_bait && k < BOSS_BAITS; k++)
        list[n++] = (thing_t){ (float)b->bait[k].x / BOSS_FIX, (float)b->bait[k].y / BOSS_FIX, 0, 0, 3, k };
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
        if (list[i].kind == 0) draw_fly(c, b, &list[i], px, py);
        else if (list[i].kind == 1) draw_player(c, b, &list[i]);
        else if (list[i].kind == 2) draw_trap(c, &list[i]);
        else draw_bait(c, &list[i]);
    }
}

/* ---- 손에 든 무기, 작은 지도 ---------------------------------------------------------- */
static void draw_weapon(canvas_t *c, const boss_snap_t *b, int me)
{
    const int k = BIG;
    int item = b->p[me].item, used = b->p[me].cool >= 6;         /* 방금 썼다 */
    int bx = VW / 2 + 50 * k, by = VY + VH - (used ? 44 : 30) * k;
    if (item == BOSS_SWATTER) {
        ui_rect(c, bx + 8 * k, by + 18 * k, 3 * k, 30 * k, C_BROWN);
        ui_rect(c, bx, by, 20 * k, 20 * k, C_GRAY3);
        for (int i = 3; i < 20; i += 4) {
            ui_rect(c, bx + i * k, by, k, 20 * k, C_GRAY1);
            ui_rect(c, bx, by + i * k, 20 * k, k, C_GRAY1);
        }
    } else if (item == BOSS_SPRAY) {
        ui_rect(c, bx + 4 * k, by + 8 * k, 12 * k, 30 * k, C_CYAN);
        ui_rect(c, bx + 7 * k, by + 2 * k, 6 * k, 6 * k, C_WHITE);
        if (used) for (int i = 0; i < 14; i++) ui_rect(c, bx + (6 - i * 3) * k, by + (4 - i) * k, k, k, (i & 1) ? C_WHITE : C_CYAN);
    } else {
        for (int y = -7 * k; y <= 7 * k; y++)
            for (int x = -9 * k; x <= 9 * k; x++)
                if (x * x * 49 + y * y * 81 <= 49 * 81 * k * k) px_view(c, bx + 10 * k + x, by + 14 * k + y, C_ORANGE);
    }
}

/* 오른쪽 위의 작은 지도. my_ang은 내가 보는 방향이다(me가 0 이상일 때) */
static void draw_minimap(canvas_t *c, const boss_snap_t *b, int me, float my_ang)
{
    const int cell = 2 * BIG, dot = BIG, ox = VW - BOSS_MAP * cell - 2, oy = VY + 2;
    ui_rect(c, ox - 1, oy - 1, BOSS_MAP * cell + 2, BOSS_MAP * cell + 2, C_BLACK);
    for (int y = 0; y < BOSS_MAP; y++)
        for (int x = 0; x < BOSS_MAP; x++)
            if (wall[y][x]) ui_rect(c, ox + x * cell, oy + y * cell, cell, cell, C_GRAY2);
    ui_rect(c, ox + b->fly.x * cell / BOSS_FIX - dot, oy + b->fly.y * cell / BOSS_FIX - dot, 3 * dot, 3 * dot,
            b->fly.stun ? C_ORANGE : (b->fly.air ? C_YELLOW : C_RED));
    for (int i = 0; i < b->n_players && i < BOSS_PLAYERS; i++) {
        int x = ox + b->p[i].x * cell / BOSS_FIX, y = oy + b->p[i].y * cell / BOSS_FIX;
        float a = i == me ? my_ang : (float)b->p[i].ang * TWO_PI / 65536.0f;
        if (!b->p[i].alive) continue;
        ui_rect(c, x - dot, y - dot, 2 * dot, 2 * dot, i == me ? C_WHITE : pcol[i]);
        ui_rect(c, x + (int)(cosf(a) * 3.0f * (float)dot), y + (int)(sinf(a) * 3.0f * (float)dot), dot, dot,
                i == me ? C_WHITE : pcol[i]);
    }
}

/* ---- 글자 ------------------------------------------------------------------------- */
/* 글자열 하나를 한글 줄의 높이에 맞춰 놓는다. 영문만 있는 조각은 한글이 섞인 조각 안의 영문과 같은 높이로
 * 내려서 놓는다. 그린 폭을 돌려준다 */
static int put_text(canvas_t *c, int x, int y, const char *s, color_t col)
{
    ui_text(c, x, ui_text_h(s) > UI_FONT_H ? y : y + UI_KFONT_LATIN_DY, s, col);
    return ui_text_w(s);
}

typedef struct { const char *key, *what; } key_hint_t;

/* 키는 노랗게, 설명은 흰색으로 한 줄에 이어 놓는다. key가 NULL인 항목에서 끝난다. gap은 항목 사이의 간격이고,
 * hi번째 항목(0부터, 없으면 -1)의 설명은 켜져 있다는 뜻으로 하늘색으로 그린다 */
static void put_hints(canvas_t *c, int x, int y, const key_hint_t *h, int gap, int hi)
{
    for (int i = 0; h->key; h++, i++) {
        x += put_text(c, x, y, h->key, C_YELLOW) + 4;
        x += put_text(c, x, y, h->what, i == hi ? C_CYAN : C_TEXT) + gap;
    }
}

/* ---- 초파리 뇌 표시 ------------------------------------------------------------------ */
/* 이름과 막대 하나. 값(0~1)만큼 차고, mark가 0보다 크면 그 자리에 세로줄(기준)을 긋는다 */
static void gauge(canvas_t *c, int x, int y, int w, const char *label, float v, float mark, color_t col)
{
    int lw = put_text(c, x, y, label, C_GRAY3) + 4, bw = w - lw;
    if (bw < 8) return;
    ui_rect(c, x + lw, y + 3, bw, 7, C_GRAY1);
    ui_rect(c, x + lw + 1, y + 4, (int)(clamp01(v) * (float)(bw - 2) + 0.5f), 5, col);
    if (mark > 0.0f) ui_rect(c, x + lw + 1 + (int)(clamp01(mark) * (float)(bw - 3)), y + 2, 1, 9, C_WHITE);
}

/* 3D 화면의 왼쪽 위에 초파리 뇌를 보여 준다. 그림은 미니게임 화면의 뇌 표시와 같다(진짜 뇌이면 뇌 지도).
 * 그 아래의 막대는 보스전에서 뇌가 하는 일을 읽는 법이다.
 *   자극: 참가자가 다가가서 초파리의 눈에 들어가는 다가옴 자극(좌우 가운데 큰 쪽)
 *   도약: 뇌의 달아남 반응. 세로줄(뛰는 기준)을 넘으면 초파리가 뛰어올라 달아난다
 * 자리가 좁으면(배율 1) 이름과 자극 막대를 빼고 도약 막대만 둔다. 차지한 자리의 오른쪽 끝을 돌려준다 */
static int draw_brain_box(canvas_t *c, const boss_snap_t *b, const boss_view_t *v)
{
    static canvas_t *panel;
    static const float zero[BRAIN_GROUP_COUNT];
    const int k = BIG, roomy = VH >= 150, x = 3, w = (BRAIN_PANEL_W + 2) * k, ph = (BRAIN_PANEL_H + 2) * k;
    const float *stim = v->brain_stim ? v->brain_stim : zero, *rate = v->brain_rate;
    int y = VY + 3;
    if (!rate) return 0;
    if (!panel) panel = canvas_new(BRAIN_PANEL_W + 2, BRAIN_PANEL_H + 2);
    if (!panel) return 0;

    ui_rect(c, x - 2, y - 2, w + 4, ph + (roomy ? 13 + 26 : 13) + 4, C_BLACK);
    if (roomy) { put_text(c, x, y, "초파리 뇌", C_CYAN); y += 13; }
    ui_clear(panel, C_BLACK);
    compose_brain_panel(panel, 1, 1, stim, rate, v->brain_map, b->tick / 2);
    canvas_blit_scaled(c, x, y, panel, k);
    y += ph + 1;
    if (roomy) {
        float loom = stim[BR_LOOM_L] > stim[BR_LOOM_R] ? stim[BR_LOOM_L] : stim[BR_LOOM_R];
        gauge(c, x, y, w, "자극", loom, 0.0f, C_CYAN);
        y += 13;
    }
    gauge(c, x, y, w, "도약", rate[BR_ESCAPE], v->takeoff, v->takeoff > 0.0f && rate[BR_ESCAPE] >= v->takeoff ? C_WARN : C_CYAN);
    return x + w + 2;
}

/* ---- 키 안내 ---------------------------------------------------------------------- */
/* 말풍선 상자. core/scenes.c의 말풍선과 같은 모양이다(모서리를 한 픽셀 깎은 검은 상자에 흰 테두리,
 * 아래로 내려오는 꼬리). boss/는 core/를 쓰지 않으므로 여기서 따로 그린다 */
static void speech_box(canvas_t *c, int x, int y, int w, int h, int tail_x)
{
    if (ui_capture_note(c, UI_HD_BUBBLE, x, y, w, h, tail_x, NULL)) return;     /* 픽셀 그림으로 보낼 때는 다시 그린다 */
    ui_rect(c, x + 1, y, w - 2, h, C_BLACK);
    ui_rect(c, x, y + 1, w, h - 2, C_BLACK);
    ui_line(c, x + 2, y, x + w - 3, y, C_WHITE);
    ui_line(c, x + 2, y + h - 1, x + w - 3, y + h - 1, C_WHITE);
    ui_line(c, x, y + 2, x, y + h - 3, C_WHITE);
    ui_line(c, x + w - 1, y + 2, x + w - 1, y + h - 3, C_WHITE);
    ui_px(c, x + 1, y + 1, C_WHITE);
    ui_px(c, x + w - 2, y + 1, C_WHITE);
    ui_px(c, x + 1, y + h - 2, C_WHITE);
    ui_px(c, x + w - 2, y + h - 2, C_WHITE);
    for (int k = 0; k < 5; k++) {
        ui_line(c, tail_x + k / 2, y + h - 1 + k, tail_x + 4 - (k + 1) / 2, y + h - 1 + k, C_BLACK);
        ui_px(c, tail_x + k / 2, y + h - 1 + k, C_WHITE);
        ui_px(c, tail_x + 4 - (k + 1) / 2, y + h - 1 + k, C_WHITE);
    }
}

/* 시프봇의 키 안내. 240x134 한 장을 다 쓴다. 보스전이 시작하기 전(서버가 잠깐 기다려 준다)과 H를 눌렀을 때 띄운다.
 * 진행자 시프봇이 말풍선으로 키를 전부 알려 주고, 옆에 한마디(살금살금 다가가라)를 덧붙인다.
 * 조작이 바뀌면 여기와 draw_legend, 상태 줄의 숫자, docs/guide.md의 "보스전 조작"을 같이 고친다 */
static void draw_guide(canvas_t *c, int intro, int age_ms)
{
    static sprite_id_t cat = SPR_MISSING;
    static const key_hint_t rows[][5] = {             /* 줄마다 끝에 빈 칸(NULL)이 있어야 한다 */
        { { "W S", "앞뒤" }, { "A D", "옆걸음" }, { "마우스", "시선" }, { NULL, NULL } },
        { { "클릭/E", "쓰기" }, { "SPACE", "점프" }, { "R", "막기" }, { NULL, NULL } },
        { { "1", "파리채" }, { "2", "스프레이" }, { "3", "끈끈이" }, { "4", "먹이" }, { NULL, NULL } },
        { { "F/휠", "다음 무기" }, { "C", "살금살금" }, { "H", "안내" }, { NULL, NULL } },
        { { "TAB", "쓰러진 뒤 다른 사람 보기" }, { "Q", "나가기" }, { NULL, NULL } },
    };
    const int n = (int)(sizeof rows / sizeof rows[0]), bx = 6, by = 18, bw = 228, bh = 10 + n * 13;
    const char *right = intro ? "곧 시작해요" : "H 닫기";
    if (cat == SPR_MISSING) cat = ui_sprite_load("sipbot_guide");

    ui_rect(c, 0, 0, SCREEN_W, SCREEN_H, C_BG);
    put_text(c, 8, 3, "보스전 조작", C_YELLOW);
    put_text(c, SCREEN_W - 8 - ui_text_w(right), 3, right, C_CYAN);

    speech_box(c, bx, by, bw, bh, 34);
    for (int i = 0; i < n; i++) put_hints(c, bx + 8, by + 5 + i * 13, rows[i], 8, -1);

    /* 시프봇은 말풍선 아래 왼쪽에 서고, 그 옆에 가장 중요한 한마디를 둔다 */
    int cy = SCREEN_H - ui_sprite_h(cat), frames = ui_sprite_frames(cat);
    ui_sprite(c, cat, 10, cy, frames > 0 ? (age_ms < 0 ? 0 : age_ms / 300) % frames : 0);
    put_text(c, 72, by + bh + 8, "먹이(4)를 놓으면 와서 먹어요", C_TEXT);
    put_text(c, 72, by + bh + 22, "먹을 때 C로 살금살금 다가가요", C_CYAN);
}

/* 싸우는 동안 3D 화면의 왼쪽 아래에 늘 보이는 짧은 안내. 무기를 고르는 숫자 키는 아래 상태 줄에 적혀 있다.
 * 살금살금이 켜져 있으면 그 글자가 하늘색이 된다. 오른쪽 끝(166)은 손에 든 무기의 왼쪽 끝보다 안쪽이어야 한다 */
static void draw_legend(canvas_t *c, int sneak)
{
    static const key_hint_t a[] = { { "클릭", "쓰기" }, { "SPACE", "점프" }, { "R", "막기" }, { NULL, NULL } };
    static const key_hint_t b[] = { { "WASD", "이동" }, { "C", "살금살금" }, { "H", "안내" }, { NULL, NULL } };
    ui_rect(c, 0, VY + VH - 29, 166, 29, C_BLACK);
    put_hints(c, 3, VY + VH - 27, a, 8, -1);
    put_hints(c, 3, VY + VH - 14, b, 8, sneak ? 1 : -1);     /* 살금살금이 둘째다 */
}

/* ---- 상태 줄, 결과 ------------------------------------------------------------------- */
static void draw_bars(canvas_t *c, const boss_snap_t *b, const boss_view_t *v, int eye)
{
    static const char *item_name[BOSS_ITEMS] = { "파리채", "스프레이", "끈끈이", "먹이" };
    char line[64];
    int bar_w = CW - 42 - 76;                                    /* 초파리 체력 막대. 화면이 넓으면 길어진다 */
    ui_rect(c, 0, 0, CW, VY, C_BLACK);
    ui_rect(c, 0, VY + VH, CW, CH - VY - VH, C_BLACK);
    ui_text(c, 2, 1, "초파리", C_TEXT);
    ui_rect(c, 42, 4, bar_w, 6, C_GRAY1);
    ui_rect(c, 43, 5, b->fly.hp_max > 0 ? (bar_w - 2) * b->fly.hp / b->fly.hp_max : 0, 4, C_RED);
    snprintf(line, sizeof line, "%d/%d", b->fly.hp, b->fly.hp_max);
    ui_text(c, 42 + bar_w + 4, 3, line, C_TEXT);
    snprintf(line, sizeof line, "%d", b->ticks_left / 20);
    ui_text(c, CW - 2 - ui_text_w(line), 3, line, b->ticks_left < 600 ? C_WARN : C_TEXT);

    int y = VY + VH + 2;
    if (eye < 0) { ui_text(c, 2, y, "모두 쓰러졌다", C_GRAY3); return; }
    if (eye != v->me) {
        snprintf(line, sizeof line, "관전 %.10s   Tab 다른 사람", v->name[eye] ? v->name[eye] : "");
        ui_text(c, 2, y, line, pcol[eye]);
        return;
    }
    /* 체력(빨간 칸), 그리고 무기 셋. 이름 앞의 숫자가 그 무기를 고르는 키다 */
    int hp = b->p[eye].hp < 5 ? b->p[eye].hp : 5;
    for (int k = 0; k < hp; k++) ui_rect(c, 2 + k * 8, y + 3, 6, 6, C_BAD);
    int x = 2 + (hp > 3 ? hp : 3) * 8 + 4;
    for (int k = 0; k < BOSS_ITEMS; k++) {
        const char key[2] = { (char)('1' + k), '\0' };               /* 무기 k를 고르는 키는 SK_1 + k다 */
        int on = k == b->p[eye].item;
        /* 남은 수는 고른 무기에만 적는다(넷을 다 적으면 좁은 화면에서 넘친다) */
        int left = k == BOSS_SPRAY ? b->p[eye].ammo : (k == BOSS_TRAP ? b->p[eye].traps : b->p[eye].baits);
        if (on && k != BOSS_SWATTER) snprintf(line, sizeof line, "%s %d", item_name[k], left);
        else snprintf(line, sizeof line, "%s", item_name[k]);
        x += put_text(c, x, y, key, on ? C_WHITE : C_GRAY3) + 2;
        x += put_text(c, x, y, line, on ? C_YELLOW : C_GRAY2) + 4;
    }
}

/* 보스전이 끝났으면 가운데에 결과를 알린다. 서버는 끝난 상태를 잠깐 그대로 둔 뒤 랭킹으로 넘어간다 */
static void draw_outcome(canvas_t *c, const boss_snap_t *b)
{
    if (b->outcome == BOSS_GOING) return;
    int win = b->outcome == BOSS_PLAYERS_WIN, alive = 0;
    for (int i = 0; i < b->n_players && i < BOSS_PLAYERS; i++) alive += b->p[i].alive;
    const char *head = win ? "초파리를 잡았다!" : (alive ? "시간이 다 됐다" : "모두 쓰러졌다");
    const char *sub  = win ? "살아남은 사람은 점수를 더 받아요" : "초파리가 이겼어요";
    color_t col = win ? C_GOOD : C_BAD;
    int hw = ui_text_w(head), sw = ui_text_w(sub);
    /* 위쪽의 작은 지도와 뇌 표시를 가리지 않게 화면의 아래쪽 절반에 놓는다 */
    int bw = (hw > sw ? hw : sw) + 24, bh = 44, bx = (VW - bw) / 2, by = VY + VH / 2 + 2;
    ui_rect(c, bx - 1, by - 1, bw + 2, bh + 2, col);
    ui_rect(c, bx, by, bw, bh, C_BLACK);
    ui_text(c, (VW - hw) / 2, by + 8, head, col);
    ui_text(c, (VW - sw) / 2, by + 26, sub, C_TEXT);
}

/* 3D 화면 위에 얹는 것: 손에 든 무기, 가운데 표시, 작은 지도, 뇌 표시, 짧은 키 안내, 알림, 상태 줄, 결과.
 * 얹는 면의 좌표로 그린다(set_view를 얹는 면의 크기로 부른 뒤). eye_ang은 지금 보는 눈의 방향이다 */
static void draw_overlay(canvas_t *c, const boss_snap_t *b, const boss_view_t *v, int eye, float eye_ang)
{
    if (eye < 0) {                                   /* 볼 사람이 없다: 지도와 뇌만 */
        ui_rect(c, 0, VY, VW, VH, C_BG);
        draw_minimap(c, b, -1, 0.0f);
        draw_brain_box(c, b, v);
        draw_bars(c, b, v, eye);
        draw_outcome(c, b);
        return;
    }
    if (eye == v->me) {
        /* 가운데 표시. 방금 맞혔으면 빨갛고, 살금살금이 켜져 있으면 하늘색이다 */
        const int a = 3 * BIG, cx = VW / 2, cy = VY + VH / 2;
        color_t col = v->hit_ms >= 0 && v->hit_ms < 250 ? C_RED : (b->p[eye].sneak ? C_CYAN : C_WHITE);
        draw_weapon(c, b, eye);
        ui_rect(c, cx - a, cy, 2 * a + BIG, BIG, col);
        ui_rect(c, cx, cy - a, BIG, 2 * a + BIG, col);
    }
    draw_minimap(c, b, eye, eye_ang);
    int left = draw_brain_box(c, b, v), right = VW - BOSS_MAP * 2 * BIG - 6;
    if (eye == v->me && b->outcome == BOSS_GOING) draw_legend(c, b->p[eye].sneak);

    /* 알림: 내가 맞았다(테두리), 돌진이 나를 겨눈다, 날갯짓, 끈끈이에 붙었다 */
    if (v->hurt_ms >= 0 && v->hurt_ms < 300) {
        const int t = 3 * BIG;
        ui_rect(c, 0, VY, VW, t, C_RED); ui_rect(c, 0, VY + VH - t, VW, t, C_RED);
        ui_rect(c, 0, VY, t, VH, C_RED); ui_rect(c, VW - t, VY, t, VH, C_RED);
    }
    const char *note = NULL;
    color_t note_col = C_WARN;
    if (b->fly.windup && b->fly.target == eye) { note = "돌진! R 막기 SPACE 점프"; note_col = C_RED; }
    else if (v->block_ms >= 0 && v->block_ms < 700) { note = "막았다!"; note_col = C_GOOD; }
    else if (v->dodge_ms >= 0 && v->dodge_ms < 700) { note = "뛰어넘었다!"; note_col = C_GOOD; }
    else if (v->wave_ms >= 0 && v->wave_ms < 500) note = "날갯짓!";
    else if (b->fly.feeding) { note = "먹고 있다! C 살금살금"; note_col = C_GOOD; }
    else if (b->fly.stun) note = "붙었다! 지금이다";
    else if (b->fly.air) note = "달아났다! C 살금살금";
    if (note && b->outcome == BOSS_GOING) {          /* 가운데에 놓되, 왼쪽의 뇌 표시와 오른쪽의 작은 지도를 피한다 */
        int w = ui_text_w(note), x = (VW - w) / 2;
        if (x + w > right) x = right - w;
        if (x < left + 2) x = left + 2;
        ui_rect(c, x - 2, VY + 3, w + 4, 14, C_BLACK);
        ui_text(c, x, VY + 4, note, note_col);
    }
    draw_bars(c, b, v, eye);
    draw_outcome(c, b);
}

void boss_view_draw(canvas_t *full, const boss_snap_t *b, const boss_view_t *v)
{
    static canvas_t *card;                           /* 시프봇의 키 안내를 그리는 240x134 */
    static canvas_t *over;                           /* 얹는 면. 화면과 크기가 다를 때(hud가 2일 때)만 쓴다 */
    int W = ui_width(full), H = ui_height(full), eye = -1, scale = W / SCREEN_W;
    if (scale < 1) scale = 1;
    if (scale > PRESENT_MAX_SCALE) scale = PRESENT_MAX_SCALE;
    int hud = scale >= 3 ? 2 : 1;
    if (!have_map) load_map();

    /* 시프봇의 키 안내는 다른 장면들처럼 240x134 한 장을 화면 크기로 키워 보여 준다. tick 0: 아직 시작하지 않았다 */
    if (b->outcome == BOSS_GOING && (v->help || b->tick == 0)) {
        if (scale == 1) { draw_guide(full, b->tick == 0, v->age_ms); return; }
        if (!card) card = canvas_new(SCREEN_W, SCREEN_H);
        if (!card) return;
        draw_guide(card, b->tick == 0, v->age_ms);
        canvas_blit_scaled(full, 0, 0, card, scale);
        return;
    }

    if (v->me >= 0 && v->me < b->n_players && b->p[v->me].alive) eye = v->me;
    int n = b->n_players < BOSS_PLAYERS ? b->n_players : BOSS_PLAYERS;
    for (int k = 0; eye < 0 && k < n; k++) {         /* 고른 사람부터 차례로, 살아 있는 사람을 찾는다 */
        int i = ((v->watch % n + n) % n + k) % n;
        if (b->p[i].alive) eye = i;
    }

    float eye_ang = 0.0f;
    ui_clear(full, C_BLACK);
    if (eye >= 0) {
        float px = (float)b->p[eye].x / BOSS_FIX, py = (float)b->p[eye].y / BOSS_FIX;
        /* 내 눈이면 마우스로 돌린 방향을 쓴다. 서버가 알려 준 방향은 한두 틱 늦다 */
        eye_ang = (float)(eye == v->me && v->look_on ? v->look_ang : b->p[eye].ang) * TWO_PI / 65536.0f;
        EZ = BOSS_VIEW_EYE_H + (eye == v->me && v->z_on ? v->z : (float)b->p[eye].z / 100.0f);      /* 뛰어오른 만큼 올라간다 */
        set_view(W, H, hud);
        draw_walls(full, px, py, cosf(eye_ang), sinf(eye_ang));
        draw_things(full, b, eye, px, py, cosf(eye_ang), sinf(eye_ang));
    }
    if (hud == 1) {
        set_view(W, H, 1);
        draw_overlay(full, b, v, eye, eye_ang);
        return;
    }
    int ow = W / hud, oh = H / hud;
    if (over && (ui_width(over) != ow || ui_height(over) != oh)) { canvas_free(over); over = NULL; }
    if (!over) over = canvas_new(ow, oh);
    if (!over) return;
    ui_clear(over, C_NONE);
    set_view(ow, oh, 1);
    draw_overlay(over, b, v, eye, eye_ang);
    canvas_blit_scaled(full, 0, 0, over, hud);
}
