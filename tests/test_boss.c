/* 보스전 계산부: 벽, 무기 셋의 거리와 피해와 대기와 탄수, 끈끈이, 돌진, 날갯짓, 끝나는 조건 셋,
 * 참가자 1~3명, 같은 입력이면 같은 결과, 쓰러진 사람의 키 */
#include "boss/boss.h"
#include "t.h"

#include <math.h>
#include <string.h>

enum { EAST = 0, SOUTH = 4, WEST = 8, NORTH = 12 };

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
    S.p[i].x = x; S.p[i].y = y; S.p[i].dir = dir;
    S.p[i].last_dist = sqrtf((x - S.fly.x) * (x - S.fly.x) + (y - S.fly.y) * (y - S.fly.y));
}

static void put_fly(float x, float y)
{
    S.fly.x = x; S.fly.y = y;
    for (int i = 0; i < S.n_players; i++) put_player(i, S.p[i].x, S.p[i].y, S.p[i].dir);
}

static void ticks(int n, const float *rate) { for (int i = 0; i < n; i++) boss_tick(&S, rate); }

static int count_ev(int kind)
{
    int n = 0;
    for (int i = 0; i < S.n_ev; i++) n += S.ev[i].kind == kind;
    return n;
}

static void press(int i, int key) { boss_input(&S, i, key); boss_tick(&S, still); }

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
    press(0, SK_UP);
    T_CHECK(fabsf(S.p[0].y - (15.0f - c.step)) < 1e-4f);
    press(0, SK_LEFT);
    T_EQ_INT(S.p[0].dir, NORTH - 1);
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
    go[BR_FORWARD] = 1.0f;
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
    put_player(0, 8.5f, 12.5f, NORTH);
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
        T_EQ_INT(S.fly.hp_max, (int)c.fly_hp_per_player * n);
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

/* 뇌 대신 쓰는 정해진 규칙(가짜 뇌와 비슷하게) */
static void rule_brain(const float *stim, float *rate)
{
    memset(rate, 0, sizeof(float) * BRAIN_GROUP_COUNT);
    float l = stim[BR_LOOM_L], r = stim[BR_LOOM_R];
    rate[BR_TURN_R] = l > r ? l - r : 0.0f;
    rate[BR_TURN_L] = r > l ? r - l : 0.0f;
    rate[BR_ESCAPE] = l > r ? l : r;
    rate[BR_BACKWARD] = l < r ? l : r;
    rate[BR_FORWARD] = 1.0f - rate[BR_ESCAPE];
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
    put_player(1, 20.0f, 20.0f, NORTH);
    ticks(c.charge_windup + c.charge_ticks + 2, still);
    T_EQ_INT(S.p[0].alive, 0);
    T_EQ_INT(S.outcome, BOSS_GOING);
    float x = S.p[0].x, y = S.p[0].y;
    int dir = S.p[0].dir, traps = S.p[0].traps;
    boss_input(&S, 0, SK_UP); boss_input(&S, 0, SK_LEFT); boss_input(&S, 0, SK_3); boss_input(&S, 0, SK_ACT);
    boss_tick(&S, still);
    T_CHECK(S.p[0].x == x && S.p[0].y == y);
    T_EQ_INT(S.p[0].dir, dir);
    T_EQ_INT(S.p[0].traps, traps);
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
    printf("  스냅숏 %zu바이트\n", sizeof(boss_snap_t));
    T_DONE();
}
