/* 키보드를 치는 초파리(47x54). 오른쪽 위 칸에서 Tab으로 넘기면 나온다.
 *
 * 상자만으로 만든 작은 3D 그림이다. 상자마다 화면에서 차지하는 칸에 선을 쏘아
 * 닿는 면의 색을 칠하고, 깊이를 비교해 앞의 것이 뒤의 것을 가린다.
 * 보는 각도는 아래 CAM_YAW, CAM_PITCH로 바꾼다.
 *
 * 초파리 뇌의 반응을 키 입력처럼 보여 준다. 반응이 기준(0.5)을 넘는 동안 그 키를 누르고 있다.
 *   왼쪽, 오른쪽 화살표 : 방향 전환      위 화살표 : 도주
 *   아래 화살표         : 후진           긴 키     : 먹기
 * 보여 주기용 그림이다. 초파리가 실제로 키 입력을 보내는 것은 아니다.
 * 초파리 앞의 작은 모니터에는 초파리 칸의 게임 화면이 나온다.
 *
 * 좌표: 초파리는 원점 근처에 서서 +z 쪽(키보드와 모니터)을 본다. +y가 위, +x가 초파리의 왼쪽이다. */
#include "ui/ui_internal.h"
#include "brain.h"

#include <math.h>

#define FW 47
#define FH 54

#define CAM_YAW  (-140.0f)   /* 초파리의 정면에서 옆으로 돈 각도(도). 0이면 정면, 180이면 바로 뒤 */
#define CAM_PITCH  40.0f     /* 위에서 내려다보는 각도(도). 0이면 눈높이 */
#define CAM_ZOOM   4.6f      /* 길이 1이 화면에서 몇 픽셀인가 */
#define CAM_FAR    100.0f
#define FLY_SIZE   0.78f     /* 초파리의 크기. 키보드와 모니터는 그대로다 */

typedef struct { float x, y, z; } v3;

static v3 add(v3 a, v3 b)       { return (v3){ a.x + b.x, a.y + b.y, a.z + b.z }; }
static v3 sub(v3 a, v3 b)       { return (v3){ a.x - b.x, a.y - b.y, a.z - b.z }; }
static v3 mul(v3 a, float s)    { return (v3){ a.x * s, a.y * s, a.z * s }; }
static float dot(v3 a, v3 b)    { return a.x * b.x + a.y * b.y + a.z * b.z; }

static const v3 CAM_AT = { 0.0f, 2.8f, 3.05f };       /* 화면 가운데에 오는 점 */

/* 상자의 겉에 무늬가 있는 것들 */
enum { T_PLAIN, T_EYE, T_BODY, T_SCREEN };

/* top은 윗면, side_x는 옆면(초파리의 좌우), side_z는 앞뒤 면의 색이다. */
typedef struct { v3 lo, hi; color_t top, side_x, side_z; int tag; } box_t;

typedef struct {
    v3 to_cam, right, up;        /* 카메라의 세 방향 */
    float depth[FH][FW];         /* 픽셀마다 가장 가까운 것까지의 거리 */
    canvas_t *c;
    int ox, oy, frame;
    const canvas_t *screen;      /* 모니터에 띄울 그림 */
} view_t;

/* 상자: 닿으면 거리 t와 닿은 면의 축(0 x, 1 y, 2 z)을 채운다. */
static int hit_box(const box_t *b, v3 o, v3 d, float *t, int *axis)
{
    const float oo[3] = { o.x, o.y, o.z }, dd[3] = { d.x, d.y, d.z };
    const float lo[3] = { b->lo.x, b->lo.y, b->lo.z }, hi[3] = { b->hi.x, b->hi.y, b->hi.z };
    float t_in = -1e9f, t_out = 1e9f;
    for (int i = 0; i < 3; i++) {
        if (fabsf(dd[i]) < 1e-6f) {
            if (oo[i] < lo[i] || oo[i] > hi[i]) return 0;
            continue;
        }
        float t1 = (lo[i] - oo[i]) / dd[i], t2 = (hi[i] - oo[i]) / dd[i];
        if (t1 > t2) { float s = t1; t1 = t2; t2 = s; }
        if (t1 > t_in) { t_in = t1; *axis = i; }
        if (t2 < t_out) t_out = t2;
    }
    if (t_in > t_out) return 0;
    *t = t_in;
    return 1;
}

/* 모니터 화면의 한 점. u, v는 화면 왼쪽 위에서 잰 위치(0 ~ 1)다.
 * 게임 화면을 아주 작게 줄이므로, 근처에서 배경이 아닌 색을 먼저 고른다. */
