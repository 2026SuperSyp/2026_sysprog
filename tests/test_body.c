/* 초파리의 몸: 세 다리씩 번갈아 딛는가, 명령만큼 빠르게 걷는가, 좌우 명령의 차이로 도는가, 뒤로 걷는가,
 * 서 있을 때 밀리면 돌아오는가, 세게 밀리면 발이 미끄러지는가, 발의 자리, 같은 입력이면 같은 결과 */
#include "boss/boss.h"
#include "boss/body.h"
#include "t.h"

#include <math.h>
#include <string.h>

#define PI 3.14159265f

static boss_cfg_t BC;
static body_cfg_t C;
static body_t B;
static float v1;                             /* 명령이 1일 때 걷는 빠르기(칸/초) */

static void setup(void)
{
    boss_cfg_default(&BC);
    boss_body_cfg(&BC, &C);
    v1 = BC.walk_mm_s * 2.0f * BC.fly_radius / BC.body_mm * BC.speed_scale;
    body_init(&B, &C, 10.0f, 10.0f, 0.0f);   /* 오른쪽(+x)을 본다 */
}

static void run(float sec, float left, float right)
{
    for (int k = 0; k < (int)(sec / BODY_DT + 0.5f); k++) body_step(&B, &C, left, right);
}

/* 두 위상의 차이를 -π ~ π로 */
static float apart(float a, float b)
{
    float d = fmodf(a - b, 2.0f * PI);
    if (d > PI) d -= 2.0f * PI;
    if (d < -PI) d += 2.0f * PI;
    return d;
}

static void test_tripod(void)
{
    setup();
    run(2.0f, 1.0f, 1.0f);
    /* 왼앞(0), 왼뒤(2), 오른가운데(4)가 같이 딛고, 나머지 셋이 반 바퀴 뒤에 같이 딛는다 */
    T_CHECK(fabsf(apart(B.phase[0], B.phase[2])) < 0.3f);
    T_CHECK(fabsf(apart(B.phase[0], B.phase[4])) < 0.3f);
    T_CHECK(fabsf(apart(B.phase[1], B.phase[3])) < 0.3f);
    T_CHECK(fabsf(apart(B.phase[1], B.phase[5])) < 0.3f);
    T_CHECK(fabsf(fabsf(apart(B.phase[0], B.phase[1])) - PI) < 0.3f);
    for (int i = 0; i < BODY_LEGS; i++) T_CHECK(fabsf(B.mag[i] - 1.0f) < 0.05f);
    int down = 0;
    for (int i = 0; i < BODY_LEGS; i++) down += B.down[i];
    T_CHECK(down >= 3);                                   /* 언제나 셋 이상이 딛고 있다 */
}

static void test_walk(void)
{
    setup();
    run(1.0f, 1.0f, 1.0f);                                /* 걸음이 자리 잡을 때까지 */
    float x0 = B.x, y0 = B.y;
    run(2.0f, 1.0f, 1.0f);
    float speed = (B.x - x0) / 2.0f;
    T_CHECK(speed > v1 * 0.85f && speed < v1 * 1.15f);    /* 명령 1이면 걷는 빠르기 */
    T_CHECK(fabsf(B.y - y0) < 0.2f && fabsf(B.ang) < 0.15f);     /* 곧게 간다 */

    setup();
    run(1.0f, 0.5f, 0.5f);
    x0 = B.x;
    run(2.0f, 0.5f, 0.5f);
    speed = (B.x - x0) / 2.0f;
    T_CHECK(speed > v1 * 0.4f && speed < v1 * 0.6f);      /* 명령이 절반이면 보폭이 절반 */

    setup();
    run(1.0f, -1.0f, -1.0f);                              /* 음수: 뒤로 딛는다 */
    x0 = B.x;
    run(2.0f, -1.0f, -1.0f);
    T_CHECK((B.x - x0) / 2.0f < -v1 * 0.8f);
}

static void test_turn(void)
{
    setup();
    run(2.0f, 1.2f, 0.6f);                                /* 왼쪽 다리가 더 크게 딛는다: 오른쪽으로 돈다(각도가 는다) */
    T_CHECK(B.ang > 0.5f);
    setup();
    run(2.0f, 0.6f, 1.2f);
    T_CHECK(B.ang < -0.5f);
    setup();
    run(2.0f, 0.6f, -0.6f);                               /* 좌우가 반대: 제자리에서 돈다 */
    T_CHECK(B.ang > 1.5f);
    T_CHECK(hypotf(B.x - 10.0f, B.y - 10.0f) < 0.4f);
}

static void test_push(void)
{
    setup();
    run(1.0f, 0.0f, 0.0f);                                /* 명령이 없으면 서 있다 */
    T_CHECK(hypotf(B.x - 10.0f, B.y - 10.0f) < 0.01f);
    body_push(&B, 1.0f, 0.0f);                            /* 살짝 밀면 밀려났다가 다리가 붙들어 돌아온다 */
    run(0.05f, 0.0f, 0.0f);
    T_CHECK(B.x > 10.005f);
    run(1.5f, 0.0f, 0.0f);
    T_CHECK(hypotf(B.x - 10.0f, B.y - 10.0f) < 0.03f);
    T_EQ_INT(B.slips, 0);
    body_push(&B, 20.0f, 0.0f);                           /* 세게 밀면 발이 미끄러져 밀려난 자리에 선다 */
    run(1.5f, 0.0f, 0.0f);
    T_CHECK(B.slips > 0);
    T_CHECK(B.x > 10.2f);
    T_CHECK(hypotf(B.vx, B.vy) < 0.05f);
}

static void test_foot(void)
{
    float f[3], g[3];
    setup();
    for (int leg = 0; leg < BODY_LEGS; leg++) {
        float se = C.swing_end[leg % 3];
        body_foot(&C, leg, se * 0.5f, 1.0f, f);           /* 드는 동안의 한가운데: 가장 높이 들었다 */
        T_CHECK(body_swinging(&C, leg, se * 0.5f) && fabsf(f[2] - C.lift) < 1e-3f);
        body_foot(&C, leg, se + 0.1f, 1.0f, f);           /* 딛는 동안: 바닥에 있고, 위상이 갈수록 뒤로 간다 */
        body_foot(&C, leg, se + 1.0f, 1.0f, g);
        T_CHECK(!body_swinging(&C, leg, se + 0.1f) && f[2] == 0.0f && g[0] < f[0]);
        T_CHECK((leg < 3) == (f[1] < 0.0f));              /* 왼쪽 다리는 왼쪽에 */
        body_foot(&C, leg, se + 1.0f, 0.0f, f);           /* 크기가 0이면 서 있는 자리 */
        T_CHECK(fabsf(f[0] - C.foot_x[leg % 3]) < 1e-5f);
    }
}

static void test_same(void)
{
    static body_t a;
    setup();
    run(1.0f, 1.0f, 0.4f);
    body_push(&B, 3.0f, -2.0f);
    run(1.0f, -0.3f, 0.8f);
    a = B;
    setup();
    run(1.0f, 1.0f, 0.4f);
    body_push(&B, 3.0f, -2.0f);
    run(1.0f, -0.3f, 0.8f);
    T_CHECK(memcmp(&a, &B, sizeof a) == 0);
}

int main(void)
{
    test_tripod();
    test_walk();
    test_turn();
    test_push();
    test_foot();
    test_same();
    T_DONE();
}
