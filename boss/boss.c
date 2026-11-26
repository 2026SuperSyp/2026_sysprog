/* 보스전 계산부. 규칙과 수치의 뜻은 boss.h와 boss/config.ini에 있다.
 *
 * 초파리의 움직임은 뇌가 정한다. 계산부는 장면을 자극으로 바꿔 줄 뿐이다.
 *   참가자가 다가온다   -> 그쪽의 다가옴 자극(BR_LOOM_L/R). 빨리 다가올수록, 가까울수록 세다.
 *                          빠르기는 최근 0.5초의 평균이다. 살금살금 다가가면 약해서 초파리가 뛰어오르지 않는다
 *                          (다가옴을 보는 뉴런 가운데 LC4는 빠르기에, LPLC2는 크기에 반응한다)
 * 뇌의 반응: 한쪽에서 다가오면 반대쪽으로 돌아 달아나고(BR_TURN_*, BR_ESCAPE), 양쪽이면 뒤로 간다(BR_BACKWARD).
 *   참가자가 아주 가깝다 -> 서 있기만 해도 그쪽의 다가옴 자극(큰 물체가 눈앞에 있다)
 *   맞았다              -> 한동안 때린 쪽에서 가장 센 다가옴 자극(건드리면 달아난다)
 * 도약도 뇌가 정한다. 뇌의 달아남 출력(BR_ESCAPE)은 진짜 뇌에서 거대 섬유 뉴런(DNp01)의 발화다. 이 뉴런은 다가옴을
 * 보는 시각 뉴런(LC4, LPLC2)에서 입력을 받고, 발화하면 하던 행동을 끊고 뛰어오르게 한다(von Reyn 외 2014).
 * 그래서 그 출력이 발화 기준(takeoff_level)을 넘으면 돌진 중이어도 뛰어오른다. 뛰는 방향은 그 순간 뇌의 방향 출력
 * (좌우 회전, 후진)을 합친 것이다: 한쪽에서 다가오면 반대쪽 앞으로, 앞에서 다가오면 뒤로 뛴다. 빠르기는 잰 값이다.
 * 떠 있는 동안에도 뇌가 정한다. 다가옴 자극이 한쪽에서 계속 오면 날면서 피해 도는 명령 뉴런(DNp06, BR_AIRTURN_L/R)이
 * 켜지고 그쪽으로 꺾는다(Kim 외 2023). 날아가는 쪽에 벽이 다가오면 그 면을 자극(BR_SURFACE_L/R, 시각 뉴런 LPLC4)으로
 * 주고, 착지 명령 뉴런(DNp07, BR_LAND)의 반응이 land_level을 넘으면 다리를 뻗고 그 벽으로 가서 붙는다(Ache 외 2019).
 * 실제로 이 뉴런들은 날고 있을 때만 반응한다. 이 뇌 모델에는 그 조절이 없어서, 떠 있을 때만 그 자극을 주고
 * 그 반응을 읽는 것은 계산부가 한다. 가장 오래 떠 있는 시간, 내려앉는 데 걸리는 시간, 내려앉은 뒤 쉬는 시간은
 * 게임 규칙이다(실제 초파리는 그대로 날아가 버린다).
 * 자극의 세기는 진짜 뇌에 맞췄다. 진짜 뇌는 다가옴 자극 0.23쯤에서 달아남 출력이 발화 기준 0.5를 넘는다
 * (make bossbot BRAIN=real의 첫 줄이 이 곡선이다). 그래서 걸어서 다가오면 0.23을 넘고, 살금살금 다가오거나
 * 가만히 서 있을 때는 넘지 않게 잡았다. 가짜 뇌는 덜 예민해서(0.42쯤) 덜 뛴다.
 * 걷는 것도 뇌가 정한다. 초파리의 눈에 보이는 물체(참가자, 먹이)를 물체 자극(BR_OBJECT_L/R)으로 주면, 진짜 뇌에서는
 * 물체를 보는 시각 뉴런(LC9)이 같은 쪽의 걷기 명령 뉴런(DNp09)을 켠다. DNp09는 뇌가 몸으로 내려보내는 뉴런이고,
 * 켜지면 앞으로 걸으며 그쪽으로 휜다(Bidaye 외 2020). 그 반응(BR_WALK_L/R)이 걷는 빠르기이고, 좌우의 차이와
 * 방향 전환 명령 뉴런(DNa02, BR_STEER_L/R)이 도는 빠르기다. 그래서 초파리는 보이는 사람과 먹이 쪽으로 걸어오고,
 * 아무것도 보이지 않으면 걷지 않는다. 빨리 다가오면 위의 달아남 회로가 먼저 켜진다.
 * 공격할 때는 계산부의 규칙이 정한다. 몰리지 않을 때(다가옴 자극이 rush_level보다 약할 때)만 가까운 참가자에게
 * 돌진한다. 그래서 "달려드는 사람은 피하고, 몰리지 않으면 덤빈다"로 보인다.
 * 벽은 뇌에 넣지 않는다. 다가옴 자극은 도약 회로의 입력이라 벽을 거기에 넣으면 벽을 볼 때마다 뛰어오른다
 * (진짜 뇌는 양쪽에 0.15씩만 들어와도 뛴다). 벽을 느끼는 감각(더듬이, 시야의 흐름)은 이 뇌 모델의 입력에 없으므로
 * 몸의 규칙으로 둔다: 앞쪽 좌우로 벽을 더듬어 가까운 쪽의 반대로 돈다. 그래도 오래 못 가면(구석에 낀 경우)
 * 무작위로 크게 돈다. 둘 다 뇌의 답을 바꾸는 것이 아니라 계산부의 규칙이다. */
#include "boss/boss.h"
#include "core/cfg.h"

#include <math.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#define PI_F 3.14159265f
#define BAIT_TOUCH 0.2f              /* 초파리의 몸이 먹이에 닿았다고 보는 틈(칸) */
#define SURFACE_ANGLE 0.35f          /* 떠 있을 때 날아가는 쪽에서 좌우로 이만큼(20도) 벌려 벽을 본다 */

/* ---- 설정 ---------------------------------------------------------------------- */
void boss_cfg_default(boss_cfg_t *c)
{
    memset(c, 0, sizeof *c);
    c->ticks = 180 * 20;
    c->fly_hp[0] = 40.0f; c->fly_hp[1] = 120.0f; c->fly_hp[2] = 180.0f;
    c->fly_radius = 0.6f;
    c->player_radius = 0.25f;
    c->step = 0.5f; c->slow_step = 0.1f; c->step_ticks = 3;
    c->body_mm = 2.5f;
    c->walk_mm_s = 28.0f; c->run_mm_s = 44.7f; c->back_mm_s = 7.2f;
    c->turn_deg_s = 200.0f;
    c->step_hz = 4.5f; c->cpg_couple = 10.0f; c->cpg_converge = 20.0f;
    c->swing_front = 2.37f; c->swing_mid = 2.23f; c->swing_hind = 1.95f;
    c->foot_lift = 0.12f; c->foot_fore = 0.48f; c->foot_side = 0.45f;
    c->body_spring_hz = 8.0f; c->body_damping = 0.7f; c->leg_grip = 60.0f; c->hit_push = 2.0f;
    c->speed_scale = 0.22f; c->turn_scale = 1.0f;
    c->takeoff_level = 0.5f; c->takeoff_mm_s = 480.0f; c->air_ticks = 12; c->air_rest_ticks = 20;
    c->takeoff_scale = 0.055f;
    c->trap_loom = 1.0f; c->trap_see = 4.0f; c->stun_struggle = 1;
    c->air_turn_deg_s = 1000.0f; c->air_turn_scale = 0.3f; c->land_level = 0.5f; c->surface_gain = 0.07f;
    c->air_min_ticks = 4; c->air_land_ticks = 3;
    c->surface_max = 0.5f; c->perch_rest_ticks = 14;
    c->approach_speed = 0.6f; c->approach_ticks = 14; c->perch_min_ticks = 20; c->perch_walk = 0.1f;
    c->perch_height = 0.45f;
    c->air_height = 1.1f; c->pitch_climb_deg = 40.0f; c->pitch_cruise_deg = 10.0f; c->pitch_flare_deg = 45.0f; c->bank_deg = 55.0f;
    c->guard_ticks = 10; c->guard_cooldown = 50; c->guard_stun = 24;
    c->jump_ticks = 10; c->jump_rest = 0; c->jump_buffer = 3; c->jump_carry = 4; c->jump_height = 1.0f; c->jump_clear = 0.35f;
    c->bait_count = 3; c->bait_ticks = 60; c->bait_wary = 100;
    c->bait_throw = 3.0f; c->bait_smell = 8.0f; c->bait_sugar = 1.0f; c->bait_heal = 5.0f;
    c->see_range = 12.0f; c->see_player = 0.35f; c->see_bait = 1.0f; c->pursue_turn = 1.0f;
    c->feed_level = 0.5f;
    c->swat_range = 1.2f;  c->swat_damage = 10.0f; c->swat_cooldown = 8;
    c->spray_range = 5.0f; c->spray_damage = 3.0f; c->spray_ammo = 20; c->spray_cooldown = 8;
    c->trap_speed = 0.6f;  c->trap_range = 8.0f;  c->trap_count = 2; c->stun_ticks = 60;
    c->charge_range = 5.0f; c->charge_speed = 0.35f; c->charge_windup = 12; c->charge_ticks = 20;
    c->charge_cooldown = 80;
    c->wave_radius = 3.0f; c->wave_push = 2.5f; c->wave_period = 200;
    c->loom_range = 5.0f;  c->wall_sense = 1.5f; c->rush_level = 0.15f; c->wall_turn = 1.0f;
    c->loom_base = 0.08f;  c->loom_gain = 0.55f; c->loom_speed = 0.15f; c->loom_avg_ticks = 10;
    c->near_range = 1.5f;  c->near_gain = 0.12f; c->hurt_ticks = 20;
    c->phase2_hp = 0.5f;   c->phase2_ticks = 600;
}

