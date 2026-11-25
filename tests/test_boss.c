/* 보스전 계산부: 벽, 무기 셋의 거리와 피해와 대기와 탄수, 끈끈이, 돌진, 날갯짓, 끝나는 조건 셋,
 * 참가자 1~3명, 같은 입력이면 같은 결과, 쓰러진 사람의 키, 옆걸음과 시선과 살금살금 켜기 */
#include "boss/boss.h"
#include "t.h"

#include <math.h>
#include <string.h>

enum { EAST = 0, SOUTH = 4, WEST = 8, NORTH = 12 };
#define DIR_STEP (2.0f * 3.14159265f / BOSS_DIRS)     /* 키로 한 번 도는 각도 */

static const float still[BRAIN_GROUP_COUNT];     /* 뇌의 반응이 모두 0: 초파리가 가만히 있다 */
static boss_state_t S;

/* 돌진과 날갯짓을 끈 설정 */
static boss_cfg_t calm(void)
{
    boss_cfg_t c;
    boss_cfg_default(&c);
    c.charge_range = 0.0f;
    c.wave_period = 1000000;
    return c;
}

static void setup(const boss_cfg_t *c, int n)
{
    int lives[3] = { 3, 3, 3 };
    boss_init(&S, c, 7, n, lives);
}

static void put_player(int i, float x, float y, int dir)
{
    S.p[i].x = x; S.p[i].y = y; S.p[i].ang = (float)dir * DIR_STEP;
    S.p[i].approach = 0.0f;                                      /* 가만히 서 있던 것으로 한다 */
    S.p[i].step_left = 0;
}

static void put_fly(float x, float y)
{
    S.fly.x = x; S.fly.y = y;
    for (int i = 0; i < S.n_players; i++) { S.p[i].approach = 0.0f; S.p[i].step_left = 0; }
}

static void ticks(int n, const float *rate) { for (int i = 0; i < n; i++) boss_tick(&S, rate); }

static int count_ev(int kind)
{
    int n = 0;
    for (int i = 0; i < S.n_ev; i++) n += S.ev[i].kind == kind;
    return n;
}

static void press(int i, int key) { boss_input(&S, i, key); boss_tick(&S, still); }
/* 키를 한 번 누르고 그 걸음이 끝날 때까지(step_ticks) 기다린다 */
static void walk(int i, int key) { press(i, key); ticks(S.cfg.step_ticks - 1, still); }

static void test_walls(void)
{
    boss_cfg_t c = calm();
    setup(&c, 1);
    put_player(0, 1.5f, 1.5f, WEST);
    for (int k = 0; k < 5; k++) press(0, SK_UP);
    T_CHECK(S.p[0].x >= 1.0f + c.player_radius);                 /* 바깥 벽 */
    put_player(0, 4.0f, 5.5f, EAST);                               /* 기둥(5~6, 5~6) 앞 */
    for (int k = 0; k < 5; k++) press(0, SK_UP);
    T_CHECK(S.p[0].x <= 5.0f - c.player_radius + 1e-4f);
    put_player(0, 8.5f, 15.0f, NORTH);                            /* 빈 곳에서는 한 걸음씩 간다 */
    press(0, SK_UP);                                              /* 한 걸음을 step_ticks에 나눠 간다 */
    T_CHECK(fabsf(S.p[0].y - (15.0f - c.step / (float)c.step_ticks)) < 1e-4f);
    ticks(c.step_ticks - 1, still);
    T_CHECK(fabsf(S.p[0].y - (15.0f - c.step)) < 1e-4f);
    ticks(3, still);                                              /* 키가 더 오지 않으면 거기서 선다 */
    T_CHECK(fabsf(S.p[0].y - (15.0f - c.step)) < 1e-4f);
    press(0, SK_LEFT);
    T_CHECK(fabsf(S.p[0].ang - (float)(NORTH - 1) * DIR_STEP) < 1e-4f);
}

static void test_swatter(void)
{
    boss_cfg_t c = calm();
    setup(&c, 1);
    put_fly(8.5f, 9.0f);
    int hp = S.fly.hp;
    put_player(0, 8.5f, 9.0f + c.fly_radius + c.player_radius + 1.0f, NORTH);
    press(0, SK_ACT);
    T_EQ_INT(S.fly.hp, hp - (int)c.swat_damage);
    T_EQ_INT(count_ev(BOSS_EV_SWAT_HIT), 1);
    press(0, SK_ACT);                                             /* 대기 중 */
    T_EQ_INT(S.fly.hp, hp - (int)c.swat_damage);
    ticks(c.swat_cooldown, still);
    press(0, SK_ACT);
    T_EQ_INT(S.fly.hp, hp - 2 * (int)c.swat_damage);
    ticks(c.swat_cooldown, still);
    put_player(0, 8.5f, 9.0f + c.fly_radius + c.player_radius + 1.4f, NORTH);   /* 너무 멀다 */
    press(0, SK_ACT);
    T_EQ_INT(count_ev(BOSS_EV_SWAT_MISS), 1);
    ticks(c.swat_cooldown, still);
    put_player(0, 8.5f, 10.5f, SOUTH);                            /* 등을 돌렸다 */
    press(0, SK_ACT);
    T_EQ_INT(S.fly.hp, hp - 2 * (int)c.swat_damage);
}

