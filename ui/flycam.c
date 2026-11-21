/* 키보드를 치는 초파리(47x54). 오른쪽 위 칸에서 Tab으로 넘기면 나온다.
 *
 * 작은 3D 그림이다. 공(타원체)과 상자로 장면을 만들고, 픽셀마다 선을 하나씩 쏘아
 * 가장 먼저 닿는 것의 색을 칠한다. 다리는 3D 선분으로 그리고 깊이를 비교해 가린다.
 * 보는 각도는 아래 CAM_YAW, CAM_PITCH로 바꾼다.
 *
 * 초파리 뇌의 반응을 키 입력처럼 보여 준다. 반응이 기준(0.5)을 넘는 동안 그 키를 누르고 있다.
 *   왼쪽, 오른쪽 화살표 : 방향 전환      위 화살표 : 도주
 *   아래 화살표         : 후진           긴 키     : 먹기
 * 보여 주기용 그림이다. 초파리가 실제로 키 입력을 보내는 것은 아니다.
 *
 * 좌표: 초파리는 원점 근처에 서서 +z 쪽(키보드)을 본다. +y가 위, +x가 초파리의 왼쪽이다. */
#include "ui/ui_internal.h"
#include "brain.h"

#include <math.h>

#define FW 47
#define FH 54

#define CAM_YAW    45.0f    /* 초파리의 정면에서 옆으로 돈 각도(도). 0이면 정면 */
#define CAM_PITCH  40.0f    /* 위에서 내려다보는 각도(도). 0이면 눈높이 */
#define CAM_ZOOM   4.0f     /* 길이 1이 화면에서 몇 픽셀인가 */
#define CAM_FAR    100.0f

typedef struct { float x, y, z; } v3;

static v3 add(v3 a, v3 b)       { return (v3){ a.x + b.x, a.y + b.y, a.z + b.z }; }
static v3 sub(v3 a, v3 b)       { return (v3){ a.x - b.x, a.y - b.y, a.z - b.z }; }
static v3 mul(v3 a, float s)    { return (v3){ a.x * s, a.y * s, a.z * s }; }
static float dot(v3 a, v3 b)    { return a.x * b.x + a.y * b.y + a.z * b.z; }
static v3 unit(v3 a)            { return mul(a, 1.0f / sqrtf(dot(a, a))); }

static const v3 CAM_AT = { 0.0f, 2.2f, 3.3f };       /* 화면 가운데에 오는 점 */
static const v3 LIGHT  = { -0.30f, 0.80f, 0.52f };   /* 빛이 오는 쪽 */

enum { M_BODY, M_THORAX, M_ABDOMEN, M_EYE, M_WING };

typedef struct { v3 c, r; int kind; color_t lit, base, dark; } ball_t;
typedef struct { v3 lo, hi; color_t top, side; int arrow; } box_t;   /* arrow: 1 위, 2 아래, 3 왼쪽, 4 오른쪽 */

typedef struct {
    v3 to_cam, right, up;        /* 카메라의 세 방향 */
    float depth[FH][FW];         /* 픽셀마다 가장 가까운 것까지의 거리 */
    canvas_t *c;
    int ox, oy;
} view_t;

/* 공: 닿으면 거리 t와 그 점의 바깥 방향 n을 채운다. */
static int hit_ball(const ball_t *b, v3 o, v3 d, float *t, v3 *n, v3 *p)
{
    v3 oo = { (o.x - b->c.x) / b->r.x, (o.y - b->c.y) / b->r.y, (o.z - b->c.z) / b->r.z };
    v3 dd = { d.x / b->r.x, d.y / b->r.y, d.z / b->r.z };
    float a = dot(dd, dd), h = dot(oo, dd), k = dot(oo, oo) - 1.0f;
    float disc = h * h - a * k;
    if (disc < 0.0f) return 0;
    *t = (-h - sqrtf(disc)) / a;
    v3 q = add(oo, mul(dd, *t));
    *n = unit((v3){ q.x / b->r.x, q.y / b->r.y, q.z / b->r.z });
    *p = add(o, mul(d, *t));
    return 1;
}

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