void boss_cfg_load(boss_cfg_t *c, const char *path)
{
    boss_cfg_default(c);
    boss_cfg_apply(c, path);
    /* 훈련 결과(make boss-train이 만든 파일)가 설정 파일 옆에 있으면 그 위에 덧씌운다. 없으면 설정 파일 그대로다 */
    char trained[512];
    const char *slash = strrchr(path, '/');
    int dir_len = slash ? (int)(slash - path) + 1 : 0;
    if (snprintf(trained, sizeof trained, "%.*s%s", dir_len, path, BOSS_TRAINED_FILE) < (int)sizeof trained && access(trained, R_OK) == 0)
        boss_cfg_apply(c, trained);
}

/* 파일에 적힌 줄만 바꾼다. 적히지 않은 수치는 지금 값 그대로다 */
void boss_cfg_apply(boss_cfg_t *c, const char *path)
{
    cfg_load(path);
#define F(k) c->k = cfg_get(#k, c->k)
#define I(k) c->k = (int)cfg_get(#k, (float)c->k)
    c->fly_hp[0] = cfg_get("fly_hp_1", c->fly_hp[0]);      /* 참가자 수마다 따로 정한다 */
    c->fly_hp[1] = cfg_get("fly_hp_2", c->fly_hp[1]);
    c->fly_hp[2] = cfg_get("fly_hp_3", c->fly_hp[2]);
    I(ticks); F(fly_radius); F(player_radius); F(step); F(slow_step); I(step_ticks);
    F(body_mm); F(walk_mm_s); F(run_mm_s); F(back_mm_s); F(turn_deg_s); F(speed_scale); F(turn_scale);
    F(step_hz); F(cpg_couple); F(cpg_converge); F(swing_front); F(swing_mid); F(swing_hind);
    F(foot_lift); F(foot_fore); F(foot_side); F(body_spring_hz); F(body_damping); F(leg_grip); F(hit_push);
    F(takeoff_level); F(takeoff_mm_s); I(air_ticks); I(air_rest_ticks); F(takeoff_scale);
    F(trap_loom); F(trap_see); I(stun_struggle);
    F(air_turn_deg_s); F(air_turn_scale); F(land_level); F(surface_gain); I(air_min_ticks); I(air_land_ticks);
    F(surface_max); I(perch_rest_ticks);
    F(approach_speed); I(approach_ticks); I(perch_min_ticks); F(perch_walk);
    F(perch_height); F(air_height); F(pitch_climb_deg); F(pitch_cruise_deg); F(pitch_flare_deg); F(bank_deg);
    I(guard_ticks); I(guard_cooldown); I(guard_stun);
    I(jump_ticks); I(jump_rest); I(jump_buffer); I(jump_carry); F(jump_height); F(jump_clear);
    I(bait_count); I(bait_ticks); I(bait_wary); F(bait_throw); F(bait_smell); F(bait_sugar); F(bait_heal);
    F(see_range); F(see_player); F(see_bait); F(pursue_turn);
    F(feed_level);
    F(swat_range); F(swat_damage); I(swat_cooldown);
    F(spray_range); F(spray_damage); I(spray_ammo); I(spray_cooldown);
    F(trap_speed); F(trap_range); I(trap_count); I(stun_ticks);
    F(charge_range); F(charge_speed); I(charge_windup); I(charge_ticks); I(charge_cooldown);
    F(wave_radius); F(wave_push); I(wave_period);
    F(loom_range); F(wall_sense); F(rush_level); F(wall_turn); F(near_range); F(near_gain); I(hurt_ticks);
    F(phase2_hp); I(phase2_ticks);
    F(loom_base); F(loom_gain); F(loom_speed); I(loom_avg_ticks);
#undef F
#undef I
}

/* 몸의 수치. 명령이 1일 때 딛고 있는 발이 몸 뒤로 가는 빠르기가 걷는 빠르기(walk_mm_s를 칸으로 옮기고 게임용
 * 배율을 곱한 값)가 되게 다리마다 보폭을 정한다. 그래서 걷는 빠르기는 전과 같고, 그것이 걸음에서 나온다 */
void boss_body_cfg(const boss_cfg_t *c, body_cfg_t *o)
{
    float v1 = c->walk_mm_s * 2.0f * c->fly_radius / c->body_mm * c->speed_scale;     /* 칸/초 */
    float wn = 2.0f * PI_F * c->body_spring_hz;
    memset(o, 0, sizeof *o);
    o->step_hz = c->step_hz > 0.1f ? c->step_hz : 0.1f;
    o->couple = c->cpg_couple;
    o->converge = c->cpg_converge;
    o->swing_end[0] = c->swing_front; o->swing_end[1] = c->swing_mid; o->swing_end[2] = c->swing_hind;
    for (int k = 0; k < 3; k++) o->sweep[k] = v1 * (1.0f - o->swing_end[k] / (2.0f * PI_F)) / o->step_hz;
    o->lift = c->foot_lift;
    o->foot_x[0] = c->foot_fore; o->foot_x[1] = 0.0f; o->foot_x[2] = -c->foot_fore;
    o->foot_y = c->foot_side;
    o->mass = 1.0f;                                  /* 힘의 수치는 모두 이 질량에 대한 값이다 */
    o->inertia = o->mass * (c->fly_radius * c->fly_radius + 0.16f) / 4.0f;            /* 긴 반지름 fly_radius, 짧은 반지름 0.4인 타원 */
    o->stiff = o->mass * wn * wn / 3.0f;             /* 세 다리가 딛고 있을 때 몸이 body_spring_hz로 제자리를 찾는다 */
    o->damp = 2.0f * c->body_damping * o->mass * wn / 3.0f;
    o->grip = c->leg_grip;
}

/* ---- 맵과 기하 ------------------------------------------------------------------- */
/* 바깥 벽과 2x2 기둥 넷. 벽과 기둥 사이, 기둥과 기둥 사이의 통로는 모두 4칸이다 */
static void build_map(boss_state_t *s)
{
    static const int pillars[][2] = { { 5, 5 }, { 11, 5 }, { 5, 11 }, { 11, 11 } };
    memset(s->wall, 0, sizeof s->wall);
    for (int i = 0; i < BOSS_MAP; i++)
        s->wall[0][i] = s->wall[BOSS_MAP - 1][i] = s->wall[i][0] = s->wall[i][BOSS_MAP - 1] = 1;
    for (size_t k = 0; k < sizeof pillars / sizeof pillars[0]; k++)
        for (int dy = 0; dy < 2; dy++)
            for (int dx = 0; dx < 2; dx++) s->wall[pillars[k][1] + dy][pillars[k][0] + dx] = 1;
}

int boss_solid(const boss_state_t *s, int cx, int cy)
{
    if (cx < 0 || cy < 0 || cx >= BOSS_MAP || cy >= BOSS_MAP) return 1;
    return s->wall[cy][cx];
}

static int solid_at(const boss_state_t *s, float x, float y) { return boss_solid(s, (int)floorf(x), (int)floorf(y)); }

/* 반지름 r인 원이 벽과 겹치는가(원을 감싸는 네모로 본다) */
static int blocked(const boss_state_t *s, float x, float y, float r)
{
    for (int cy = (int)floorf(y - r); cy <= (int)floorf(y + r); cy++)
        for (int cx = (int)floorf(x - r); cx <= (int)floorf(x + r); cx++)
            if (boss_solid(s, cx, cy)) return 1;
    return 0;
}

/* x, y 순서로 따로 움직여 벽을 타고 미끄러진다 */
static void slide(const boss_state_t *s, float *x, float *y, float dx, float dy, float r)
{
    if (!blocked(s, *x + dx, *y, r)) *x += dx;
    if (!blocked(s, *x, *y + dy, r)) *y += dy;
}

static int line_clear(const boss_state_t *s, float x0, float y0, float x1, float y1)
{
    float dx = x1 - x0, dy = y1 - y0, len = sqrtf(dx * dx + dy * dy);
    int n = (int)(len / 0.1f) + 1;
    for (int i = 1; i < n; i++) {
        float t = (float)i / (float)n;
        if (solid_at(s, x0 + dx * t, y0 + dy * t)) return 0;
    }
    return 1;
}