static void test_spray(void)
{
    boss_cfg_t c = calm();
    c.fly_hp[0] = 200.0f;                                         /* 스프레이를 다 써도 쓰러지지 않게 */
    setup(&c, 1);
    put_fly(8.5f, 9.0f);
    int hp = S.fly.hp;
    put_player(0, 8.5f, 13.0f, NORTH);
    press(0, SK_2);
    T_EQ_INT(S.p[0].item, BOSS_SPRAY);
    press(0, SK_ACT);
    T_EQ_INT(S.fly.hp, hp - (int)c.spray_damage);
    T_EQ_INT(S.p[0].ammo, c.spray_ammo - 1);
    for (int k = 0; k < 40; k++) { press(0, SK_ACT); ticks(c.spray_cooldown, still); }
    T_EQ_INT(S.p[0].ammo, 0);
    T_EQ_INT(S.fly.hp, hp - c.spray_ammo * (int)c.spray_damage);  /* 다 쓰면 더 나가지 않는다 */

    setup(&c, 1);
    put_fly(8.5f, 9.0f);
    hp = S.fly.hp;
    put_player(0, 8.5f, 9.0f + c.fly_radius + c.spray_range + 0.5f, NORTH);     /* 닿지 않는 거리 */
    press(0, SK_2); press(0, SK_ACT);
    T_EQ_INT(S.fly.hp, hp);
    put_fly(12.0f, 10.0f);
    put_player(0, 12.0f, 14.0f, NORTH);                           /* 가운데 기둥(11~12, 11~12)이 가린다 */
    ticks(c.spray_cooldown, still);
    press(0, SK_ACT);
    T_EQ_INT(S.fly.hp, hp);
    T_EQ_INT(count_ev(BOSS_EV_SPRAY_MISS), 1);
}

static void test_trap(void)
{
    boss_cfg_t c = calm();
    float go[BRAIN_GROUP_COUNT] = { 0 };
    go[BR_WALK_L] = go[BR_WALK_R] = 0.25f;                         /* 걷기 명령: 곧게 걷는다 */
    setup(&c, 1);
    put_fly(8.5f, 9.0f);
    S.fly.ang = 0.0f;                                             /* 오른쪽(+x)을 본다 */
    put_player(0, 8.5f, 15.0f, NORTH);
    press(0, SK_3);
    press(0, SK_ACT);
    T_EQ_INT(S.p[0].traps, c.trap_count - 1);
    int stuck_at = -1;
    for (int k = 0; k < 20 && stuck_at < 0; k++) { boss_tick(&S, still); if (count_ev(BOSS_EV_STUCK)) stuck_at = k; }
    T_CHECK(stuck_at >= 0);
    T_EQ_INT(S.fly.stun, c.stun_ticks - 1);                       /* 붙은 틱도 한 틱으로 센다 */
    float x = S.fly.x;
    ticks(c.stun_ticks - 1, go);                                  /* 붙어 있는 동안은 움직이지 않는다 */
    T_CHECK(S.fly.x == x);
    ticks(3, go);
    T_CHECK(S.fly.x > x);
    ticks(c.swat_cooldown, still);
    press(0, SK_ACT);
    ticks(c.swat_cooldown, still);
    press(0, SK_ACT);                                             /* 2개를 다 썼다 */
    T_EQ_INT(S.p[0].traps, 0);
    T_EQ_INT(count_ev(BOSS_EV_TRAP_THROW), 0);
}

static void test_charge(void)
{
    boss_cfg_t c;
    boss_cfg_default(&c);
    c.wave_period = 1000000;
    setup(&c, 1);
    put_fly(8.5f, 9.0f);
    put_player(0, 8.5f, 12.0f, NORTH);
    boss_tick(&S, still);
    T_EQ_INT(count_ev(BOSS_EV_CHARGE), 1);
    int hit_at = -1;
    for (int k = 0; k < c.charge_windup + c.charge_ticks + 2 && hit_at < 0; k++) {
        boss_tick(&S, still);
        if (count_ev(BOSS_EV_HIT_PLAYER)) hit_at = k;
    }
    T_CHECK(hit_at >= c.charge_windup);                           /* 겨누는 동안에는 부딪히지 않는다 */
    T_EQ_INT(S.p[0].hp, 2);
    T_EQ_INT(S.fly.charge_cool, c.charge_cooldown);
    put_player(0, S.p[0].x, S.p[0].y, NORTH);
    ticks(c.charge_cooldown - 1, still);                          /* 쉬는 동안은 다시 돌진하지 않는다 */
    T_EQ_INT(S.p[0].hp, 2);
    T_EQ_INT(S.fly.windup + S.fly.charging, 0);

    /* 몰아붙이면(빨리 다가오면) 돌진하지 않는다 */
    setup(&c, 1);
    put_fly(8.5f, 9.0f);
    put_player(0, 8.5f, 12.0f, NORTH);
    S.p[0].approach = c.loom_speed;                               /* 달려오던 중이다 */
    for (int k = 0; k < 4; k++) {
        press(0, SK_UP);
        T_EQ_INT(count_ev(BOSS_EV_CHARGE), 0);
    }
}

static void test_wave(void)
{
    boss_cfg_t c = calm();
    c.wave_period = 5;
    setup(&c, 2);
    put_fly(8.5f, 8.0f);
    put_player(0, 8.5f, 10.0f, NORTH);                            /* 2칸: 밀린다 */
    put_player(1, 4.5f, 8.0f, EAST);                              /* 4칸: 그대로 */
    int waves = 0;
    for (int k = 0; k < 5; k++) { boss_tick(&S, still); waves += count_ev(BOSS_EV_WAVE); }
    T_EQ_INT(waves, 1);
    T_CHECK(S.p[0].y >= 10.0f + c.wave_push - 1e-3f);
    T_CHECK(S.p[1].x == 4.5f && S.p[1].y == 8.0f);
    for (int k = 0; k < 15; k++) { boss_tick(&S, still); waves += count_ev(BOSS_EV_WAVE); }
    T_EQ_INT(waves, 4);
}

