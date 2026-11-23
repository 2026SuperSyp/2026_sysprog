/* 보스전 계산부. 규칙과 수치의 뜻은 boss.h와 boss/config.ini에 있다.
 *
 * 초파리의 움직임은 뇌가 정한다. 계산부는 장면을 자극으로 바꿔 줄 뿐이다.
 *   참가자가 다가온다   -> 그쪽의 다가옴 자극(BR_LOOM_L/R). 빨리 다가올수록, 가까울수록 세다
 *   벽이 앞에 있다      -> 그쪽의 다가옴 자극. 벽도 다가오는 큰 물체로 본다(벽에 박혀 서 있지 않게 한다)
 * 뇌의 반응: 한쪽에서 다가오면 반대쪽으로 돌아 달아나고(BR_TURN_*, BR_ESCAPE), 양쪽이면 뒤로 간다(BR_BACKWARD).
 * 공격할 때는 계산부의 규칙이 정한다. 몰리지 않을 때(다가옴 자극이 rush_level보다 약할 때)만 가까운 참가자에게
 * 돌진한다. 그래서 "달려드는 사람은 피하고, 몰리지 않으면 덤빈다"로 보인다.
 * 가려는데 오래 못 가면(구석에 낀 경우) 무작위로 크게 돈다. 뇌의 답을 바꾸는 것이 아니라 계산부의 규칙이다. */
#include "boss/boss.h"
#include "core/cfg.h"

#include <math.h>
#include <string.h>

#define PI_F 3.14159265f

/* ---- 설정 ---------------------------------------------------------------------- */
void boss_cfg_default(boss_cfg_t *c)
{
    memset(c, 0, sizeof *c);
    c->ticks = 180 * 20;
    c->fly_hp_per_player = 100.0f;
    c->fly_radius = 0.6f;
    c->player_radius = 0.25f;
    c->step = 0.5f;
    c->fly_speed = 0.08f;
    c->fly_turn = 0.25f;
    c->swat_range = 1.2f;  c->swat_damage = 10.0f; c->swat_cooldown = 8;
    c->spray_range = 10.0f; c->spray_damage = 3.0f; c->spray_ammo = 20; c->spray_cooldown = 3;
    c->trap_speed = 0.6f;  c->trap_range = 8.0f;  c->trap_count = 2; c->stun_ticks = 60;
    c->charge_range = 5.0f; c->charge_speed = 0.35f; c->charge_windup = 12; c->charge_ticks = 20;
    c->charge_cooldown = 80;
    c->wave_radius = 3.0f; c->wave_push = 2.5f; c->wave_period = 200;
    c->loom_range = 5.0f;  c->wall_sense = 1.5f; c->rush_level = 0.4f;
}

void boss_cfg_load(boss_cfg_t *c, const char *path)
{
    boss_cfg_default(c);
    cfg_load(path);
#define F(k) c->k = cfg_get(#k, c->k)
#define I(k) c->k = (int)cfg_get(#k, (float)c->k)
    I(ticks); F(fly_hp_per_player); F(fly_radius); F(player_radius); F(step); F(fly_speed); F(fly_turn);
    F(swat_range); F(swat_damage); I(swat_cooldown);
    F(spray_range); F(spray_damage); I(spray_ammo); I(spray_cooldown);
    F(trap_speed); F(trap_range); I(trap_count); I(stun_ticks);
    F(charge_range); F(charge_speed); I(charge_windup); I(charge_ticks); I(charge_cooldown);
    F(wave_radius); F(wave_push); I(wave_period);
    F(loom_range); F(wall_sense); F(rush_level);
#undef F
#undef I
}

/* ---- 맵과 기하 ------------------------------------------------------------------- */
/* 바깥 벽, 네 귀퉁이 쪽의 2x2 기둥, 가운데 2x2 기둥 */
static void build_map(boss_state_t *s)
{
    static const int pillars[][2] = { { 5, 5 }, { 17, 5 }, { 5, 17 }, { 17, 17 }, { 11, 11 } };
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

static float dir_angle(int dir) { return (float)dir * (2.0f * PI_F / BOSS_DIRS); }
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
    static const float start_x[BOSS_PLAYERS] = { 9.5f, 12.0f, 14.5f };
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
        p->y = 20.5f;
        p->dir = BOSS_DIRS * 3 / 4;                  /* 위쪽(-y)을 본다 */
        p->hp = lives ? lives[i] : 3;
        if (p->hp < 1) p->hp = 1;
        p->alive = 1;
        p->item = BOSS_SWATTER;
        p->ammo = c->spray_ammo;
        p->traps = c->trap_count;
        p->last_dist = dist(p->x, p->y, 12.0f, 5.5f);
    }
    s->fly.x = 12.0f;
    s->fly.y = 5.5f;
    s->fly.ang = PI_F / 2.0f;                        /* 아래쪽(참가자 쪽)을 본다 */
    s->fly.hp = s->fly.hp_max = (int)(c->fly_hp_per_player * (float)n_players + 0.5f);
    s->fly.wave_in = c->wave_period;
    s->fly.target = -1;
}