/* 각도를 0 이상 2파이 미만으로 */
static float wrap_angle(float a)
{
    a = fmodf(a, 2.0f * PI_F);
    return a < 0.0f ? a + 2.0f * PI_F : a;
}
static float dist(float ax, float ay, float bx, float by) { return sqrtf((ax - bx) * (ax - bx) + (ay - by) * (ay - by)); }
static float clamp01(float v) { return v < 0.0f ? 0.0f : (v > 1.0f ? 1.0f : v); }

static void event(boss_state_t *s, int kind, int who, float x, float y)
{
    if (s->n_ev >= BOSS_EVENTS) return;
    s->ev[s->n_ev++] = (boss_event_t){ (uint8_t)kind, (uint8_t)who,
                                       (int16_t)(x * BOSS_FIX), (int16_t)(y * BOSS_FIX) };
}

/* ---- 시작과 입력 ----------------------------------------------------------------- */
void boss_init(boss_state_t *s, const boss_cfg_t *c, uint32_t seed, int n_players, const int lives[])
{
    static const float start_x[BOSS_PLAYERS] = { 7.5f, 9.0f, 10.5f };
    memset(s, 0, sizeof *s);
    s->cfg = *c;
    stage_rng_seed(&s->rng, seed);
    build_map(s);
    if (n_players < 1) n_players = 1;
    if (n_players > BOSS_PLAYERS) n_players = BOSS_PLAYERS;
    s->n_players = n_players;
    for (int i = 0; i < n_players; i++) {
        boss_player_t *p = &s->p[i];
        p->x = start_x[i];
        p->y = 15.5f;
        p->ang = 1.5f * PI_F;                        /* 위쪽(-y)을 본다 */
        p->hp = lives ? lives[i] : 3;
        if (p->hp < 1) p->hp = 1;
        p->alive = 1;
        p->item = BOSS_SWATTER;
        p->ammo = c->spray_ammo;
        p->traps = c->trap_count;
        p->baits = c->bait_count;
    }
    s->fly.x = 9.0f;
    s->fly.y = 3.5f;
    s->fly.ang = PI_F / 2.0f;                        /* 아래쪽(참가자 쪽)을 본다 */
    s->fly.hp = s->fly.hp_max = (int)(c->fly_hp[n_players - 1] + 0.5f);
    if (s->fly.hp_max < 1) s->fly.hp = s->fly.hp_max = 1;
    s->fly.wave_in = c->wave_period;
    s->fly.target = -1;
    boss_body_cfg(c, &s->bcfg);
    s->walk_v1 = c->walk_mm_s * 2.0f * c->fly_radius / c->body_mm * c->speed_scale;
    /* 도는 명령이 1일 때 turn_deg_s로 돌게 한다: 좌우 발 사이가 2 x foot_side이므로 빠르기의 차이는 그 곱이다 */
    s->turn_gain = c->turn_deg_s * (PI_F / 180.0f) * c->turn_scale * c->foot_side / (s->walk_v1 > 1e-4f ? s->walk_v1 : 1e-4f);
    body_init(&s->fly.body, &s->bcfg, s->fly.x, s->fly.y, s->fly.ang);
}

/* 올라갈 때는 빨리 솟아 꼭대기에서 느려지고, 내려올 때는 올라갈 때보다 빨리 떨어진다(오르는 데 55%, 내리는 데 45%).
 * 좌우가 같은 포물선은 붕 떠 있는 것처럼 느리게 느껴진다 */
float boss_jump_curve(float t)
{
    const float up = 0.55f;
    if (t <= 0.0f || t >= 1.0f) return 0.0f;
    float u = t < up ? (up - t) / up : (t - up) / (1.0f - up);
    return 1.0f - u * u;
}

void boss_input(boss_state_t *s, int player, int key)
{
    if (player < 0 || player >= s->n_players || s->outcome != BOSS_GOING) return;
    boss_player_t *p = &s->p[player];
    if (!p->alive) return;                           /* 쓰러진 사람의 키는 쓰지 않는다 */
    if (key == BOSS_SNEAK) { p->sneak = !p->sneak; return; }
    if (key == BOSS_GUARD) {                         /* 막기: 쉬는 중이 아니면 막는 자세를 시작한다 */
        if (p->guard_cool == 0) { p->guard = s->cfg.guard_ticks; p->guard_cool = s->cfg.guard_cooldown; }
        return;
    }
    if (key == BOSS_JUMP) {      /* 뛰어오르기: 땅에 서 있으면 바로 뛴다. 아직 떠 있으면 눌렀다는 것을 잠깐 기억해 둔다 */
        if (s->cfg.jump_ticks <= 0) return;
        if (p->jump == 0 && p->jump_rest == 0) {
            p->jump = s->cfg.jump_ticks;
            p->carry = p->step_left > 0 || p->moved_ago <= s->cfg.jump_carry;       /* 걷고 있었으면 그 기세로 */
        } else {
            p->jump_buf = s->cfg.jump_buffer;
        }
        return;
    }
    int slow = (key & BOSS_SLOW) != 0, strafe = (key & BOSS_STRAFE) != 0;
    switch (key & ~(BOSS_SLOW | BOSS_STRAFE)) {
    case SK_UP:    p->move = 1;  p->slow = slow; break;
    case SK_DOWN:  p->move = -1; p->slow = slow; break;
    case SK_LEFT:
        if (strafe) { p->strafe = -1; p->slow = slow; }
        else if (p->turn > -2) p->turn--;
        break;
    case SK_RIGHT:
        if (strafe) { p->strafe = 1; p->slow = slow; }
        else if (p->turn < 2) p->turn++;
        break;
    case SK_1:     p->pick = 1 + BOSS_SWATTER; break;
    case SK_2:     p->pick = 1 + BOSS_SPRAY; break;
    case SK_3:     p->pick = 1 + BOSS_TRAP; break;
    case SK_4:     p->pick = 1 + BOSS_BAIT; break;
    case SK_SUB:   p->pick = 1 + (p->item + 1) % BOSS_ITEMS; break;
    case SK_ACT:   p->use = 1; break;
    default: break;
    }
}

void boss_look(boss_state_t *s, int player, float ang)
{
    if (player < 0 || player >= s->n_players || s->outcome != BOSS_GOING) return;
    boss_player_t *p = &s->p[player];
    if (!p->alive || !isfinite(ang)) return;
    p->look = wrap_angle(ang);
    p->has_look = 1;
}

/* ---- 자극 ------------------------------------------------------------------------- */
/* 초파리 기준의 왼쪽/오른쪽 몫. side는 방향의 sin(-1이 왼쪽 옆, 1이 오른쪽 옆, 0이 앞이나 뒤) */
static void add_side(float *l, float *r, float level, float side)
{
    float wl = clamp01(0.5f - side), wr = clamp01(0.5f + side);
    if (level * wl > *l) *l = level * wl;
    if (level * wr > *r) *r = level * wr;
}

/* 초파리(머리 방향 hx, hy)가 볼 때 dx, dy에 있는 것이 어느 쪽인가. -1이 왼쪽 옆, 1이 오른쪽 옆, 0이 정면이다.
 * 뒤에 있는 것은 가까운 쪽 옆으로 본다. 정면처럼 양쪽으로 나누면 뇌가 뒤로 가라고 답해서 위협 쪽으로 물러나게 된다 */
static float side_of(float hx, float hy, float dx, float dy, float d)
{
    float side = (hx * dy - hy * dx) / d;
    if (hx * dx + hy * dy < 0.0f) side = side >= 0.0f ? 1.0f : -1.0f;
    return side;
}

/* 벽까지의 거리(앞쪽으로 ang 방향, 최대 range) */
static float wall_dist(const boss_state_t *s, float x, float y, float ang, float range)
{
    float cx = cosf(ang), cy = sinf(ang);
    for (float d = 0.1f; d <= range; d += 0.1f)
        if (solid_at(s, x + cx * d, y + cy * d)) return d;
    return range + 1.0f;
}

/* 참가자 i가 초파리에게 주는 다가옴 자극(0~1). 가까울수록, 빨리 다가올수록 세다.
 * 아주 가까우면(near_range 안) 서 있기만 해도 세다 */
static float threat(const boss_state_t *s, int i)
{
    const boss_cfg_t *c = &s->cfg;
    const boss_player_t *p = &s->p[i];
    float d = dist(p->x, p->y, s->fly.x, s->fly.y), gap = d - c->fly_radius - c->player_radius;
    if (!p->alive || gap >= c->loom_range) return 0.0f;
    if (gap < 0.0f) gap = 0.0f;
    float approach = clamp01(p->approach / c->loom_speed);       /* 최근에 다가온 평균 빠르기 */
    float level = (1.0f - gap / c->loom_range) * (c->loom_base + c->loom_gain * approach);
    if (gap < c->near_range) level += c->near_gain * (1.0f - gap / c->near_range);
    return clamp01(level);
}