static void test_end(void)
{
    boss_cfg_t c = calm();
    setup(&c, 1);                                                 /* 초파리 체력 0 */
    put_fly(8.5f, 9.0f);
    S.fly.hp = 5;
    put_player(0, 8.5f, 10.85f, NORTH);
    press(0, SK_ACT);
    T_EQ_INT(S.outcome, BOSS_PLAYERS_WIN);
    T_EQ_INT(S.fly.hp, 0);
    T_EQ_INT(count_ev(BOSS_EV_FLY_DOWN), 1);

    boss_cfg_t c2;                                                /* 전멸 */
    boss_cfg_default(&c2);
    c2.wave_period = 1000000;
    int one[1] = { 1 };
    boss_init(&S, &c2, 3, 1, one);
    put_fly(8.5f, 9.0f);
    put_player(0, 8.5f, 12.0f, NORTH);
    ticks(c2.charge_windup + c2.charge_ticks + 2, still);
    T_EQ_INT(S.p[0].alive, 0);
    T_EQ_INT(S.outcome, BOSS_FLY_WINS);

    c.ticks = 10;                                                 /* 시간 끝 */
    setup(&c, 2);
    ticks(9, still);
    T_EQ_INT(S.outcome, BOSS_GOING);
    ticks(1, still);
    T_EQ_INT(S.outcome, BOSS_FLY_WINS);
    boss_input(&S, 0, SK_UP);                                     /* 끝난 뒤의 키는 쓰지 않는다 */
    T_EQ_INT(S.p[0].move, 0);
}

static void test_players(void)
{
    boss_cfg_t c = calm();
    for (int n = 1; n <= 3; n++) {
        setup(&c, n);
        T_EQ_INT(S.n_players, n);
        T_EQ_INT(S.fly.hp_max, (int)c.fly_hp[n - 1]);              /* 참가자 수마다 따로 정한 체력 */
        boss_input(&S, n, SK_UP);                                 /* 없는 참가자 */
        if (n < 3) T_EQ_INT(S.p[n].move, 0);
        boss_snap_t snap;
        boss_snapshot(&S, &snap);
        T_EQ_INT(snap.n_players, n);
        T_EQ_INT(snap.fly.hp, S.fly.hp);
        T_EQ_INT(snap.p[n - 1].x, (int)(S.p[n - 1].x * BOSS_FIX));
    }
    int lives[3] = { 1, 2, 3 };
    boss_init(&S, &c, 1, 3, lives);
    T_EQ_INT(S.p[0].hp, 1);
    T_EQ_INT(S.p[2].hp, 3);
}

/* 맞으면 때린 쪽에서 센 자극이 들어오고 그동안은 돌진하지 않는다. 가까이 서 있기만 해도 자극이 세다 */
static void test_hurt(void)
{
    boss_cfg_t c;
    float stim[BRAIN_GROUP_COUNT];
    boss_cfg_default(&c);
    c.wave_period = 1000000;
    setup(&c, 1);
    put_fly(8.5f, 9.0f);
    S.fly.ang = 0.0f;                                             /* 오른쪽(+x)을 본다. 아래쪽(+y)이 초파리의 오른쪽이다 */
    put_player(0, 8.5f, 13.0f, NORTH);                            /* 멀리 서 있다: 약하다 */
    boss_stimulus(&S, stim);
    T_CHECK(stim[BR_LOOM_R] < c.rush_level);
    put_player(0, 8.5f, 10.0f, NORTH);                            /* 바로 옆에 서 있다: 세다 */
    boss_stimulus(&S, stim);
    T_CHECK(stim[BR_LOOM_R] >= c.rush_level && stim[BR_LOOM_L] < stim[BR_LOOM_R]);

    put_player(0, 8.5f, 10.85f, NORTH);
    S.fly.charge_cool = 0;
    boss_input(&S, 0, SK_ACT);
    boss_tick(&S, still);
    T_EQ_INT(count_ev(BOSS_EV_SWAT_HIT), 1);
    put_player(0, 8.5f, 13.0f, NORTH);                            /* 때리고 물러나도 */
    int charges = 0;
    for (int k = 0; k < c.hurt_ticks - 2; k++) {              /* 맞은 틱도 한 틱으로 센다 */
        boss_stimulus(&S, stim);
        T_CHECK(stim[BR_LOOM_R] >= 0.9f);                         /* 때린 쪽(오른쪽)에서 센 자극 */
        boss_tick(&S, still);
        charges += count_ev(BOSS_EV_CHARGE);
        put_player(0, 8.5f, 13.0f, NORTH);
    }
    T_EQ_INT(charges, 0);
    ticks(3, still);                                              /* 지나면 다시 덤빈다 */
    T_CHECK(S.fly.windup > 0);
}

/* 떠 있는 동안: 뇌의 꺾기 명령만큼 날아가는 방향이 돌고, 착지 명령이 기준을 넘으면 일찍 내려앉는다.
 * 날아가는 쪽의 벽은 떠 있을 때만 면 자극으로 들어간다 */