void boss_input(boss_state_t *s, int player, int key)
{
    if (player < 0 || player >= s->n_players || s->outcome != BOSS_GOING) return;
    boss_player_t *p = &s->p[player];
    if (!p->alive) return;                           /* 쓰러진 사람의 키는 쓰지 않는다 */
    switch (key) {
    case SK_UP:    if (p->move < 2) p->move++; break;
    case SK_DOWN:  if (p->move > -2) p->move--; break;
    case SK_LEFT:  if (p->turn > -2) p->turn--; break;
    case SK_RIGHT: if (p->turn < 2) p->turn++; break;
    case SK_1:     p->pick = 1 + BOSS_SWATTER; break;
    case SK_2:     p->pick = 1 + BOSS_SPRAY; break;
    case SK_3:     p->pick = 1 + BOSS_TRAP; break;
    case SK_SUB:   p->pick = 1 + (p->item + 1) % BOSS_ITEMS; break;
    case SK_ACT:   p->use = 1; break;
    default: break;
    }
}

/* ---- 자극 ------------------------------------------------------------------------- */
/* 초파리 기준의 왼쪽/오른쪽 몫. side는 방향의 sin(-1이 왼쪽 옆, 1이 오른쪽 옆, 0이 앞이나 뒤) */
static void add_side(float *l, float *r, float level, float side)
{
    float wl = clamp01(0.5f - side), wr = clamp01(0.5f + side);
    if (level * wl > *l) *l = level * wl;
    if (level * wr > *r) *r = level * wr;
}

/* 벽까지의 거리(앞쪽으로 ang 방향, 최대 range) */
static float wall_dist(const boss_state_t *s, float x, float y, float ang, float range)
{
    float cx = cosf(ang), cy = sinf(ang);
    for (float d = 0.1f; d <= range; d += 0.1f)
        if (solid_at(s, x + cx * d, y + cy * d)) return d;
    return range + 1.0f;
}

void boss_stimulus(const boss_state_t *s, float stim[BRAIN_GROUP_COUNT])
{
    const boss_cfg_t *c = &s->cfg;
    const boss_fly_t *f = &s->fly;
    float hx = cosf(f->ang), hy = sinf(f->ang), l = 0.0f, r = 0.0f;
    memset(stim, 0, sizeof(float) * BRAIN_GROUP_COUNT);
    if (s->outcome != BOSS_GOING) return;

    for (int i = 0; i < s->n_players; i++) {
        const boss_player_t *p = &s->p[i];
        if (!p->alive) continue;
        float dx = p->x - f->x, dy = p->y - f->y, d = sqrtf(dx * dx + dy * dy);
        float gap = d - c->fly_radius - c->player_radius;
        if (gap >= c->loom_range || d < 1e-4f) continue;
        float close = 1.0f - gap / c->loom_range;
        float approach = clamp01((p->last_dist - d) / c->step);      /* 이번 틱에 한 걸음 다가왔으면 1 */
        float level = clamp01(close * (0.35f + 0.9f * approach));
        add_side(&l, &r, level, (hx * dy - hy * dx) / d);
    }
    /* 앞쪽 30도 좌우로 벽을 더듬는다 */
    float dl = wall_dist(s, f->x, f->y, f->ang - PI_F / 6.0f, c->wall_sense + c->fly_radius) - c->fly_radius;
    float dr = wall_dist(s, f->x, f->y, f->ang + PI_F / 6.0f, c->wall_sense + c->fly_radius) - c->fly_radius;
    float wl = 0.8f * clamp01(1.0f - dl / c->wall_sense), wr = 0.8f * clamp01(1.0f - dr / c->wall_sense);
    if (wl > l) l = wl;
    if (wr > r) r = wr;
    stim[BR_LOOM_L] = l;
    stim[BR_LOOM_R] = r;
}

/* ---- 참가자 ------------------------------------------------------------------------- */
static void hurt_fly(boss_state_t *s, int who, int dmg)
{
    if (dmg > s->fly.hp) dmg = s->fly.hp;
    s->fly.hp -= dmg;
    s->dmg_dealt[who] += dmg;
}

static void use_item(boss_state_t *s, int i)
{
    const boss_cfg_t *c = &s->cfg;
    boss_player_t *p = &s->p[i];
    boss_fly_t *f = &s->fly;
    float a = dir_angle(p->dir), ux = cosf(a), uy = sinf(a);
    if (p->cool > 0) return;

    if (p->item == BOSS_SWATTER) {
        float dx = f->x - p->x, dy = f->y - p->y, d = sqrtf(dx * dx + dy * dy);
        int hit = d <= c->fly_radius + c->player_radius + c->swat_range && d > 1e-4f &&
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
    } else {
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
    }
}