/* 떠서 날아가는 빠르기(칸/초). 도약의 빠르기에는 전체 배율(speed_scale)이 아니라 따로 둔 배율을 곱한다. 전체 배율을
 * 그대로 쓰면 0.2초에 10칸을 날아가서 순간이동처럼 보였다 */
static float air_speed(const boss_cfg_t *c)
{
    return c->takeoff_mm_s * 2.0f * c->fly_radius / c->body_mm * c->takeoff_scale;
}

void boss_stimulus(const boss_state_t *s, float stim[BRAIN_GROUP_COUNT])
{
    const boss_fly_t *f = &s->fly;
    float hx = cosf(f->ang), hy = sinf(f->ang), l = 0.0f, r = 0.0f;
    memset(stim, 0, sizeof(float) * BRAIN_GROUP_COUNT);
    if (s->outcome != BOSS_GOING) return;

    for (int i = 0; i < s->n_players; i++) {
        const boss_player_t *p = &s->p[i];
        if (!p->alive) continue;
        float dx = p->x - f->x, dy = p->y - f->y, d = sqrtf(dx * dx + dy * dy);
        if (d < 1e-4f) continue;
        add_side(&l, &r, threat(s, i), side_of(hx, hy, dx, dy, d));
    }
    if (f->hurt > 0) {                               /* 방금 맞았다. 때린 쪽이 가장 센 자극이다 */
        float dx = f->hurt_x - f->x, dy = f->hurt_y - f->y, d = sqrtf(dx * dx + dy * dy);
        if (d > 1e-4f) add_side(&l, &r, 1.0f, side_of(hx, hy, dx, dy, d));
    }
    /* 날아오는 끈끈이. 빠르게 다가오는 물체라서 사람이 다가올 때와 같은 다가옴 자극이 된다. 가까울수록 세다.
     * 초파리를 비껴 갈 것과 멀어지는 것은 눈에서 커지지 않으므로 자극이 아니다. 뇌가 반응하는 데 두세 틱이 걸려서
     * 멀리서 던진 것은 보고 뛰어오르고, 바로 앞에서 던진 것은 못 피한다 */
    for (int k = 0; k < BOSS_PROJ; k++) {
        const boss_proj_t *t = &s->proj[k];
        float dx = t->x - f->x, dy = t->y - f->y, d = sqrtf(dx * dx + dy * dy), v = sqrtf(t->vx * t->vx + t->vy * t->vy);
        if (!t->on || d < 1e-4f || v < 1e-4f || d - s->cfg.fly_radius >= s->cfg.trap_see) continue;
        if (t->vx * dx + t->vy * dy >= 0.0f) continue;                       /* 멀어지고 있다 */
        if (fabsf(dx * t->vy - dy * t->vx) / v > s->cfg.fly_radius + 0.3f) continue;   /* 비껴 간다 */
        float gap = d - s->cfg.fly_radius;
        add_side(&l, &r, s->cfg.trap_loom * (1.0f - (gap < 0.0f ? 0.0f : gap) / s->cfg.trap_see), side_of(hx, hy, dx, dy, d));
    }
    stim[BR_LOOM_L] = l;
    stim[BR_LOOM_R] = r;
    /* 먹이에 닿아 있으면 단맛. 진짜 뇌에서는 단맛을 느끼는 뉴런들이고, 이것이 주둥이를 내미는 운동 뉴런(먹기)을 켠다 */
    for (int k = 0; k < BOSS_BAITS; k++)
        if (s->bait[k].on && dist(s->bait[k].x, s->bait[k].y, f->x, f->y) <= s->cfg.fly_radius + BAIT_TOUCH)
            stim[BR_SUGAR] = s->cfg.bait_sugar;
    /* 보이는 물체: 참가자와 먹이. 진짜 뇌에서는 물체를 보는 시각 뉴런(LC9)이고, 같은 쪽의 걷기 명령 뉴런을 켠다.
     * 가까울수록 크게 보여서 세다. 벽에 가리면 보이지 않는다. 놀란 동안(wary)에는 먹이를 보지 않는다 */
    float ol = 0.0f, orr = 0.0f;
    for (int i = 0; i < s->n_players; i++) {
        const boss_player_t *p = &s->p[i];
        float dx = p->x - f->x, dy = p->y - f->y, d = sqrtf(dx * dx + dy * dy);
        if (!p->alive || d < 1e-4f || d >= s->cfg.see_range || !line_clear(s, f->x, f->y, p->x, p->y)) continue;
        add_side(&ol, &orr, s->cfg.see_player * (0.4f + 0.6f * (1.0f - d / s->cfg.see_range)), side_of(hx, hy, dx, dy, d));
    }
    for (int k = 0; k < BOSS_BAITS && f->wary == 0; k++) {
        const boss_bait_t *b = &s->bait[k];
        float dx = b->x - f->x, dy = b->y - f->y, d = sqrtf(dx * dx + dy * dy);
        if (!b->on || d < 1e-4f || d >= s->cfg.bait_smell || !line_clear(s, f->x, f->y, b->x, b->y)) continue;
        add_side(&ol, &orr, s->cfg.see_bait * (0.4f + 0.6f * (1.0f - d / s->cfg.bait_smell)), side_of(hx, hy, dx, dy, d));
    }
    stim[BR_OBJECT_L] = ol;
    stim[BR_OBJECT_R] = orr;
    /* 떠 있는 동안: 날아가는 쪽의 벽. 눈에는 다가오는 면으로 보인다. 세기는 빠르기 / 거리, 곧 닿을 때까지 남은
     * 시간의 역수에 비례한다. 걸을 때는 느려서 이 값이 0에 가깝고, 실제 착지 뉴런도 날 때만 반응하므로(Ache 외 2019)
     * 떠 있을 때만 준다. 왼쪽 앞과 오른쪽 앞을 따로 본다 */
    if (f->air > 0) {
        float a = atan2f(f->air_dy, f->air_dx), v = air_speed(&s->cfg) * (f->land_cmd ? s->cfg.approach_speed : 1.0f);
        float dl = wall_dist(s, f->x, f->y, a - SURFACE_ANGLE, 8.0f) - s->cfg.fly_radius;
        float dr = wall_dist(s, f->x, f->y, a + SURFACE_ANGLE, 8.0f) - s->cfg.fly_radius;
        /* surface_max에서 자른다. 착지 뉴런은 그 세기에서 이미 충분히 발화하고, 벽 바로 앞에서 더 세게 주면 뇌 전체의
         * 발화가 늘어 폭주할 수 있다 */
        float sl = s->cfg.surface_gain * v / (dl < 0.1f ? 0.1f : dl), sr = s->cfg.surface_gain * v / (dr < 0.1f ? 0.1f : dr);
        stim[BR_SURFACE_L] = sl > s->cfg.surface_max ? s->cfg.surface_max : sl;
        stim[BR_SURFACE_R] = sr > s->cfg.surface_max ? s->cfg.surface_max : sr;
    }
}

/* ---- 참가자 ------------------------------------------------------------------------- */
static void hurt_fly(boss_state_t *s, int who, int dmg)
{
    if (dmg > s->fly.hp) dmg = s->fly.hp;
    if (s->fly.lured) s->bait[s->fly.bait].on = 0;                   /* 놀라서 가던 먹이를 버린다 */
    for (int k = 0; k < BOSS_BAITS; k++)                             /* 닿아 있던 먹이도 버린다 */
        if (s->bait[k].on && dist(s->bait[k].x, s->bait[k].y, s->fly.x, s->fly.y) <= s->cfg.fly_radius + BAIT_TOUCH)
            s->bait[k].on = 0;
    s->fly.wary = s->cfg.bait_wary;
    s->fly.hp -= dmg;
    s->dmg_dealt[who] += dmg;
    s->fly.hurt = s->cfg.hurt_ticks;
    s->fly.hurt_x = s->p[who].x;
    s->fly.hurt_y = s->p[who].y;
    /* 맞으면 때린 쪽의 반대로 밀려난다. 다리가 버티면 제자리로 돌아오고, 못 버티면 발이 미끄러진다 */
    float dx = s->fly.x - s->p[who].x, dy = s->fly.y - s->p[who].y, d = sqrtf(dx * dx + dy * dy);
    if (d > 1e-4f) body_push(&s->fly.body, dx / d * s->cfg.hit_push * (float)dmg / 10.0f, dy / d * s->cfg.hit_push * (float)dmg / 10.0f);
}