static void test_flight(void)
{
    boss_cfg_t c = calm();
    float r[BRAIN_GROUP_COUNT] = { 0 }, stim[BRAIN_GROUP_COUNT];
    c.air_ticks = 30;                                             /* 벽까지 날아갈 만큼 길게 */
    setup(&c, 1);
    put_player(0, 16.0f, 16.0f, NORTH);
    put_fly(8.0f, 9.0f);
    S.fly.ang = 0.0f;                                             /* 오른쪽(+x)을 본다. 벽은 9칸 앞이다 */
    boss_stimulus(&S, stim);
    T_CHECK(stim[BR_SURFACE_L] == 0.0f && stim[BR_SURFACE_R] == 0.0f);    /* 땅에서는 면 자극이 없다 */
    r[BR_ESCAPE] = 1.0f;
    boss_tick(&S, r);                                             /* 앞(+x)으로 뛰어오른다 */
    T_EQ_INT(count_ev(BOSS_EV_TAKEOFF), 1);
    T_CHECK(S.fly.air_dx > 0.99f);
    boss_stimulus(&S, stim);
    float far_l = stim[BR_SURFACE_L];
    T_CHECK(far_l > 0.0f && far_l < 0.2f);                         /* 벽이 멀다: 약하다 */

    memset(r, 0, sizeof r);
    r[BR_AIRTURN_R] = 1.0f;                                       /* 오른쪽으로 꺾어라 */
    boss_tick(&S, r);
    float per_tick = c.air_turn_deg_s * 3.14159265f / 180.0f * c.air_turn_scale / 20.0f;
    T_CHECK(fabsf(atan2f(S.fly.air_dy, S.fly.air_dx) - per_tick) < 1e-3f);
    T_CHECK(fabsf(S.fly.ang - per_tick) < 1e-3f);                  /* 몸도 같이 돈다 */
    r[BR_AIRTURN_R] = 0.0f;
    r[BR_AIRTURN_L] = 1.0f;
    boss_tick(&S, r);
    T_CHECK(fabsf(S.fly.air_dy) < 1e-3f);                          /* 왼쪽으로 같은 만큼: 제자리 */

    memset(r, 0, sizeof r);                                       /* 벽 가까이 가면 면 자극이 세진다 */
    while (S.fly.air > c.air_land_ticks + 1 && S.fly.x < 13.0f) boss_tick(&S, r);
    T_CHECK(S.fly.air > c.air_land_ticks + 1);
    boss_stimulus(&S, stim);
    T_CHECK(stim[BR_SURFACE_L] > far_l + 0.1f && fabsf(stim[BR_SURFACE_L] - stim[BR_SURFACE_R]) < 0.05f);

    r[BR_LAND] = c.land_level - 0.01f;                            /* 기준에 못 미치면 그대로 난다 */
    int left = S.fly.air;
    boss_tick(&S, r);
    T_EQ_INT(S.fly.air, left - 1);
    T_EQ_INT(count_ev(BOSS_EV_DESCEND), 0);
    r[BR_LAND] = 1.0f;                                            /* 착지 명령: 내려앉기 시작한다 */
    boss_tick(&S, r);
    T_EQ_INT(count_ev(BOSS_EV_DESCEND), 1);
    T_EQ_INT(S.fly.air, c.air_land_ticks);
    int landed = 0;
    for (int k = 0; k < c.air_land_ticks; k++) { boss_tick(&S, r); landed += count_ev(BOSS_EV_LAND); }
    T_EQ_INT(landed, 1);
    T_EQ_INT(S.fly.air, 0);
    boss_stimulus(&S, stim);
    T_CHECK(stim[BR_SURFACE_L] == 0.0f);

    setup(&c, 1);                                                 /* 뛰어오른 직후에는 착지 명령이 있어도 내려앉지 않는다 */
    put_player(0, 16.0f, 16.0f, NORTH);
    put_fly(4.0f, 9.0f);
    S.fly.ang = 0.0f;
    memset(r, 0, sizeof r);
    r[BR_ESCAPE] = 1.0f;
    r[BR_LAND] = 1.0f;
    boss_tick(&S, r);
    for (int k = 0; k < c.air_min_ticks - 1; k++) { boss_tick(&S, r); T_EQ_INT(count_ev(BOSS_EV_DESCEND), 0); }
    boss_tick(&S, r);
    T_EQ_INT(count_ev(BOSS_EV_DESCEND), 1);
}

/* 도약: 달아남 반응이 기준을 넘을 때만 뛴다. 방향은 뇌의 방향 반응을 따른다. 붙어 있으면 못 뛰고, 돌진도 끊는다 */
static void test_takeoff(void)
{
    boss_cfg_t c = calm();
    float r[BRAIN_GROUP_COUNT] = { 0 };
    setup(&c, 1);
    put_player(0, 16.0f, 16.0f, NORTH);
    put_fly(8.5f, 9.0f);
    S.fly.ang = 0.0f;                                             /* 오른쪽(+x)을 본다 */
    r[BR_ESCAPE] = c.takeoff_level - 0.01f;                       /* 기준에 못 미친다 */
    boss_tick(&S, r);
    T_EQ_INT(count_ev(BOSS_EV_TAKEOFF), 0);

    put_fly(8.5f, 9.0f);
    S.fly.v = 0.0f;
    S.fly.ang = 0.0f;                                             /* 위에서 한 틱 걸으며 몸이 조금 돌았을 수 있다 */
    r[BR_ESCAPE] = 1.0f;
    r[BR_BACKWARD] = 1.0f;                                        /* 앞에서 다가왔다: 뒤(-x)로 뛴다 */
    boss_tick(&S, r);
    T_EQ_INT(count_ev(BOSS_EV_TAKEOFF), 1);
    T_EQ_INT(S.fly.air, c.air_ticks);
    int landed = 0;
    for (int k = 0; k < c.air_ticks; k++) { boss_tick(&S, r); landed += count_ev(BOSS_EV_LAND); }
    T_EQ_INT(landed, 1);
    T_CHECK(S.fly.x < 8.5f - 3.0f);                               /* 걷기로는 갈 수 없는 거리 */
    T_CHECK(S.fly.x >= 1.0f + c.fly_radius - 1e-3f);              /* 벽은 넘지 않는다 */
    T_CHECK(fabsf(S.fly.y - 9.0f) < 0.01f);
    int again = 0;
    for (int k = 0; k < c.air_rest_ticks - 1; k++) { boss_tick(&S, r); again += count_ev(BOSS_EV_TAKEOFF); }
    T_EQ_INT(again, 0);                                           /* 내려앉은 뒤에는 한동안 못 뛴다 */
    boss_tick(&S, r);
    T_EQ_INT(count_ev(BOSS_EV_TAKEOFF), 1);

    setup(&c, 1);                                                 /* 왼쪽에서 다가왔다: 오른쪽 앞으로 뛴다 */
    put_player(0, 16.0f, 16.0f, NORTH);
    put_fly(8.5f, 9.0f);
    S.fly.ang = 0.0f;
    memset(r, 0, sizeof r);
    r[BR_ESCAPE] = 1.0f;
    r[BR_TURN_R] = 1.0f;
    boss_tick(&S, r);
    T_CHECK(S.fly.air_dx > 0.5f && S.fly.air_dy > 0.5f);

    setup(&c, 1);                                                 /* 끈끈이에 붙어 있으면 못 뛴다 */
    S.fly.stun = 5;
    boss_tick(&S, r);
    T_EQ_INT(count_ev(BOSS_EV_TAKEOFF), 0);

    boss_cfg_default(&c);                                         /* 돌진하려고 겨누다가도 뛴다 */
    c.wave_period = 1000000;
    setup(&c, 1);
    put_fly(8.5f, 9.0f);
    put_player(0, 8.5f, 12.0f, NORTH);
    boss_tick(&S, still);
    T_CHECK(S.fly.windup > 0);
    boss_tick(&S, r);
    T_EQ_INT(count_ev(BOSS_EV_TAKEOFF), 1);
    T_EQ_INT(S.fly.windup, 0);
    T_EQ_INT(S.fly.charge_cool, c.charge_cooldown);
}