static color_t screen_color(const canvas_t *src, float u, float v, int frame)
{
    if (u < 0.08f || u > 0.92f || v < 0.06f || v > 0.94f) return C_GRAY1;      /* 테두리 */
    if (!src) return ((int)(v * 12.0f) + frame) % 4 == 0 ? C_CYAN : C_BLUE;
    int cx = (int)((u - 0.08f) / 0.84f * (float)src->w), cy = (int)((v - 0.06f) / 0.88f * (float)src->h);
    for (int k = 0; k < 9; k++) {
        int x = cx + (k % 3 - 1) * (src->w / 30), y = cy + (k / 3 - 1) * (src->h / 30);
        if (x < 0 || y < 0 || x >= src->w || y >= src->h) continue;
        color_t col = src->px[y * src->w + x];
        if (col != C_BG && col != C_BLACK && col != C_NONE) return col;
    }
    return C_BLUE;
}

/* 상자의 한 면에서 점 p의 색 */
static color_t box_color(const view_t *v, const box_t *b, int axis, v3 p)
{
    color_t col = axis == 1 ? b->top : (axis == 0 ? b->side_x : b->side_z);
    if (b->tag == T_EYE && (axis == 0 || (axis == 2 && v->to_cam.z > 0.0f))) {
        /* 눈의 반짝임: 위쪽 앞 모서리의 흰 네모 */
        float inner = fabsf(b->lo.x) < fabsf(b->hi.x) ? fabsf(b->lo.x) : fabsf(b->hi.x);
        float a = axis == 0 ? b->hi.z - p.z : fabsf(p.x) - inner;
        float h = b->hi.y - p.y;
        if (a > 0.15f && a < 0.6f && h > 0.15f && h < 0.6f) col = C_WHITE;
    }
    if (b->tag == T_BODY && axis == 0 && (int)((b->hi.z - p.z) / 0.4f) % 2 == 1) col = CX_CLAY;   /* 옆구리 줄무늬 */
    if (b->tag == T_SCREEN && axis == 2)
        col = screen_color(v->screen, (b->hi.x - p.x) / (b->hi.x - b->lo.x), (b->hi.y - p.y) / (b->hi.y - b->lo.y), v->frame);
    return col;
}

/* 3D 점을 화면의 칸으로 바꾼다. */
static void project(const view_t *v, v3 p, float *sx, float *sy)
{
    v3 q = sub(p, CAM_AT);
    *sx = FW / 2.0f + dot(q, v->right) * CAM_ZOOM;
    *sy = FH / 2.0f - dot(q, v->up) * CAM_ZOOM;
}

/* 화면의 한 칸에서 장면 쪽으로 쏘는 선의 출발점 */
static v3 ray_from(const view_t *v, int px, int py)
{
    float sx = ((float)px + 0.5f - FW / 2.0f) / CAM_ZOOM, sy = (FH / 2.0f - (float)py - 0.5f) / CAM_ZOOM;
    return add(add(CAM_AT, mul(v->to_cam, CAM_FAR)), add(mul(v->right, sx), mul(v->up, sy)));
}

static void draw_box(view_t *v, const box_t *b)
{
    /* 여덟 꼭짓점이 화면에서 차지하는 범위만 살핀다 */
    float x0 = 1e9f, y0 = 1e9f, x1 = -1e9f, y1 = -1e9f;
    for (int k = 0; k < 8; k++) {
        float sx, sy;
        project(v, (v3){ k & 1 ? b->hi.x : b->lo.x, k & 2 ? b->hi.y : b->lo.y, k & 4 ? b->hi.z : b->lo.z }, &sx, &sy);
        if (sx < x0) x0 = sx;
        if (sx > x1) x1 = sx;
        if (sy < y0) y0 = sy;
        if (sy > y1) y1 = sy;
    }
    if (x1 < 0.0f || y1 < 0.0f || x0 > FW - 1 || y0 > FH - 1) return;
    int ax = x0 < 0.0f ? 0 : (int)x0, bx = x1 > FW - 1 ? FW - 1 : (int)x1;
    int ay = y0 < 0.0f ? 0 : (int)y0, by = y1 > FH - 1 ? FH - 1 : (int)y1;
    v3 d = mul(v->to_cam, -1.0f);
    for (int py = ay; py <= by; py++) {
        for (int px = ax; px <= bx; px++) {
            v3 o = ray_from(v, px, py);
            float t; int axis = 0;
            if (!hit_box(b, o, d, &t, &axis) || t >= v->depth[py][px]) continue;
            v->depth[py][px] = t;
            ui_px(v->c, v->ox + px, v->oy + py, box_color(v, b, axis, add(o, mul(d, t))));
        }
    }
}