/* 키 윗면의 화살표. lx, lz는 키 가운데에서 잰 위치(-1 ~ 1)다. */
static int on_arrow(int arrow, float lx, float lz)
{
    float along, across;
    switch (arrow) {
    case 1: along = lz;  across = lx; break;
    case 2: along = -lz; across = lx; break;
    case 3: along = lx;  across = lz; break;
    case 4: along = -lx; across = lz; break;
    default: return 0;
    }
    return along > -0.6f && along < 0.6f && fabsf(across) < (0.6f - along) * 0.7f;
}

static color_t ball_color(const ball_t *b, v3 n, v3 p, v3 half)
{
    float lam = dot(n, LIGHT);
    int tone = lam > 0.62f ? 2 : (lam > 0.12f ? 1 : 0);
    if (b->kind == M_THORAX && n.y > 0.35f && fabsf(fabsf(p.x - b->c.x) - 0.65f) < 0.22f) tone--;   /* 등의 줄무늬 */
    if (b->kind == M_ABDOMEN && fmodf(p.z - b->c.z + 20.0f, 1.25f) < 0.45f) tone--;                 /* 배의 띠 */
    if (b->kind == M_WING && fabsf(p.x - b->c.x) < 0.13f && b->r.x < 1.0f) tone = 0;                /* 날개의 맥 */
    if (b->kind == M_EYE && dot(n, half) > 0.94f) return C_WHITE;                                   /* 눈의 반짝임 */
    return tone >= 2 ? b->lit : (tone == 1 ? b->base : b->dark);
}

/* 3D 점을 화면의 칸과 거리로 바꾼다. */
static void project(const view_t *v, v3 p, float *sx, float *sy, float *t)
{
    v3 q = sub(p, CAM_AT);
    *sx = FW / 2.0f + dot(q, v->right) * CAM_ZOOM;
    *sy = FH / 2.0f - dot(q, v->up) * CAM_ZOOM;
    *t = CAM_FAR - dot(q, v->to_cam);
}

/* 3D 선분. 이미 그려진 것보다 뒤에 있는 부분은 그리지 않는다. */
static void line3(view_t *v, v3 a, v3 b, color_t col)
{
    float ax, ay, at, bx, by, bt;
    project(v, a, &ax, &ay, &at);
    project(v, b, &bx, &by, &bt);
    int steps = (int)(fabsf(bx - ax) > fabsf(by - ay) ? fabsf(bx - ax) : fabsf(by - ay)) + 1;
    for (int i = 0; i <= steps; i++) {
        float k = (float)i / (float)steps;
        int x = (int)(ax + (bx - ax) * k), y = (int)(ay + (by - ay) * k);
        float t = at + (bt - at) * k;
        if (x < 0 || y < 0 || x >= FW || y >= FH || t > v->depth[y][x] + 0.4f) continue;
        v->depth[y][x] = t;
        ui_px(v->c, v->ox + x, v->oy + y, col);
    }
}

static void leg(view_t *v, v3 shoulder, v3 knee, v3 foot)
{
    line3(v, shoulder, knee, C_BROWN);
    line3(v, knee, foot, C_BROWN);
}