/* 살금살금: 걸음이 짧고, 누르고 있어도 걸음 속도는 정해져 있고, 천천히 다가가면 자극이 약하다 */
static void test_sneak(void)
{
    boss_cfg_t c = calm();
    float stim[BRAIN_GROUP_COUNT];
    setup(&c, 1);
    put_fly(8.5f, 9.0f);
    S.fly.ang = 0.0f;
    put_player(0, 8.5f, 15.0f, NORTH);
    walk(0, SK_UP | BOSS_SLOW);
    T_CHECK(fabsf(S.p[0].y - (15.0f - c.slow_step)) < 1e-4f);     /* 짧은 걸음 */
    put_player(0, 8.5f, 15.0f, NORTH);
    for (int k = 0; k < c.step_ticks * 4; k++) press(0, SK_UP);   /* 틱마다 눌러도 step_ticks에 한 걸음 */
    T_CHECK(fabsf(S.p[0].y - (15.0f - 4.0f * c.step)) < 1e-3f);

    /* 같은 자리(파리채가 닿는 거리)까지 왔을 때: 걸어왔으면 뛰어오를 만큼 세고, 살금살금 왔으면 약하다.
     * 기준은 진짜 뇌다. 진짜 뇌는 다가옴 자극 0.23쯤에서 달아남 반응이 0.5를 넘는다 */
    const float jump_stim = 0.23f;
    float reach = c.fly_radius + c.player_radius + c.swat_range - 0.05f;
    put_player(0, 8.5f, 9.0f + reach + 3.0f * c.step, NORTH);
    for (int k = 0; k < c.step_ticks * 3; k++) press(0, SK_UP);
    boss_stimulus(&S, stim);
    T_CHECK(stim[BR_LOOM_R] >= jump_stim);
    put_player(0, 8.5f, 9.0f + reach + 10.0f * c.slow_step, NORTH);
    for (int k = 0; k < c.step_ticks * 10; k++) press(0, SK_UP | BOSS_SLOW);
    T_CHECK(fabsf(S.p[0].y - (9.0f + reach)) < 1e-3f);
    boss_stimulus(&S, stim);
    T_CHECK(stim[BR_LOOM_R] < jump_stim);
    int hp = S.fly.hp;
    press(0, SK_ACT);                                             /* 그 자리에서 파리채가 닿는다 */
    T_EQ_INT(S.fly.hp, hp - (int)c.swat_damage);
}

/* 뇌 대신 쓰는 정해진 규칙(가짜 뇌와 비슷하게) */
static void rule_brain(const float *stim, float *rate)
{
    memset(rate, 0, sizeof(float) * BRAIN_GROUP_COUNT);
    float l = stim[BR_LOOM_L], r = stim[BR_LOOM_R];
    rate[BR_TURN_R] = l > r ? l - r : 0.0f;
    rate[BR_TURN_L] = r > l ? r - l : 0.0f;
    rate[BR_ESCAPE] = l > r ? l : r;
    rate[BR_BACKWARD] = l < r ? l : r;
    rate[BR_WALK_L] = rate[BR_WALK_R] = 0.25f * (1.0f - rate[BR_ESCAPE]);
    rate[BR_STEER_L] = rate[BR_TURN_L];
    rate[BR_STEER_R] = rate[BR_TURN_R];
}

static void run_script(boss_state_t *s, boss_snap_t *snaps, int n)
{
    static const int keys[] = { SK_UP, SK_UP, SK_LEFT, SK_ACT, SK_2, SK_ACT, SK_RIGHT, SK_UP, SK_3, SK_ACT, SK_DOWN };
    boss_cfg_t c;
    boss_cfg_default(&c);
    int lives[3] = { 3, 2, 1 };
    boss_init(s, &c, 1234, 3, lives);
    for (int t = 0; t < n; t++) {
        float stim[BRAIN_GROUP_COUNT], rate[BRAIN_GROUP_COUNT];
        for (int i = 0; i < 3; i++) boss_input(s, i, keys[(t * 3 + i * 5) % 11]);
        boss_stimulus(s, stim);
        rule_brain(stim, rate);
        boss_tick(s, rate);
        boss_snapshot(s, &snaps[t]);
    }
}

static void test_same(void)
{
    static boss_state_t a, b;
    static boss_snap_t sa[600], sb[600];
    run_script(&a, sa, 600);
    run_script(&b, sb, 600);
    T_CHECK(memcmp(sa, sb, sizeof sa) == 0);
}