static void put(view_t *v, float x0, float x1, float y0, float y1, float z0, float z1,
                color_t top, color_t side_x, color_t side_z, int tag)
{
    box_t b = { { x0 < x1 ? x0 : x1, y0, z0 }, { x0 < x1 ? x1 : x0, y1, z1 }, top, side_x, side_z, tag };
    draw_box(v, &b);
}

/* 초파리의 몸 한 조각. 크기를 FLY_SIZE만큼 줄여서 놓는다. */
static void put_fly(view_t *v, float x0, float x1, float y0, float y1, float z0, float z1,
                    color_t top, color_t side_x, color_t side_z, int tag)
{
    const float k = FLY_SIZE;
    put(v, x0 * k, x1 * k, y0 * k, y1 * k, z0 * k, z1 * k, top, side_x, side_z, tag);
}

/* 키 하나. 눌리면 낮아지고 색이 켜진다. */
static void put_key(view_t *v, float x0, float x1, float z0, float z1, int down)
{
    put(v, x0, x1, 0.5f, down ? 0.7f : 1.0f, z0, z1,
        down ? C_CYAN : C_GRAY3, down ? CX_TEAL : C_GRAY2, down ? CX_TEAL : C_GRAY2, T_PLAIN);
}

/* 앞다리 하나: 어깨에서 옆으로, 앞으로, 그리고 아래로 꺾여 (tx, tip, tz)를 짚는다. side는 왼쪽 +1, 오른쪽 -1 */
static void put_arm(view_t *v, float side, float tx, float tip, float tz)
{
    const float front = 2.8f * FLY_SIZE;                 /* 머리의 앞면 */
    put(v, side * 0.5f, tx + 0.2f * side, 1.5f, 1.9f, front, front + 0.4f, C_GRAY3, C_GRAY2, C_GRAY1, T_PLAIN);
    put(v, tx - 0.2f, tx + 0.2f, 1.5f, 1.9f, front + 0.2f, tz + 0.2f, C_GRAY3, C_GRAY2, C_GRAY1, T_PLAIN);
    put(v, tx - 0.2f, tx + 0.2f, tip, 1.5f, tz - 0.2f, tz + 0.2f, C_GRAY3, C_GRAY2, C_GRAY1, T_PLAIN);
}