void flycam_draw(canvas_t *c, int ox, int oy, const float *rate, int frame)
{
    static const float zero[BRAIN_GROUP_COUNT];
    static view_t v;
    if (!rate) rate = zero;
    int left  = rate[BR_TURN_L] >= 0.5f, right = rate[BR_TURN_R]   >= 0.5f;
    int up    = rate[BR_ESCAPE] >= 0.5f, down  = rate[BR_BACKWARD] >= 0.5f;
    int space = rate[BR_FEED]   >= 0.5f;
    int typing = left || right || up || down || space;
    /* 칠 때는 빠르게, 쉴 때는 천천히 끄덕인다 */
    float bob = (typing ? (frame & 1) : ((frame / 8) & 1)) ? -0.3f : 0.0f;
    int flap = up && (frame & 1);                                /* 도주할 때 날개를 편다 */

    float yaw = CAM_YAW * 3.14159265f / 180.0f, pitch = CAM_PITCH * 3.14159265f / 180.0f;
    v.to_cam = (v3){ sinf(yaw) * cosf(pitch), sinf(pitch), cosf(yaw) * cosf(pitch) };
    v.right  = (v3){ cosf(yaw), 0.0f, -sinf(yaw) };
    v.up     = (v3){ -sinf(yaw) * sinf(pitch), cosf(pitch), -cosf(yaw) * sinf(pitch) };
    v.c = c; v.ox = ox; v.oy = oy;
    v3 half = unit(add(LIGHT, v.to_cam));

    /* 장면: 초파리 */
    const ball_t balls[] = {
        { { 0.0f, 2.5f + bob, -3.0f }, { 1.35f, 1.25f, 2.3f }, M_ABDOMEN, C_WARN,   C_BROWN, C_FLY_BODY },
        { { 0.0f, 2.9f + bob,  0.0f }, { 1.60f, 1.50f, 1.9f }, M_THORAX,  C_ORANGE, C_BROWN, C_FLY_BODY },
        { { 0.0f, 3.1f + bob,  2.5f }, { 1.60f, 1.50f, 1.4f }, M_BODY,    C_ORANGE, C_BROWN, C_FLY_BODY },
        { {  1.15f, 3.4f + bob, 2.9f }, { 1.15f, 1.30f, 1.15f }, M_EYE,     C_BAD, C_FLY_EYE, C_BROWN },
        { { -1.15f, 3.4f + bob, 2.9f }, { 1.15f, 1.30f, 1.15f }, M_EYE,     C_BAD, C_FLY_EYE, C_BROWN },
        { { 0.0f, 2.0f + bob,  3.7f }, { 0.35f, 0.60f, 0.35f }, M_BODY,   C_WARN, C_WARN, C_BROWN },
        { { flap ?  3.0f :  1.0f, 3.9f + bob, flap ? -1.0f : -3.4f },
          { flap ? 2.6f : 0.85f, 0.10f, flap ? 1.2f : 2.9f }, M_WING, C_WING, C_WING, C_GRAY3 },
        { { flap ? -3.0f : -1.0f, 3.9f + bob, flap ? -1.0f : -3.4f },
          { flap ? 2.6f : 0.85f, 0.10f, flap ? 1.2f : 2.9f }, M_WING, C_WING, C_WING, C_GRAY3 },
    };
    /* 장면: 키보드. 눌린 키는 낮아지고 노랗게 된다 */
#define KEY(x0, x1, z0, z1, on, arrow) \
    { { x0, 0.6f, z0 }, { x1, (on) ? 0.80f : 1.15f, z1 }, (on) ? C_YELLOW : C_GRAY3, (on) ? C_ORANGE : C_GRAY2, arrow }
    const box_t boxes[] = {
        { { -4.2f, 0.0f, 3.2f }, { 4.2f, 0.6f, 9.0f }, C_GRAY1, C_BLACK, 0 },
        KEY(-3.7f,  3.7f, 3.6f, 4.8f, space, 0),
        KEY( 1.3f,  3.7f, 5.3f, 6.7f, left,  3),
        KEY(-1.2f,  1.2f, 5.3f, 6.7f, down,  2),
        KEY(-3.7f, -1.3f, 5.3f, 6.7f, right, 4),
        KEY(-1.2f,  1.2f, 7.2f, 8.6f, up,    1),
    };
#undef KEY

    v3 d = mul(v.to_cam, -1.0f);
    for (int py = 0; py < FH; py++) {
        for (int px = 0; px < FW; px++) {
            float sx = ((float)px + 0.5f - FW / 2.0f) / CAM_ZOOM, sy = (FH / 2.0f - (float)py - 0.5f) / CAM_ZOOM;
            v3 o = add(add(CAM_AT, mul(v.to_cam, CAM_FAR)), add(mul(v.right, sx), mul(v.up, sy)));

            /* 책상. 초파리 아래에는 그림자가 진다 */
            float best = o.y / v.to_cam.y;
            v3 f = add(o, mul(d, best));
            float shx = f.x / 1.9f, shz = (f.z + 1.0f) / 4.2f;
            color_t col = shx * shx + shz * shz < 1.0f ? C_BLACK : C_BG;

            for (size_t i = 0; i < sizeof boxes / sizeof boxes[0]; i++) {
                float t; int axis = 0;
                if (!hit_box(&boxes[i], o, d, &t, &axis) || t >= best) continue;
                best = t;
                col = axis == 1 ? boxes[i].top : boxes[i].side;
                if (axis == 1 && boxes[i].arrow) {
                    v3 p = add(o, mul(d, t));
                    float lx = (p.x - (boxes[i].lo.x + boxes[i].hi.x) / 2.0f) / ((boxes[i].hi.x - boxes[i].lo.x) / 2.0f);
                    float lz = (p.z - (boxes[i].lo.z + boxes[i].hi.z) / 2.0f) / ((boxes[i].hi.z - boxes[i].lo.z) / 2.0f);
                    if (on_arrow(boxes[i].arrow, lx, lz)) col = C_BLACK;
                }
            }
            for (size_t i = 0; i < sizeof balls / sizeof balls[0]; i++) {
                float t; v3 n, p;
                if (!hit_ball(&balls[i], o, d, &t, &n, &p) || t >= best) continue;
                best = t;
                col = ball_color(&balls[i], n, p, half);
            }
            v.depth[py][px] = best;
            ui_px(c, ox + px, oy + py, col);
        }
    }

    /* 앞다리: 누르는 키로 뻗는다. 왼다리는 왼쪽, 위, 긴 키. 오른다리는 오른쪽, 아래, 긴 키 */
    v3 lf = { 1.8f, 1.9f, 4.6f }, rf = { -1.8f, 1.9f, 4.6f };
    if (left)       lf = (v3){ 2.5f, 0.80f, 6.0f };
    else if (up)    lf = (v3){ 0.5f, 0.80f, 7.9f };
    else if (space) lf = (v3){ 1.8f, 0.80f, 4.2f };
    if (right)      rf = (v3){ -2.5f, 0.80f, 6.0f };
    else if (down)  rf = (v3){ -0.5f, 0.80f, 6.0f };
    else if (space) rf = (v3){ -1.8f, 0.80f, 4.2f };

    for (int s = -1; s <= 1; s += 2) {
        float fs = (float)s;
        v3 foot = s > 0 ? lf : rf;
        v3 sh = { fs * 1.1f, 2.2f + bob, 1.2f };
        v3 knee = add(mul(add(sh, foot), 0.5f), (v3){ fs * 0.9f, 1.3f, 0.0f });
        leg(&v, sh, knee, foot);
        leg(&v, (v3){ fs * 1.4f, 2.2f + bob,  0.0f }, (v3){ fs * 3.2f, 2.8f,  0.3f }, (v3){ fs * 3.9f, 0.0f,  0.6f });
        leg(&v, (v3){ fs * 1.3f, 2.2f + bob, -1.0f }, (v3){ fs * 3.0f, 2.6f, -2.6f }, (v3){ fs * 3.4f, 0.0f, -4.2f });
        line3(&v, (v3){ fs * 0.4f, 4.1f + bob, 3.3f }, (v3){ fs * 0.7f, 4.9f + bob, 4.2f }, C_BROWN);   /* 더듬이 */
    }

    ui_text(c, ox + 2, oy + 2, "FLY", C_WHITE);
}