static void test_dead_input(void)
{
    boss_cfg_t c;
    boss_cfg_default(&c);
    c.wave_period = 1000000;
    int lives[2] = { 1, 3 };
    boss_init(&S, &c, 5, 2, lives);
    put_fly(8.5f, 9.0f);
    put_player(0, 8.5f, 12.0f, NORTH);
    put_player(1, 16.0f, 16.0f, NORTH);
    ticks(c.charge_windup + c.charge_ticks + 2, still);
    T_EQ_INT(S.p[0].alive, 0);
    T_EQ_INT(S.outcome, BOSS_GOING);
    float x = S.p[0].x, y = S.p[0].y, ang = S.p[0].ang;
    int traps = S.p[0].traps;
    boss_input(&S, 0, SK_UP); boss_input(&S, 0, SK_LEFT); boss_input(&S, 0, SK_3); boss_input(&S, 0, SK_ACT);
    boss_input(&S, 0, SK_RIGHT | BOSS_STRAFE); boss_input(&S, 0, BOSS_SNEAK);
    boss_look(&S, 0, 1.0f);
    boss_tick(&S, still);
    T_CHECK(S.p[0].x == x && S.p[0].y == y);
    T_CHECK(S.p[0].ang == ang);
    T_EQ_INT(S.p[0].sneak, 0);
    T_EQ_INT(S.p[0].traps, traps);
}

/* 막기: 막는 자세일 때 돌진에 닿으면 다치지 않고 초파리가 멈춘다. 한 번 쓰면 한동안 못 쓴다 */
static void test_guard(void)
{
    boss_cfg_t c;
    boss_cfg_default(&c);
    c.wave_period = 1000000;
    setup(&c, 1);
    put_fly(8.5f, 9.0f);
    put_player(0, 8.5f, 12.0f, NORTH);
    boss_tick(&S, still);
    T_EQ_INT(count_ev(BOSS_EV_CHARGE), 1);
    ticks(c.charge_windup, still);                                /* 겨누기가 끝나고 달려오기 시작한다 */
    boss_input(&S, 0, BOSS_GUARD);
    T_EQ_INT(S.p[0].guard, c.guard_ticks);
    int blocked = 0;
    for (int k = 0; k < c.guard_ticks && !blocked; k++) { boss_tick(&S, still); blocked += count_ev(BOSS_EV_BLOCK); }
    T_EQ_INT(blocked, 1);
    T_EQ_INT(S.p[0].hp, 3);                                       /* 다치지 않았다 */
    T_CHECK(S.fly.stun > 0 && S.fly.charging == 0);
    boss_input(&S, 0, BOSS_GUARD);                                /* 쉬는 중에는 다시 못 쓴다 */
    T_EQ_INT(S.p[0].guard, 0);
    ticks(c.guard_cooldown, still);
    T_EQ_INT(S.p[0].guard_cool, 0);
    boss_snap_t snap;
    boss_input(&S, 0, BOSS_GUARD);
    boss_snapshot(&S, &snap);
    T_EQ_INT(snap.p[0].guard, c.guard_ticks);
}

/* 뛰어오르기: 포물선으로 올라갔다 내려오고, 높이 떠 있을 때 닿은 돌진은 발밑으로 지나간다 */
static void test_jump(void)
{
    boss_cfg_t c;
    boss_cfg_default(&c);
    c.wave_period = 1000000;
    c.charge_range = 0.0f;                                        /* 먼저 뛰는 것만 본다 */
    setup(&c, 1);
    put_fly(8.5f, 3.0f);
    put_player(0, 8.5f, 12.0f, NORTH);
    boss_input(&S, 0, BOSS_JUMP);
    T_EQ_INT(S.p[0].jump, c.jump_ticks);
    float top = 0.0f;
    for (int k = 0; k < c.jump_ticks; k++) {
        boss_tick(&S, still);
        if (S.p[0].z > top) top = S.p[0].z;
        if (k == c.jump_ticks / 2) boss_input(&S, 0, BOSS_JUMP);  /* 한창 떠 있을 때 누른 것은 내려서기 전에 잊는다 */
    }
    T_CHECK(top > c.jump_height * 0.9f && top <= c.jump_height + 1e-4f);
    T_CHECK(S.p[0].z == 0.0f && S.p[0].jump == 0);
    boss_tick(&S, still);
    T_EQ_INT(S.p[0].jump, 0);                                     /* 잊었다 */
    boss_input(&S, 0, BOSS_JUMP);                                 /* 내려선 뒤에는 바로 다시 뛸 수 있다 */
    T_EQ_INT(S.p[0].jump, c.jump_ticks);
    ticks(c.jump_ticks - 2, still);
    boss_input(&S, 0, BOSS_JUMP);                                 /* 내려서기 직전에 누르면 기억해 두었다가 내려서자마자 뛴다 */
    T_CHECK(S.p[0].jump > 0 && S.p[0].jump_buf > 0);
    ticks(2, still);
    T_EQ_INT(S.p[0].jump, c.jump_ticks);
    T_CHECK(boss_jump_curve(0.3f) > boss_jump_curve(0.8f));       /* 올라갈 때보다 빨리 떨어진다 */
    boss_snap_t snap;
    ticks(c.jump_ticks / 2, still);
    boss_snapshot(&S, &snap);
    T_CHECK(snap.p[0].z > 80);                                    /* 꼭대기 근처. 높이는 칸의 1/100로 보낸다 */

    /* 걷다가 뛰면 걷던 빠르기와 방향 그대로 날아간다(키가 더 오지 않아도). 서서 뛰면 제자리다 */
    put_player(0, 8.5f, 15.0f, NORTH);
    ticks(c.jump_ticks + 10, still);
    press(0, SK_UP);
    boss_input(&S, 0, BOSS_JUMP);
    float from = S.p[0].y, per_tick = c.step / (float)c.step_ticks;
    ticks(c.jump_ticks, still);
    T_CHECK(S.p[0].y < from - per_tick * (float)(c.jump_ticks - 1) + 1e-3f);
    T_CHECK(fabsf(S.p[0].x - 8.5f) < 1e-3f);
    from = S.p[0].y;
    ticks(6, still);                                              /* 내려서면 멈춘다 */
    T_CHECK(fabsf(S.p[0].y - from) < per_tick + 1e-3f);
    from = S.p[0].y;
    boss_input(&S, 0, BOSS_JUMP);                                 /* 서서 뛴다 */
    ticks(c.jump_ticks, still);
    T_CHECK(S.p[0].y == from);
    boss_input(&S, 0, BOSS_JUMP);                                 /* 떠 있을 때 누른 이동 키는 날아가는 쪽을 바꾼다 */
    boss_tick(&S, still);
    boss_input(&S, 0, SK_RIGHT | BOSS_STRAFE);
    ticks(c.jump_ticks - 1, still);
    T_CHECK(S.p[0].x > 8.5f + per_tick * (float)(c.jump_ticks - 3));

    boss_cfg_default(&c);                                         /* 돌진을 뛰어넘는다 */
    c.wave_period = 1000000;
    setup(&c, 1);
    put_fly(8.5f, 9.0f);
    put_player(0, 8.5f, 12.0f, NORTH);
    boss_tick(&S, still);
    T_EQ_INT(count_ev(BOSS_EV_CHARGE), 1);
    ticks(c.charge_windup, still);                                /* 겨누기가 끝나고 달려오기 시작한다 */
    boss_input(&S, 0, BOSS_JUMP);
    int dodged = 0, hurt = 0;
    for (int k = 0; k < c.charge_ticks + 2; k++) {
        boss_tick(&S, still);
        dodged += count_ev(BOSS_EV_DODGE);
        hurt += count_ev(BOSS_EV_HIT_PLAYER);
    }
    T_EQ_INT(dodged, 1);                                          /* 한 번만 알린다 */
    T_EQ_INT(hurt, 0);
    T_EQ_INT(S.p[0].hp, 3);
    T_EQ_INT(S.fly.stun, 0);                                      /* 막기와 달리 초파리는 멈추지 않는다 */

    setup(&c, 1);                                                 /* 뛰지 않으면 맞는다 */
    put_fly(8.5f, 9.0f);
    put_player(0, 8.5f, 12.0f, NORTH);
    ticks(c.charge_windup + c.charge_ticks + 2, still);
    T_EQ_INT(S.p[0].hp, 2);
}

