/* 초파리의 몸. 식과 구조의 설명은 body.h에 있다. */
#include "boss/body.h"

#include <math.h>
#include <string.h>

#define TWO_PI_F 6.2831853f

/* 서로 번갈아 딛는 다리의 쌍(이웃한 다리). 같은 쪽의 앞뒤 이웃과, 좌우로 마주 보는 다리다 */
static const int pair[7][2] = { { 0, 1 }, { 1, 2 }, { 3, 4 }, { 4, 5 }, { 0, 3 }, { 1, 4 }, { 2, 5 } };

static float wrap(float a)
{
    a = fmodf(a, TWO_PI_F);
    return a < 0.0f ? a + TWO_PI_F : a;
}

int body_swinging(const body_cfg_t *c, int leg, float phase)
{
    return wrap(phase) < c->swing_end[leg % 3];
}

void body_foot(const body_cfg_t *c, int leg, float phase, float mag, float out[3])
{
    int seg = leg % 3;
    float p = wrap(phase), se = c->swing_end[seg], at, z = 0.0f;
    if (p < se) {                                    /* 들고 있다: 뒤에서 앞으로 부드럽게 옮기며 들었다 놓는다 */
        float u = p / se;
        at = -0.5f * cosf(3.14159265f * u);
        z = c->lift * mag * sinf(3.14159265f * u);
    } else {                                         /* 딛고 있다: 앞에서 뒤로 고르게 간다(몸이 그만큼 앞으로 간다) */
        at = 0.5f - (p - se) / (TWO_PI_F - se);
    }
    out[0] = c->foot_x[seg] + c->sweep[seg] * mag * at;
    out[1] = (leg < 3 ? -1.0f : 1.0f) * c->foot_y;
    out[2] = z;
}

/* 몸 기준의 점을 맵 좌표로 */
static void to_map(const body_t *b, float fx, float fy, float *mx, float *my)
{
    float cs = cosf(b->ang), sn = sinf(b->ang);
    *mx = b->x + cs * fx - sn * fy;
    *my = b->y + sn * fx + cs * fy;
}

static void plant(body_t *b, const body_cfg_t *c)
{
    for (int i = 0; i < BODY_LEGS; i++) {
        float f[3];
        b->down[i] = !body_swinging(c, i, b->phase[i]);
        body_foot(c, i, b->phase[i], b->mag[i], f);
        to_map(b, f[0], f[1], &b->ax[i], &b->ay[i]);
        b->ex[i] = b->ey[i] = 0.0f;
    }
}

void body_init(body_t *b, const body_cfg_t *c, float x, float y, float ang)
{
    memset(b, 0, sizeof *b);
    b->x = x; b->y = y; b->ang = ang;
    /* 세 다리씩 엇갈리게 시작한다. 딛고 있는 동안의 한가운데와, 그 반 바퀴 뒤 */
    for (int i = 0; i < BODY_LEGS; i++) {
        int group_a = (i == 0 || i == 2 || i == 4);
        b->phase[i] = 3.14159265f + (group_a ? 0.0f : 3.14159265f);
    }
    plant(b, c);
}

void body_place(body_t *b, const body_cfg_t *c, float x, float y, float ang)
{
    b->x = x; b->y = y; b->ang = ang;
    b->vx = b->vy = b->w = 0.0f;
    plant(b, c);
}

void body_push(body_t *b, float vx, float vy)
{
    b->vx += vx;
    b->vy += vy;
}

void body_step(body_t *b, const body_cfg_t *c, float left, float right)
{
    const float dt = BODY_DT;
    float dth[BODY_LEGS];

    /* 걸음 생성기. 명령의 크기가 보폭, 부호가 도는 쪽이다 */
    for (int i = 0; i < BODY_LEGS; i++) {
        float cmd = i < 3 ? left : right;
        dth[i] = TWO_PI_F * (cmd < 0.0f ? -c->step_hz : c->step_hz);
    }
    for (int k = 0; k < 7; k++) {
        int i = pair[k][0], j = pair[k][1];
        float s = sinf(b->phase[j] - b->phase[i] - 3.14159265f);
        dth[i] += b->mag[j] * c->couple * s;
        dth[j] -= b->mag[i] * c->couple * s;         /* sin(θ_i - θ_j - π) = -sin(θ_j - θ_i - π) */
    }
    for (int i = 0; i < BODY_LEGS; i++) {
        float cmd = i < 3 ? left : right;
        b->phase[i] += dth[i] * dt;
        b->mag[i] += c->converge * (fabsf(cmd) - b->mag[i]) * dt;
    }

    /* 다리의 힘. 딛는 순간의 발 자리를 바닥에 고정하고, 발이 있어야 할 자리와의 차이만큼 몸을 끈다 */
    float fx = 0.0f, fy = 0.0f, torque = 0.0f;
    for (int i = 0; i < BODY_LEGS; i++) {
        float f[3], px, py;
        int down = !body_swinging(c, i, b->phase[i]);
        body_foot(c, i, b->phase[i], b->mag[i], f);
        to_map(b, f[0], f[1], &px, &py);
        if (down && !b->down[i]) { b->ax[i] = px; b->ay[i] = py; b->ex[i] = b->ey[i] = 0.0f; }   /* 방금 디뎠다 */
        b->down[i] = down;
        if (!down) continue;
        float ex = b->ax[i] - px, ey = b->ay[i] - py;
        float lx = c->stiff * ex + c->damp * (ex - b->ex[i]) / dt, ly = c->stiff * ey + c->damp * (ey - b->ey[i]) / dt;
        float len = sqrtf(lx * lx + ly * ly);
        if (len > c->grip) {                         /* 버틸 수 있는 힘을 넘었다: 발이 미끄러진다 */
            float keep = c->grip / len;
            lx *= keep; ly *= keep;
            ex = lx / c->stiff; ey = ly / c->stiff;
            b->ax[i] = px + ex; b->ay[i] = py + ey;
            b->slips++;
        }
        b->ex[i] = ex; b->ey[i] = ey;
        fx += lx; fy += ly;
        torque += (px - b->x) * ly - (py - b->y) * lx;
    }
    b->vx += fx / c->mass * dt;
    b->vy += fy / c->mass * dt;
    b->w += torque / c->inertia * dt;
    b->x += b->vx * dt;
    b->y += b->vy * dt;
    b->ang += b->w * dt;
}