static void use_item(boss_state_t *s, int i)
{
    const boss_cfg_t *c = &s->cfg;
    boss_player_t *p = &s->p[i];
    boss_fly_t *f = &s->fly;
    float ux = cosf(p->ang), uy = sinf(p->ang);
    if (p->cool > 0) return;

    if (p->item == BOSS_SWATTER) {
        float dx = f->x - p->x, dy = f->y - p->y, d = sqrtf(dx * dx + dy * dy);
        /* 달아나느라 솟아오른 동안(air)은 파리채가 닿지 않는다. 스프레이와 끈끈이는 닿는다 */
        int hit = f->air == 0 && d <= c->fly_radius + c->player_radius + c->swat_range && d > 1e-4f &&
                  (ux * dx + uy * dy) / d >= 0.82f && line_clear(s, p->x, p->y, f->x, f->y);
        if (hit) hurt_fly(s, i, (int)c->swat_damage);
        event(s, hit ? BOSS_EV_SWAT_HIT : BOSS_EV_SWAT_MISS, i, p->x + ux, p->y + uy);
        p->cool = c->swat_cooldown;
    } else if (p->item == BOSS_SPRAY) {
        if (p->ammo <= 0) return;
        p->ammo--;
        p->cool = c->spray_cooldown;
        float ex = p->x, ey = p->y;
        int hit = 0;
        for (float d = 0.1f; d <= c->spray_range; d += 0.05f) {
            ex = p->x + ux * d;
            ey = p->y + uy * d;
            if (solid_at(s, ex, ey)) break;
            if (dist(ex, ey, f->x, f->y) <= c->fly_radius) { hit = 1; break; }
        }
        if (hit) hurt_fly(s, i, (int)c->spray_damage);
        event(s, hit ? BOSS_EV_SPRAY_HIT : BOSS_EV_SPRAY_MISS, i, ex, ey);
    } else if (p->item == BOSS_TRAP) {
        if (p->traps <= 0) return;
        for (int k = 0; k < BOSS_PROJ; k++) {
            if (s->proj[k].on) continue;
            p->traps--;
            p->cool = c->swat_cooldown;
            s->proj[k] = (boss_proj_t){ 1, i, p->x + ux * 0.4f, p->y + uy * 0.4f,
                                        ux * c->trap_speed, uy * c->trap_speed, c->trap_range };
            event(s, BOSS_EV_TRAP_THROW, i, p->x, p->y);
            break;
        }
    } else {                                         /* 먹이: 보는 쪽으로 bait_throw만큼 앞, 벽에 막히면 그 앞에 놓는다 */
        if (p->baits <= 0) return;
        for (int k = 0; k < BOSS_BAITS; k++) {
            if (s->bait[k].on) continue;
            float bx = p->x, by = p->y;
            for (float d = 0.2f; d <= c->bait_throw && !blocked(s, p->x + ux * d, p->y + uy * d, 0.2f); d += 0.1f) {
                bx = p->x + ux * d;
                by = p->y + uy * d;
            }
            p->baits--;
            p->cool = c->swat_cooldown;
            s->bait[k] = (boss_bait_t){ 1, bx, by, c->bait_ticks };
            event(s, BOSS_EV_BAIT, i, bx, by);
            break;
        }
    }
}

static void players_act(boss_state_t *s)
{
    const boss_cfg_t *c = &s->cfg;
    boss_fly_t *f = &s->fly;
    for (int i = 0; i < s->n_players; i++) {
        boss_player_t *p = &s->p[i];
        if (p->cool > 0) p->cool--;
        if (p->guard > 0) p->guard--;
        if (p->guard_cool > 0) p->guard_cool--;
        if (p->jump > 0) {                               /* 떠 있다 */
            p->jump--;
            p->z = p->jump > 0 ? c->jump_height * boss_jump_curve(1.0f - (float)p->jump / (float)c->jump_ticks) : 0.0f;
            if (p->jump == 0) { p->jump_rest = c->jump_rest; p->carry = 0; }
        } else if (p->jump_rest > 0) {
            p->jump_rest--;
        }
        if (p->jump_buf > 0) {                           /* 기억해 둔 키: 뛸 수 있게 되면(내려서자마자) 뛴다 */
            p->jump_buf--;
            if (p->jump == 0 && p->jump_rest == 0 && p->alive) {
                p->jump = c->jump_ticks;
                p->jump_buf = 0;
                p->carry = p->moved_ago <= c->jump_carry;        /* 날아와서 바로 다시 뛰면 계속 나아간다 */
            }
        }
        p->closing = 0.0f;
        if (!p->alive) { p->move = p->strafe = p->turn = p->use = p->pick = p->has_look = p->step_left = 0; continue; }
        if (p->has_look) p->ang = p->look;               /* 마우스로 정한 방향 */
        if (p->turn) p->ang = wrap_angle(p->ang + (float)p->turn * (2.0f * PI_F / BOSS_DIRS));
        /* 걷는 중에 온 키는 버린다(누르고 있으면 또 온다). 떠 있을 때 온 키는 바로 받아서 날아가는 쪽을 바꾼다 */
        if ((p->move || p->strafe) && (p->step_left == 0 || p->jump > 0)) {
            /* 새 걸음. 앞은 (cos, sin), 오른쪽은 거기서 90도 돈 (-sin, cos)이다. 앞과 옆이 같이 왔으면 비스듬히 간다.
             * 방향은 걸음을 뗄 때 정하고, 한 걸음의 거리를 step_ticks에 고르게 나눠 간다 */
            float fx = cosf(p->ang), fy = sinf(p->ang);
            float mx = fx * (float)p->move - fy * (float)p->strafe, my = fy * (float)p->move + fx * (float)p->strafe;
            float n = sqrtf(mx * mx + my * my), len = (p->slow || p->sneak) ? c->slow_step : c->step;
            int ticks = c->step_ticks > 1 ? c->step_ticks : 1;
            p->step_left = ticks;
            p->step_dx = mx / n * len / (float)ticks;
            p->step_dy = my / n * len / (float)ticks;
            if (p->jump > 0) p->carry = 1;
        }
        /* 걷다가 뛰어올랐으면 걷던 빠르기와 방향 그대로 날아간다: 내려설 때까지 틱마다 같은 만큼 간다.
         * 터미널은 키를 누르고 있는지를 알려 주지 않고, 다른 키(스페이스)를 누르면 걷는 키의 되풀이가 끊긴다.
         * 그래서 이렇게 하지 않으면 걷다가 뛰는 순간 제자리에 멈춰서 위아래로만 뛴다 */
        if (p->jump > 0 && p->carry && p->step_left == 0) p->step_left = 1;
        if (p->step_left == 0 && p->moved_ago < 1000) p->moved_ago++;
        if (p->step_left > 0) {
            p->moved_ago = 0;
            float nx = p->x, ny = p->y, ox = p->x, oy = p->y;
            p->step_left--;
            slide(s, &nx, &ny, p->step_dx, p->step_dy, c->player_radius);
            /* 초파리 몸을 뚫고 지나가지 않는다 */
            if (dist(nx, ny, f->x, f->y) >= c->fly_radius + c->player_radius ||
                dist(nx, ny, f->x, f->y) >= dist(p->x, p->y, f->x, f->y)) {
                p->x = nx;
                p->y = ny;
            }
            /* 초파리 쪽으로 움직인 만큼만 다가온 것으로 센다(초파리가 움직여 가까워진 것은 세지 않는다) */
            float d = dist(ox, oy, f->x, f->y);
            if (d > 1e-4f) p->closing = ((p->x - ox) * (f->x - ox) + (p->y - oy) * (f->y - oy)) / d;
        }
        if (p->pick) p->item = p->pick - 1;
        if (p->use) use_item(s, i);
        p->move = p->strafe = p->turn = p->use = p->pick = p->has_look = 0;
    }
}

static void traps_fly(boss_state_t *s)
{
    boss_fly_t *f = &s->fly;
    for (int k = 0; k < BOSS_PROJ; k++) {
        boss_proj_t *t = &s->proj[k];
        if (!t->on) continue;
        t->x += t->vx;
        t->y += t->vy;
        t->left -= s->cfg.trap_speed;
        if (dist(t->x, t->y, f->x, f->y) <= s->cfg.fly_radius) {
            f->stun = s->cfg.stun_ticks;
            f->air = 0;                               /* 날다가 붙으면 그 자리에 떨어진다 */
            f->windup = f->charging = 0;              /* 붙으면 돌진도 멈춘다 */
            f->target = -1;
            event(s, BOSS_EV_STUCK, t->owner, f->x, f->y);
            t->on = 0;
        } else if (solid_at(s, t->x, t->y) || t->left <= 0.0f) {
            event(s, BOSS_EV_TRAP_MISS, t->owner, t->x, t->y);
            t->on = 0;
        }
    }
}

/* ---- 초파리 ------------------------------------------------------------------------- */
static void hurt_player(boss_state_t *s, int i, float from_x, float from_y)
{
    boss_player_t *p = &s->p[i];
    float dx = p->x - from_x, dy = p->y - from_y, d = sqrtf(dx * dx + dy * dy);
    p->hp--;
    s->dmg_taken[i]++;
    event(s, BOSS_EV_HIT_PLAYER, i, p->x, p->y);
    if (d > 1e-4f) for (int k = 0; k < 4; k++) slide(s, &p->x, &p->y, dx / d * 0.25f, dy / d * 0.25f, s->cfg.player_radius);
    if (p->hp <= 0) {
        p->hp = 0;
        p->alive = 0;
        event(s, BOSS_EV_DEATH, i, p->x, p->y);
    }
}