/* 먹이: 놓으면 초파리가 와서, 닿았을 때 뇌의 먹기 반응이 있으면 먹는다. 먹는 중에 맞으면 먹이를 버린다 */
static void test_bait(void)
{
    boss_cfg_t c = calm();
    float eat[BRAIN_GROUP_COUNT] = { 0 }, walk[BRAIN_GROUP_COUNT] = { 0 }, stim[BRAIN_GROUP_COUNT];
    eat[BR_FEED] = 1.0f;
    walk[BR_WALK_L] = walk[BR_WALK_R] = 1.0f;                     /* 뇌의 걷기 명령: 곧게 걷는다 */
    setup(&c, 1);
    put_fly(8.5f, 9.0f);
    put_player(0, 8.5f, 14.5f, NORTH);
    press(0, SK_4);
    T_EQ_INT(S.p[0].item, BOSS_BAIT);
    press(0, SK_ACT);                                             /* 보는 쪽 앞에 놓인다 */
    T_EQ_INT(S.p[0].baits, c.bait_count - 1);
    T_CHECK(S.bait[0].on && fabsf(S.bait[0].x - 8.5f) < 0.01f && fabsf(S.bait[0].y - (14.5f - c.bait_throw)) < 0.15f);
    boss_stimulus(&S, stim);
    T_CHECK(stim[BR_SUGAR] == 0.0f);                              /* 아직 닿지 않았다 */
    /* 먹이가 물체로 보인다. 초파리는 아래(먹이 쪽)를 보고 있으므로 정면이다: 좌우에 같은 세기로 들어간다 */
    T_CHECK(stim[BR_OBJECT_L] > 0.2f && fabsf(stim[BR_OBJECT_L] - stim[BR_OBJECT_R]) < 0.05f);
    float d0 = S.bait[0].y - S.fly.y;
    ticks(4, still);                                              /* 걷기 명령이 없으면 걷지 않는다 */
    T_CHECK(fabsf(S.bait[0].y - S.fly.y - d0) < 0.01f);
    ticks(8, walk);                                               /* 걷기 명령이 켜지면 다가간다 */
    T_CHECK(S.fly.lured && S.bait[0].y - S.fly.y < d0 - 0.5f);
    for (int k = 0; k < 40 && S.bait[0].y - S.fly.y > c.fly_radius + 0.1f; k++) boss_tick(&S, walk);
    ticks(3, still);                                              /* 닿았지만 먹기 반응이 없으면 먹지 않는다 */
    boss_stimulus(&S, stim);
    T_CHECK(stim[BR_SUGAR] > 0.5f);
    T_EQ_INT(S.fly.feeding, 0);
    T_EQ_INT(S.bait[0].left, c.bait_ticks);
    boss_tick(&S, eat);                                           /* 먹기 반응이 나오면 먹는다 */
    T_EQ_INT(S.fly.feeding, 1);
    T_EQ_INT(count_ev(BOSS_EV_EAT), 1);
    ticks(4, eat);
    T_EQ_INT(S.bait[0].left, c.bait_ticks - 5);
    boss_snap_t snap;
    boss_snapshot(&S, &snap);
    T_CHECK(snap.n_bait == 1 && snap.fly.feeding == 1 && snap.p[0].baits == c.bait_count - 1);
    int hp = S.fly.hp;                                            /* 먹는 중에 맞으면 먹이를 버리고 한동안 찾지 않는다 */
    put_player(0, S.fly.x, S.fly.y + c.fly_radius + c.player_radius + 0.5f, NORTH);
    press(0, SK_1);
    press(0, SK_ACT);
    T_EQ_INT(S.fly.hp, hp - (int)c.swat_damage);
    T_EQ_INT(S.bait[0].on, 0);
    T_CHECK(S.fly.wary > 0);

    setup(&c, 1);                                                 /* 참가자도 물체로 보인다: 초파리의 왼쪽에 서면 왼쪽이 세다 */
    put_fly(8.5f, 9.0f);                                          /* 초파리는 아래(+y)를 본다. 그 왼쪽은 +x 쪽이다 */
    put_player(0, 12.5f, 9.0f, NORTH);
    boss_stimulus(&S, stim);
    T_CHECK(stim[BR_OBJECT_L] > stim[BR_OBJECT_R] + 0.1f);
    walk[BR_WALK_R] = 0.0f;                                       /* 왼쪽 걷기 명령만 켜지면 왼쪽으로 휘어 걷는다 */
    float a0 = S.fly.ang;
    ticks(6, walk);
    T_CHECK(S.fly.ang < a0 - 0.1f);
    walk[BR_WALK_R] = 1.0f;

    setup(&c, 1);                                                 /* 다 먹으면 먹이가 없어지고 체력이 조금 돌아온다 */
    put_fly(8.5f, 9.0f);
    put_player(0, 8.5f, 12.0f, NORTH);
    S.fly.hp -= 20;
    hp = S.fly.hp;
    press(0, SK_4);
    press(0, SK_ACT);
    ticks(c.bait_ticks + 2, eat);
    T_EQ_INT(S.bait[0].on, 0);
    T_EQ_INT(S.fly.hp, hp + (int)c.bait_heal);
}