void flycam_draw(canvas_t *c, int ox, int oy, const float *rate, int frame, const canvas_t *screen)
{
    static const float zero[BRAIN_GROUP_COUNT];
    static view_t v;
    if (!rate) rate = zero;
    int left  = rate[BR_TURN_L] >= 0.5f, right = rate[BR_TURN_R]   >= 0.5f;
    int up    = rate[BR_ESCAPE] >= 0.5f, down  = rate[BR_BACKWARD] >= 0.5f;
    int space = rate[BR_FEED]   >= 0.5f;
    int typing = left || right || up || down || space;
    /* 칠 때는 빠르게, 쉴 때는 천천히 고개를 끄덕인다 */
    float nod = (typing ? (frame & 1) : ((frame / 8) & 1)) ? -0.3f : 0.0f;
    float flap = up && (frame & 1) ? 0.45f : 0.0f;               /* 도주할 때 날개를 든다 */

    float yaw = CAM_YAW * 3.14159265f / 180.0f, pitch = CAM_PITCH * 3.14159265f / 180.0f;
    v.to_cam = (v3){ sinf(yaw) * cosf(pitch), sinf(pitch), cosf(yaw) * cosf(pitch) };
    v.right  = (v3){ cosf(yaw), 0.0f, -sinf(yaw) };
    v.up     = (v3){ -sinf(yaw) * sinf(pitch), cosf(pitch), -cosf(yaw) * sinf(pitch) };
    v.c = c; v.ox = ox; v.oy = oy; v.frame = frame; v.screen = screen;

    /* 책상. 초파리 아래에는 그림자가 진다 */
    v3 d = mul(v.to_cam, -1.0f);
    for (int py = 0; py < FH; py++) {
        for (int px = 0; px < FW; px++) {
            v3 o = ray_from(&v, px, py);
            float t = o.y / v.to_cam.y;
            v3 f = add(o, mul(d, t));
            v.depth[py][px] = t;
            ui_px(c, ox + px, oy + py, (fabsf(f.x) < 1.5f * FLY_SIZE && f.z > -2.6f * FLY_SIZE && f.z < 2.9f * FLY_SIZE) ? C_BLACK : C_BG);
        }
    }

    /* 초파리: 몸통, 머리, 눈, 더듬이 */
    put_fly(&v, -1.3f, 1.3f, 1.4f, 3.4f, -2.4f, 0.6f, CX_CLAY, C_BROWN, C_BROWN, T_BODY);
    put_fly(&v, -1.6f, 1.6f, 1.5f + nod, 3.9f + nod, 0.6f, 2.8f, CX_TAN, CX_CLAY, CX_CLAY, T_PLAIN);
    for (int s = -1; s <= 1; s += 2) {
        float fs = (float)s;
        put_fly(&v, fs * 0.9f, fs * 2.1f, 2.3f + nod, 4.1f + nod, 1.3f, 3.0f, C_BAD, C_FLY_EYE, CX_MAROON, T_EYE);
        put_fly(&v, fs * 0.3f, fs * 0.7f, 3.9f + nod, 4.9f + nod, 2.2f, 2.6f, C_GRAY2, C_GRAY1, C_GRAY1, T_PLAIN);
        /* 날개: 뒤로 갈수록 한 칸씩 올라가는 판 세 장 */
        for (int k = 0; k < 3; k++) {
            float zh = 0.2f - 1.1f * (float)k, yl = 3.55f + (0.45f + flap) * (float)k;
            put_fly(&v, fs * 0.2f, fs * 1.6f, yl, yl + 0.4f, zh - 1.1f, zh, C_WING, C_GRAY3, C_GRAY3, T_PLAIN);
        }
        /* 서 있는 다리 둘 */
        put_fly(&v, fs * 0.9f, fs * 1.4f, 0.0f, 1.4f, -0.4f, 0.1f, C_GRAY3, C_GRAY2, C_GRAY1, T_PLAIN);
        put_fly(&v, fs * 0.9f, fs * 1.4f, 0.0f, 1.4f, -2.2f, -1.7f, C_GRAY3, C_GRAY2, C_GRAY1, T_PLAIN);
    }

    /* 키보드 */
    put(&v, -2.9f, 2.9f, 0.0f, 0.5f, 3.0f, 5.9f, CX_SLATE, C_BLACK, C_BLACK, T_PLAIN);
    put_key(&v, -2.4f,  2.4f, 3.2f, 3.9f, space);
    put_key(&v,  0.9f,  2.3f, 4.1f, 4.8f, left);
    put_key(&v, -0.7f,  0.7f, 4.1f, 4.8f, down);
    put_key(&v, -2.3f, -0.9f, 4.1f, 4.8f, right);
    put_key(&v, -0.7f,  0.7f, 5.0f, 5.7f, up);

    /* 모니터: 받침, 기둥, 화면 */
    put(&v, -1.3f, 1.3f, 0.0f, 0.2f, 6.2f, 7.2f, C_GRAY2, C_GRAY1, C_GRAY1, T_PLAIN);
    put(&v, -0.3f, 0.3f, 0.2f, 1.2f, 6.6f, 6.9f, C_GRAY2, C_GRAY1, C_GRAY1, T_PLAIN);
    put(&v, -2.6f, 2.6f, 1.2f, 5.4f, 6.5f, 6.9f, C_GRAY2, C_GRAY1, C_GRAY1, T_SCREEN);

    /* 앞다리: 누르는 키를 짚는다. 왼다리는 왼쪽, 위, 긴 키. 오른다리는 오른쪽, 아래, 긴 키 */
    float lx = 1.2f, lz = 3.55f, ltip = 1.3f, rx = -1.2f, rz = 3.55f, rtip = 1.3f;
    if (left)       { lx = 1.6f; lz = 4.45f; ltip = 0.7f; }
    else if (up)    { lx = 0.3f; lz = 5.35f; ltip = 0.7f; }
    else if (space) { ltip = 0.7f; }
    if (right)      { rx = -1.6f; rz = 4.45f; rtip = 0.7f; }
    else if (down)  { rx = -0.3f; rz = 4.45f; rtip = 0.7f; }
    else if (space) { rtip = 0.7f; }
    put_arm(&v, +1.0f, lx, ltip, lz);
    put_arm(&v, -1.0f, rx, rtip, rz);

    ui_text(c, ox + 2, oy + 2, "FLY", C_WHITE);
}