/* 냄새가 닿는 거리 안에 있고 보이는 먹이 가운데 가장 가까운 것. 없으면 -1 */
static int nearest_bait(const boss_state_t *s)
{
    int best = -1;
    float best_d = s->cfg.bait_smell;
    for (int k = 0; k < BOSS_BAITS; k++) {
        const boss_bait_t *b = &s->bait[k];
        if (!b->on) continue;
        float d = dist(b->x, b->y, s->fly.x, s->fly.y);
        if (d < best_d && line_clear(s, s->fly.x, s->fly.y, b->x, b->y)) { best = k; best_d = d; }
    }
    return best;
}

/* 몰리지 않았을 때 가까이 있고 보이는 참가자를 고른다. 없으면 -1 */
static int charge_target(const boss_state_t *s, float rushed)
{
    const boss_cfg_t *c = &s->cfg;
    int best = -1;
    float best_d = c->charge_range + 1.0f;
    if (rushed >= c->rush_level) return -1;
    for (int i = 0; i < s->n_players; i++) {
        const boss_player_t *p = &s->p[i];
        if (!p->alive) continue;
        float d = dist(p->x, p->y, s->fly.x, s->fly.y);
        if (d <= c->charge_range && d < best_d && line_clear(s, s->fly.x, s->fly.y, p->x, p->y)) {
            best = i;
            best_d = d;
        }
    }
    return best;
}

/* 날아가는 쪽 바로 앞이 벽인가. 맞으면 그 벽에 몸을 붙이고, 벽 쪽 방향을 *face에 넣는다. 비스듬히 닿았으면
 * 더 정면인 쪽 벽에 붙는다 */
static int wall_ahead(const boss_state_t *s, boss_fly_t *f, float *face)
{
    float r = s->cfg.fly_radius, reach = r + 0.25f, dx = f->air_dx, dy = f->air_dy;
    int hx = fabsf(dx) > 0.2f && solid_at(s, f->x + (dx > 0.0f ? reach : -reach), f->y);
    int hy = fabsf(dy) > 0.2f && solid_at(s, f->x, f->y + (dy > 0.0f ? reach : -reach));
    if (hx && (!hy || fabsf(dx) >= fabsf(dy))) {
        f->x = dx > 0.0f ? floorf(f->x + reach) - r - 0.001f : floorf(f->x - reach) + 1.0f + r + 0.001f;
        *face = dx > 0.0f ? 0.0f : PI_F;
        return 1;
    }
    if (hy) {
        f->y = dy > 0.0f ? floorf(f->y + reach) - r - 0.001f : floorf(f->y - reach) + 1.0f + r + 0.001f;
        *face = dy > 0.0f ? PI_F / 2.0f : -PI_F / 2.0f;
        return 1;
    }
    return 0;
}

/* 몸의 자세(높이, 머리를 든 각도, 옆으로 기울인 각도)를 지금 하는 일에 맞춰 조금씩 옮긴다. 화면에 보이는 것이고
 * 맞고 안 맞고에는 쓰지 않는다.
 *   머리를 드는 각도: 초파리는 몸의 기울기로 나는 빠르기를 조절한다. 느릴 때는 몸을 세우고 빠를 때는 눕힌다.
 *     그래서 솟을 때와 내려앉으려 속도를 줄일 때는 세우고, 날아갈 때는 눕힌다. 벽에 붙으면 머리가 위다
 *   옆으로 기울이기: 날면서 꺾을 때는 도는 쪽으로 몸을 기울여 돈다(Muijres 외 2015). 꺾기 명령이 셀수록 많이 기울인다 */
static void fly_attitude(boss_state_t *s)
{
    const boss_cfg_t *c = &s->cfg;
    boss_fly_t *f = &s->fly;
    const float deg = PI_F / 180.0f;
    float z = 0.0f, pitch = 0.0f, roll = 0.0f;
    if (f->hp <= 0) f->perch = 0;                                 /* 쓰러지면 벽에서 떨어진다 */
    if (f->perch) {
        z = c->perch_height;                                        /* 붙은 뒤 그 높이로 옮겨 앉는다 */
        pitch = PI_F / 2.0f;
    } else if (f->air > 0) {
        int down = f->air <= c->air_land_ticks;                     /* 바닥으로 내려오는 중 */
        z = down ? c->air_height * (float)(f->air - 1) / (float)(c->air_land_ticks > 0 ? c->air_land_ticks : 1) : c->air_height;
        pitch = (f->land_cmd || down ? c->pitch_flare_deg : (f->air_age < 3 ? c->pitch_climb_deg : c->pitch_cruise_deg)) * deg;
        float full = c->air_turn_deg_s * deg * c->air_turn_scale / 20.0f, lean = full > 1e-6f ? f->w / full : 0.0f;
        roll = (lean < -1.0f ? -1.0f : (lean > 1.0f ? 1.0f : lean)) * c->bank_deg * deg;
    } else if (f->charging > 0) {
        pitch = -0.2f;                                              /* 머리를 숙이고 달려든다 */
    }
    f->z += (z - f->z) * 0.5f;
    f->pitch += (pitch - f->pitch) * 0.45f;
    f->roll += (roll - f->roll) * 0.5f;
    if (f->z < 0.005f) f->z = 0.0f;
}