/* 옆걸음, 마우스로 정하는 시선, 살금살금 켜고 끄기 */
static void test_look_strafe(void)
{
    boss_cfg_t c = calm();
    boss_snap_t snap;
    setup(&c, 1);
    put_fly(3.0f, 3.0f);
    put_player(0, 8.5f, 15.0f, NORTH);
    walk(0, SK_RIGHT | BOSS_STRAFE);                              /* 위를 보고 있으면 오른쪽은 +x다 */
    T_CHECK(fabsf(S.p[0].x - (8.5f + c.step)) < 1e-4f && fabsf(S.p[0].y - 15.0f) < 1e-4f);
    T_CHECK(fabsf(S.p[0].ang - (float)NORTH * DIR_STEP) < 1e-4f); /* 시선은 그대로다 */
    walk(0, SK_LEFT | BOSS_STRAFE);
    T_CHECK(fabsf(S.p[0].x - 8.5f) < 1e-4f);
    boss_input(&S, 0, SK_UP);                                     /* 앞과 옆이 같이 오면 비스듬히 한 걸음 */
    boss_input(&S, 0, SK_RIGHT | BOSS_STRAFE);
    boss_tick(&S, still);
    ticks(c.step_ticks - 1, still);
    T_CHECK(fabsf(hypotf(S.p[0].x - 8.5f, S.p[0].y - 15.0f) - c.step) < 1e-4f);
    T_CHECK(S.p[0].x > 8.5f + 0.3f && S.p[0].y < 15.0f - 0.3f);

    put_player(0, 8.5f, 15.0f, NORTH);
    boss_look(&S, 0, 0.3f);                                       /* 시선: 다음 틱에 그쪽을 본다 */
    T_CHECK(fabsf(S.p[0].ang - (float)NORTH * DIR_STEP) < 1e-4f);
    boss_tick(&S, still);
    T_CHECK(fabsf(S.p[0].ang - 0.3f) < 1e-5f);
    boss_snapshot(&S, &snap);
    T_EQ_INT(snap.p[0].ang, (int)lroundf(0.3f / (2.0f * 3.14159265f) * 65536.0f));
    boss_look(&S, 0, -0.5f);                                      /* 범위 밖의 각도는 한 바퀴 안으로 옮긴다 */
    boss_tick(&S, still);
    T_CHECK(fabsf(S.p[0].ang - (2.0f * 3.14159265f - 0.5f)) < 1e-4f);
    boss_look(&S, 0, 0.0f);                                       /* 오른쪽(+x)을 보고 앞으로 */
    walk(0, SK_UP);
    T_CHECK(fabsf(S.p[0].x - (8.5f + c.step)) < 1e-4f && fabsf(S.p[0].y - 15.0f) < 1e-4f);

    put_player(0, 8.5f, 15.0f, NORTH);                            /* 살금살금을 켜면 모든 걸음이 짧다 */
    boss_input(&S, 0, BOSS_SNEAK);
    walk(0, SK_UP);
    T_CHECK(fabsf(S.p[0].y - (15.0f - c.slow_step)) < 1e-4f);
    walk(0, SK_RIGHT | BOSS_STRAFE);
    T_CHECK(fabsf(S.p[0].x - (8.5f + c.slow_step)) < 1e-4f);
    boss_snapshot(&S, &snap);
    T_EQ_INT(snap.p[0].sneak, 1);
    boss_input(&S, 0, BOSS_SNEAK);                                /* 다시 누르면 꺼진다 */
    put_player(0, 8.5f, 15.0f, NORTH);
    walk(0, SK_UP);
    T_CHECK(fabsf(S.p[0].y - (15.0f - c.step)) < 1e-4f);
}

int main(void)
{
    test_walls();
    test_swatter();
    test_spray();
    test_trap();
    test_charge();
    test_wave();
    test_end();
    test_players();
    test_same();
    test_dead_input();
    test_hurt();
    test_takeoff();
    test_flight();
    test_sneak();
    test_look_strafe();
    test_guard();
    test_jump();
    test_bait();
    printf("  스냅숏 %zu바이트\n", sizeof(boss_snap_t));
    T_DONE();
}