static void players_act(boss_state_t *s)
{
    const boss_cfg_t *c = &s->cfg;
    boss_fly_t *f = &s->fly;
    for (int i = 0; i < s->n_players; i++) {
        boss_player_t *p = &s->p[i];
        if (p->cool > 0) p->cool--;
        if (!p->alive) { p->move = p->turn = p->use = p->pick = 0; continue; }
        p->dir = ((p->dir + p->turn) % BOSS_DIRS + BOSS_DIRS) % BOSS_DIRS;
        if (p->move) {
            float a = dir_angle(p->dir), len = c->step * (float)p->move;
            float nx = p->x, ny = p->y;
            slide(s, &nx, &ny, cosf(a) * len, sinf(a) * len, c->player_radius);
            /* 초파리 몸을 뚫고 지나가지 않는다 */
            if (dist(nx, ny, f->x, f->y) >= c->fly_radius + c->player_radius ||
                dist(nx, ny, f->x, f->y) >= dist(p->x, p->y, f->x, f->y)) {
                p->x = nx;
                p->y = ny;
            }
        }
        if (p->pick) p->item = p->pick - 1;
        if (p->use) use_item(s, i);
        p->move = p->turn = p->use = p->pick = 0;
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

static void fly_act(boss_state_t *s, const float rate[BRAIN_GROUP_COUNT])
{
    const boss_cfg_t *c = &s->cfg;
    boss_fly_t *f = &s->fly;
    if (f->charge_cool > 0) f->charge_cool--;
    if (f->stun > 0) { f->stun--; return; }          /* 붙어 있는 동안은 움직이지도 공격하지도 않는다 */

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
            if (s->p[i].alive && dist(s->p[i].x, s->p[i].y, f->x, f->y) <= c->fly_radius + c->player_radius + 0.05f) {
                hurt_player(s, i, f->x, f->y);
                f->charging = 0;
                break;
            }
        }
        if (dist(ox, oy, f->x, f->y) < 0.01f) f->charging = 0;  /* 벽에 막혔다 */
        if (f->charging == 0) { f->target = -1; f->charge_cool = c->charge_cooldown; }
        return;
    }
    if (f->charge_cool == 0) {
        float rushed = 0.0f;
        for (int i = 0; i < s->n_players; i++) {             /* 벽은 빼고 참가자가 몰아붙이는 정도만 본다 */
            const boss_player_t *p = &s->p[i];
            float d = dist(p->x, p->y, f->x, f->y);
            if (!p->alive || d >= c->loom_range) continue;
            float approach = clamp01((p->last_dist - d) / c->step);
            float level = (1.0f - d / c->loom_range) * (0.35f + 0.9f * approach);
            if (level > rushed) rushed = level;
        }
        int t = charge_target(s, rushed);
        if (t >= 0) {
            f->target = t;
            f->windup = c->charge_windup;
            event(s, BOSS_EV_CHARGE, t, f->x, f->y);
            return;
        }
    }

    /* 평소: 뇌의 반응으로 움직인다 */
    f->ang += (rate[BR_TURN_R] - rate[BR_TURN_L]) * c->fly_turn;
    float v = c->fly_speed * (0.6f * rate[BR_FORWARD] + 1.4f * rate[BR_ESCAPE] - 1.0f * rate[BR_BACKWARD]);
    float ox = f->x, oy = f->y;
    slide(s, &f->x, &f->y, cosf(f->ang) * v, sinf(f->ang) * v, c->fly_radius);
    float moved = dist(ox, oy, f->x, f->y), want = fabsf(v);
    if (want > 0.02f && moved < want * 0.25f) f->stuck_ticks++;
    else f->stuck_ticks = 0;
    if (f->stuck_ticks >= 20) {                      /* 구석에 끼었다. 크게 돌아서 빠져나간다 */
        f->ang += (stage_rng_float(&s->rng) < 0.5f ? -1.0f : 1.0f) * (PI_F / 2.0f + stage_rng_float(&s->rng));
        f->stuck_ticks = 0;
    }
    if (f->ang > PI_F) f->ang -= 2.0f * PI_F;
    if (f->ang < -PI_F) f->ang += 2.0f * PI_F;
}

/* ---- 한 틱 ------------------------------------------------------------------------- */
void boss_tick(boss_state_t *s, const float rate[BRAIN_GROUP_COUNT])
{
    s->n_ev = 0;
    if (s->outcome != BOSS_GOING) return;
    players_act(s);
    traps_fly(s);
    if (s->fly.hp > 0) fly_act(s, rate);
    for (int i = 0; i < s->n_players; i++)
        s->p[i].last_dist = dist(s->p[i].x, s->p[i].y, s->fly.x, s->fly.y);
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
        o->p[i].dir = (uint8_t)p->dir;
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