static void fly_act(boss_state_t *s, const float rate[BRAIN_GROUP_COUNT])
{
    const boss_cfg_t *c = &s->cfg;
    boss_fly_t *f = &s->fly;
    int was_feeding = f->feeding;
    f->feeding = f->lured = 0;
    if (f->wary > 0) f->wary--;
    if (f->charge_cool > 0) f->charge_cool--;
    if (f->hurt > 0) f->hurt--;
    /* 붙어 있는 동안은 움직이지도 공격하지도 않는다. 그동안에도 뇌는 반응한다: 달아남 출력(거대 섬유)이 발화 기준을
     * 넘는 틱에는 뛰어오르려고 몸부림쳐서 끈끈이가 그만큼 빨리 떨어진다. 그래서 붙여 놓고 에워싸서 때리면 빨리 풀려나고,
     * 멀리 떨어져 있으면 오래 붙어 있다 */
    if (f->stun > 0) {
        f->stun--;
        if (rate[BR_ESCAPE] >= c->takeoff_level) f->stun = f->stun > c->stun_struggle ? f->stun - c->stun_struggle : 0;
        return;
    }

    /* 떠 있는 동안: 날아가는 방향으로 간다. 그 방향은 뇌의 꺾기 명령(DNp06)의 좌우 차이만큼 돈다. 몸도 같이 돈다.
     * 빨라서 벽을 건너뛰지 않게 잘게 나눠 움직인다 */
    if (f->air > 0) {
        float turn = (rate[BR_AIRTURN_R] - rate[BR_AIRTURN_L]) * c->air_turn_deg_s * PI_F / 180.0f * c->air_turn_scale / 20.0f;
        if (turn != 0.0f) {
            float cs = cosf(turn), sn = sinf(turn), dx = f->air_dx, dy = f->air_dy;
            f->air_dx = dx * cs - dy * sn;
            f->air_dy = dx * sn + dy * cs;
            f->ang = wrap_angle(f->ang + turn);
        }
        f->w = turn;
        float len = air_speed(c) / 20.0f * (f->land_cmd ? c->approach_speed : 1.0f);
        int n = (int)(len / 0.2f) + 1;
        for (int k = 0; k < n; k++)
            slide(s, &f->x, &f->y, f->air_dx * len / (float)n, f->air_dy * len / (float)n, c->fly_radius);
        f->air_age++;
        /* 착지: 뇌의 착지 명령(DNp07)이 기준을 넘으면 다리를 뻗고 속도를 줄여, 다가오던 그 면(벽)으로 간다. 닿으면
         * 벽에 붙는다. approach_ticks 안에 닿지 못하면(날면서 꺾어 벽에서 멀어진 경우) 바닥에 내려앉는다.
         * 명령이 없어도 air_ticks가 지나면 바닥에 내려앉는다(게임 규칙). 남은 틱의 마지막 air_land_ticks가 내려오는 동안이다 */
        if (!f->land_cmd && f->air > c->air_land_ticks + 1 && f->air_age >= c->air_min_ticks && rate[BR_LAND] >= c->land_level) {
            f->land_cmd = 1;
            f->air = c->approach_ticks + 1;
            event(s, BOSS_EV_DESCEND, 255, f->x, f->y);
        }
        float face;
        if (f->land_cmd && wall_ahead(s, f, &face)) {
            f->ang = face;
            f->perch = 1;
            f->perch_age = 0;
            f->air = 1;
        }
        if (--f->air == 0) {
            f->air_rest = f->perch ? c->perch_rest_ticks : c->air_rest_ticks;   /* 벽에서는 곧 다시 날 수 있다 */
            f->land_cmd = 0;
            f->v = f->w = 0.0f;
            event(s, BOSS_EV_LAND, 255, f->x, f->y);
        }
        return;
    }
    if (f->air_rest > 0) f->air_rest--;
    /* 도약: 뇌의 달아남 출력(거대 섬유)이 발화 기준을 넘었다. 하던 일(돌진 포함)을 끊고 뛰어오른다.
     * 방향은 몸 기준으로 앞뒤 = 1 - 2 x 후진, 좌우 = 오른쪽 회전 - 왼쪽 회전 */
    if (f->air_rest == 0 && c->air_ticks > 0 && rate[BR_ESCAPE] >= c->takeoff_level) {
        float a = f->ang + atan2f(rate[BR_TURN_R] - rate[BR_TURN_L], 1.0f - 2.0f * rate[BR_BACKWARD]);
        if (f->perch) {
            /* 벽에 붙어 있었다. 머리가 벽 쪽이라 뇌가 말한 방향을 벽면에 비춰서(벽으로 들어가는 몫만 뒤집어서) 뛴다:
             * 옆으로 피하는 몫은 그대로이고, 벽을 차고 방 쪽으로 나온다. 몸도 그쪽을 본다 */
            a = f->ang + PI_F - atan2f(rate[BR_TURN_R] - rate[BR_TURN_L], 1.0f);
            f->ang = wrap_angle(a);
            f->perch = 0;
        }
        f->air_dx = cosf(a);
        f->air_dy = sinf(a);
        f->air = c->air_ticks;
        f->air_age = 0;
        f->land_cmd = 0;
        if (f->windup > 0 || f->charging > 0) f->charge_cool = c->charge_cooldown;
        f->windup = f->charging = 0;
        f->target = -1;
        event(s, BOSS_EV_TAKEOFF, 255, f->x, f->y);
        return;
    }

    /* 벽에 붙어 있다. 걷지 않는다. 한동안 머문 뒤 뇌의 걷기 명령(보이는 사람이나 먹이)이 켜지면 바닥으로 내려와
     * 방 쪽을 본다. 그 전에 다가옴이 세면 위에서 이미 날아올랐다. 날갯짓과 돌진은 붙은 채로도 한다 */
    if (f->perch) {
        float walk = rate[BR_WALK_L] > rate[BR_WALK_R] ? rate[BR_WALK_L] : rate[BR_WALK_R];
        f->v = f->w = 0.0f;
        if (++f->perch_age >= c->perch_min_ticks && walk >= c->perch_walk) {
            f->perch = 0;
            f->ang = wrap_angle(f->ang + PI_F);
        }
    }

    /* 날갯짓: 가까운 참가자를 모두 밀어낸다 */
    if (--f->wave_in <= 0) {
        f->wave_in = c->wave_period;
        event(s, BOSS_EV_WAVE, 255, f->x, f->y);
        for (int i = 0; i < s->n_players; i++) {
            boss_player_t *p = &s->p[i];
            float dx = p->x - f->x, dy = p->y - f->y, d = sqrtf(dx * dx + dy * dy);
            if (!p->alive || d > c->wave_radius || d < 1e-4f) continue;
            for (int k = 0; k < 10; k++)
                slide(s, &p->x, &p->y, dx / d * c->wave_push / 10.0f, dy / d * c->wave_push / 10.0f, c->player_radius);
        }
    }

    /* 돌진: 멈춰서 겨누고(windup) 그때의 방향으로 곧게 달린다. 달리는 동안은 방향을 바꾸지 않으므로 피할 수 있다.
     * 길 위에서 처음 닿은 참가자가 맞는다 */
    if (f->windup > 0) {
        boss_player_t *p = &s->p[f->target];
        if (!p->alive) { f->windup = 0; f->target = -1; f->charge_cool = c->charge_cooldown; return; }
        f->ang = atan2f(p->y - f->y, p->x - f->x);
        if (--f->windup == 0) f->charging = c->charge_ticks;
        return;
    }
    if (f->charging > 0) {
        float ox = f->x, oy = f->y;
        slide(s, &f->x, &f->y, cosf(f->ang) * c->charge_speed, sinf(f->ang) * c->charge_speed, c->fly_radius);
        f->charging--;
        for (int i = 0; i < s->n_players; i++) {
            if (f->dodged & (1 << i)) continue;      /* 이 돌진을 이미 뛰어넘었다 */
            if (s->p[i].alive && dist(s->p[i].x, s->p[i].y, f->x, f->y) <= c->fly_radius + c->player_radius + 0.05f) {
                if (s->p[i].z >= c->jump_clear) {    /* 높이 떠 있다: 돌진이 발밑으로 지나간다. 초파리는 계속 달린다 */
                    f->dodged |= 1 << i;
                    event(s, BOSS_EV_DODGE, i, s->p[i].x, s->p[i].y);
                    continue;
                }
                if (s->p[i].guard > 0) {             /* 막았다: 다치지 않고, 초파리가 튕겨 나가 잠깐 멈춘다 */
                    s->p[i].guard = 0;
                    event(s, BOSS_EV_BLOCK, i, s->p[i].x, s->p[i].y);
                    f->stun = c->guard_stun;
                    for (int k = 0; k < 4; k++) slide(s, &f->x, &f->y, -cosf(f->ang) * 0.2f, -sinf(f->ang) * 0.2f, c->fly_radius);
                } else {
                    hurt_player(s, i, f->x, f->y);
                }
                f->charging = 0;
                break;
            }
        }
        if (dist(ox, oy, f->x, f->y) < 0.01f) f->charging = 0;  /* 벽에 막혔다 */
        if (f->charging == 0) { f->target = -1; f->charge_cool = c->charge_cooldown; }
        return;
    }
    /* 먹이. 보이는 먹이는 물체 자극으로 뇌에 들어가서(boss_stimulus) 뇌의 걷기 명령이 그쪽으로 걷게 한다. 여기서는
     * 먹이로 가는 중인지만 적어 둔다(그동안은 돌진하지 않는다). 닿으면 단맛 자극이 들어가고, 먹을지도 뇌가 정한다: 먹기 반응
     * (진짜 뇌에서는 주둥이를 내미는 운동 뉴런 MN9)이 기준을 넘으면 그 자리에서 먹는다. 먹는 동안에도 다가옴 자극이
     * 세면 뇌의 달아남 출력이 먼저 켜져서 위에서 이미 뛰어올랐다. 그래서 먹는 초파리에게는 살금살금 다가가야 한다 */
    int bait = f->wary > 0 ? -1 : nearest_bait(s);
    if (bait >= 0) {
        boss_bait_t *bt = &s->bait[bait];
        f->bait = bait;
        if (dist(bt->x, bt->y, f->x, f->y) > c->fly_radius + BAIT_TOUCH) {
            f->lured = 1;
        } else if (rate[BR_FEED] >= c->feed_level) {
            f->feeding = 1;
            f->v = f->w = 0.0f;
            if (!was_feeding) event(s, BOSS_EV_EAT, 255, bt->x, bt->y);
            if (--bt->left <= 0) {                   /* 다 먹었다: 기운을 조금 차린다 */
                bt->on = 0;
                f->hp += (int)c->bait_heal;
                if (f->hp > f->hp_max) f->hp = f->hp_max;
            }
            return;
        }
    }
    if (f->charge_cool == 0 && f->hurt == 0 && !f->lured) {      /* 맞은 직후와 먹이로 가는 동안에는 덤비지 않는다 */
        float rushed = 0.0f;
        for (int i = 0; i < s->n_players; i++) {             /* 벽은 빼고 참가자가 몰아붙이는 정도만 본다 */
            float level = threat(s, i);
            if (level > rushed) rushed = level;
        }
        int t = charge_target(s, rushed);
        if (t >= 0) {
            f->target = t;
            f->windup = c->charge_windup;
            f->dodged = 0;
            f->perch = 0;                            /* 벽에서 떨어져 겨눈다 */
            event(s, BOSS_EV_CHARGE, t, f->x, f->y);
            return;
        }
    }

    if (f->perch) return;

    /* 평소: 뇌의 반응을 좌우 두 명령으로 합쳐 몸에 내려보낸다. 걷는 것은 몸(boss/body.c)이 한다. 명령이 다리 여섯의
     * 걸음을 조절하고, 딛고 있는 다리가 몸을 민다. 명령이 1이면 걷는 빠르기(walk_mm_s)가 나오게 보폭을 정해 두었고,
     * 도는 빠르기는 좌우 보폭의 차이에서 나온다. 달아남과 후진은 걷는 빠르기에 대한 비만큼 명령을 키우거나 줄인다 */
    body_t *bd = &f->body;
    if (bd->x != f->x || bd->y != f->y || bd->ang != f->ang) body_place(bd, &s->bcfg, f->x, f->y, f->ang);   /* 날거나 돌진해서 옮겨졌다 */
    /* 앞으로 걷기는 걷기 명령 뉴런(DNp09)의 반응이다. 좌우 가운데 센 쪽을 쓴다(한쪽만 켜져도 걷는다) */
    float walk = rate[BR_WALK_L] > rate[BR_WALK_R] ? rate[BR_WALK_L] : rate[BR_WALK_R];
    float fwd = walk + c->run_mm_s / c->walk_mm_s * rate[BR_ESCAPE] - c->back_mm_s / c->walk_mm_s * rate[BR_BACKWARD];
    /* 도는 것은 방향 전환 명령 뉴런(DNa02)의 좌우 차이에, 걷기 명령의 좌우 차이(켜진 쪽으로 휘어 걷는다)를 더한다 */
    float turn = (rate[BR_STEER_R] - rate[BR_STEER_L]) + (rate[BR_WALK_R] - rate[BR_WALK_L]) * c->pursue_turn;
    /* 벽 피하기(몸의 규칙): 앞쪽 30도 좌우로 벽을 더듬어 가까운 쪽의 반대로 돈다. 정면이면 오른쪽으로 돈다 */
    float dl = wall_dist(s, f->x, f->y, f->ang - PI_F / 6.0f, c->wall_sense + c->fly_radius) - c->fly_radius;
    float dr = wall_dist(s, f->x, f->y, f->ang + PI_F / 6.0f, c->wall_sense + c->fly_radius) - c->fly_radius;
    float wl = clamp01(1.0f - dl / c->wall_sense), wr = clamp01(1.0f - dr / c->wall_sense);
    if (wl > 0.0f || wr > 0.0f) turn += (wl >= wr ? wl : -wr) * c->wall_turn;
    /* 오른쪽으로 돌려면 왼쪽 다리가 더 크게 딛는다. 한쪽이 음수가 되면 그쪽 다리는 뒤로 딛는다(제자리에서 돈다) */
    float left = fwd + s->turn_gain * turn, right = fwd - s->turn_gain * turn;
    float ox = f->x, oy = f->y, oa = f->ang;
    for (int k = 0; k < (int)(0.05f / BODY_DT + 0.5f); k++) {
        float px = bd->x, py = bd->y;
        body_step(bd, &s->bcfg, left, right);
        if (blocked(s, bd->x, bd->y, c->fly_radius)) {              /* 벽에 닿았다: 그 방향으로는 가지 못한다 */
            if (!blocked(s, bd->x, py, c->fly_radius)) { bd->y = py; bd->vy = 0.0f; }
            else if (!blocked(s, px, bd->y, c->fly_radius)) { bd->x = px; bd->vx = 0.0f; }
            else { bd->x = px; bd->y = py; bd->vx = bd->vy = 0.0f; }
        }
    }
    if (bd->ang > PI_F) bd->ang -= 2.0f * PI_F;
    if (bd->ang < -PI_F) bd->ang += 2.0f * PI_F;
    f->x = bd->x; f->y = bd->y; f->ang = bd->ang;
    f->v = dist(ox, oy, f->x, f->y);
    f->w = f->ang - oa;
    float want = fabsf(fwd) * s->walk_v1 / 20.0f;
    if (want > 0.02f && f->v < want * 0.25f) f->stuck_ticks++;
    else f->stuck_ticks = 0;
    if (f->stuck_ticks >= 20) {                      /* 구석에 끼었다. 크게 돌아서 빠져나간다 */
        f->ang += (stage_rng_float(&s->rng) < 0.5f ? -1.0f : 1.0f) * (PI_F / 2.0f + stage_rng_float(&s->rng));
        if (f->ang > PI_F) f->ang -= 2.0f * PI_F;
        if (f->ang < -PI_F) f->ang += 2.0f * PI_F;
        f->stuck_ticks = 0;
    }
}

/* ---- 한 틱 ------------------------------------------------------------------------- */
void boss_tick(boss_state_t *s, const float rate[BRAIN_GROUP_COUNT])
{
    s->n_ev = 0;
    if (s->outcome != BOSS_GOING) return;
    players_act(s);
    traps_fly(s);
    if (s->fly.hp > 0) fly_act(s, rate);
    fly_attitude(s);
    for (int i = 0; i < s->n_players; i++)
        s->p[i].approach += (s->p[i].closing - s->p[i].approach) / (float)(s->cfg.loom_avg_ticks > 1 ? s->cfg.loom_avg_ticks : 1);
    s->tick++;

    int alive = 0;
    for (int i = 0; i < s->n_players; i++) alive += s->p[i].alive;
    if (s->fly.hp <= 0) {
        s->outcome = BOSS_PLAYERS_WIN;
        event(s, BOSS_EV_FLY_DOWN, 255, s->fly.x, s->fly.y);
    } else if (alive == 0 || s->tick >= s->cfg.ticks) {
        s->outcome = BOSS_FLY_WINS;
    }
}

void boss_snapshot(const boss_state_t *s, boss_snap_t *o)
{
    const boss_fly_t *f = &s->fly;
    int n = 0;
    memset(o, 0, sizeof *o);
    o->tick = (uint16_t)s->tick;
    o->ticks_left = (uint16_t)(s->cfg.ticks > s->tick ? s->cfg.ticks - s->tick : 0);
    o->outcome = (uint8_t)s->outcome;
    o->n_players = (uint8_t)s->n_players;
    for (int i = 0; i < s->n_players; i++) {
        const boss_player_t *p = &s->p[i];
        o->p[i].x = (int16_t)(p->x * BOSS_FIX);
        o->p[i].y = (int16_t)(p->y * BOSS_FIX);
        o->p[i].ang = (uint16_t)(lroundf(p->ang / (2.0f * PI_F) * 65536.0f) & 0xffff);
        o->p[i].sneak = (uint8_t)p->sneak;
        o->p[i].guard = (uint8_t)p->guard;
        o->p[i].guard_cool = (uint8_t)(p->guard_cool > 255 ? 255 : p->guard_cool);
        o->p[i].z = (uint8_t)(p->z >= 2.55f ? 255 : (int)(p->z * 100.0f + 0.5f));
        o->p[i].baits = (uint8_t)p->baits;
        o->p[i].hp = (uint8_t)p->hp;
        o->p[i].alive = (uint8_t)p->alive;
        o->p[i].item = (uint8_t)p->item;
        o->p[i].ammo = (uint8_t)p->ammo;
        o->p[i].traps = (uint8_t)p->traps;
        o->p[i].cool = (uint8_t)(p->cool > 255 ? 255 : p->cool);
    }
    float a = f->ang < 0.0f ? f->ang + 2.0f * PI_F : f->ang;
    o->fly.x = (int16_t)(f->x * BOSS_FIX);
    o->fly.y = (int16_t)(f->y * BOSS_FIX);
    o->fly.ang = (uint16_t)((int)(a / (2.0f * PI_F) * 65536.0f) & 0xffff);
    o->fly.hp = (int16_t)f->hp;
    o->fly.hp_max = (int16_t)f->hp_max;
    o->fly.stun = (uint8_t)(f->stun > 255 ? 255 : f->stun);
    o->fly.windup = (uint8_t)f->windup;
    o->fly.charging = (uint8_t)f->charging;
    o->fly.wave_in = (uint8_t)(f->wave_in > 255 ? 255 : f->wave_in);
    o->fly.air = (uint8_t)(f->air > 255 ? 255 : f->air);
    o->fly.target = (uint8_t)(f->target < 0 ? 255 : f->target);
    o->fly.feeding = (uint8_t)f->feeding;
    o->fly.lured = (uint8_t)f->lured;
    o->fly.z = (uint8_t)(f->z * 100.0f > 255.0f ? 255.0f : f->z * 100.0f);
    o->fly.pose = (uint8_t)((f->perch ? BOSS_POSE_PERCH : 0) |
                            (f->air > 0 && (f->land_cmd || f->air <= s->cfg.air_land_ticks) ? BOSS_POSE_LANDING : 0));
    o->fly.pitch = (int8_t)(f->pitch / (PI_F / 2.0f) * 127.0f);
    o->fly.roll = (int8_t)(f->roll / (PI_F / 2.0f) * 127.0f);
    for (int k = 0; k < BODY_LEGS; k++) {
        float ph = fmodf(f->body.phase[k], 2.0f * PI_F), mg = f->body.mag[k] * 127.0f;
        if (ph < 0.0f) ph += 2.0f * PI_F;
        o->fly.leg[k] = (uint8_t)((int)(ph / (2.0f * PI_F) * 256.0f) & 0xff);
        o->fly.leg_mag[k] = (uint8_t)(mg > 255.0f ? 255.0f : mg);
    }
    for (int k = 0; k < BOSS_BAITS; k++) {
        if (!s->bait[k].on) continue;
        int left = s->cfg.bait_ticks > 0 ? s->bait[k].left * 255 / s->cfg.bait_ticks : 255;
        o->bait[o->n_bait].x = (int16_t)(s->bait[k].x * BOSS_FIX);
        o->bait[o->n_bait].y = (int16_t)(s->bait[k].y * BOSS_FIX);
        o->bait[o->n_bait].left = (uint8_t)(left < 0 ? 0 : (left > 255 ? 255 : left));
        o->n_bait++;
    }
    for (int k = 0; k < BOSS_PROJ; k++) {
        if (!s->proj[k].on) continue;
        o->proj[n].x = (int16_t)(s->proj[k].x * BOSS_FIX);
        o->proj[n].y = (int16_t)(s->proj[k].y * BOSS_FIX);
        n++;
    }
    o->n_proj = (uint8_t)n;
    o->n_ev = (uint8_t)s->n_ev;
    memcpy(o->ev, s->ev, sizeof(boss_event_t) * (size_t)s->n_ev);
}
