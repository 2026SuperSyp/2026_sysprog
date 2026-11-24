/* 보스전 봇. 계산부를 화면 없이 여러 판 돌려 전략별 승률과 초파리의 행동을 표로 낸다.
 *   build/dev/bossbot [--brain=real] [--games=N] [--real-games=N] [--lives=N] [--config=boss/config.ini]
 *   --games는 가짜 뇌로 도는 판 수(기본 40), --real-games는 진짜 뇌로 도는 판 수(기본 12)다
 *   --lives는 참가자의 시작 체력(1~3, 기본 3)이다. 서버는 모두에게 3을 준다. 체력이 승률을 얼마나 바꾸는지 볼 때 쓴다
 *
 * 참가자 전략(사람처럼 3틱에 한 번만 키를 누른다)
 *   rush    초파리 쪽으로 돌아 다가가 파리채로 친다. 끈끈이가 닿는 거리면 먼저 던져 붙인다
 *   sneak   rush와 같되 4칸 안에서는 살금살금(Shift) 다가간다. 초파리가 붙어 있으면 그냥 달려간다
 *   kite    3.5~7칸 거리를 지키며 스프레이를 쏜다. 다 쓰면 rush처럼 한다
 *   random  아무 키나 누른다
 *   mix     sneak, kite, sneak (3명일 때)
 * 초파리는 뇌가 움직인다: 기본은 같은 프로세스 안의 가짜 뇌(fast), --brain=real이면 진짜 배선(data/). */
#include "boss/boss.h"
#include "brain/brain_host.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

enum { RUSH, SNEAK, KITE, RANDOM, N_STRAT };
static const char *strat_name[] = { "rush", "sneak", "kite", "random", "mix" };

typedef struct { float lx, ly; int blocked, last; } bot_t;

static float fdist(const boss_state_t *s, int i) { return hypotf(s->fly.x - s->p[i].x, s->fly.y - s->p[i].y); }

/* 초파리 쪽을 보고 있으면 0, 아니면 돌 키 */
static int face_fly(const boss_state_t *s, int i)
{
    float a = atan2f(s->fly.y - s->p[i].y, s->fly.x - s->p[i].x);
    int want = ((int)lroundf(a / (2.0f * 3.14159265f / BOSS_DIRS)) % BOSS_DIRS + BOSS_DIRS) % BOSS_DIRS;
    int cur = (int)lroundf(s->p[i].ang / (2.0f * 3.14159265f / BOSS_DIRS)) % BOSS_DIRS;   /* 봇은 키로만 돌아서 늘 22.5도 단위다 */
    int diff = (want - cur + BOSS_DIRS) % BOSS_DIRS;
    if (diff == 0) return 0;
    return diff <= BOSS_DIRS / 2 ? SK_RIGHT : SK_LEFT;
}

static int pick(const boss_state_t *s, int i, int item) { return s->p[i].item == item ? SK_ACT : SK_1 + item; }

static int bot_key(const boss_state_t *s, int i, int strat, bot_t *b, stage_rng_t *r)
{
    static const int any[] = { SK_UP, SK_DOWN, SK_LEFT, SK_RIGHT, SK_ACT, SK_SUB, SK_1, SK_2, SK_3 };
    const boss_player_t *p = &s->p[i];
    const boss_fly_t *f = &s->fly;
    if (strat == RANDOM) return any[stage_rng_range(r, 0, 8)];

    /* 앞으로 가려 했는데 그대로면 막힌 것이다. 몇 번 옆으로 돌아 피한다 */
    if (b->blocked > 0) { b->blocked--; return b->blocked % 2 ? SK_RIGHT : SK_UP; }
    int turn = face_fly(s, i);
    float d = fdist(s, i);
    if (turn) return turn;
    /* 초파리는 다가가면 뛰어올라 달아난다. 먼저 끈끈이로 붙이고, 붙어 있는 동안 다가가 친다 */
    if (p->traps > 0 && f->stun == 0 && f->air == 0 && d < s->cfg.trap_range - 1.0f) return pick(s, i, BOSS_TRAP);
    if (strat == KITE && p->ammo > 0 && f->stun == 0) {
        /* 사거리 안에서 되도록 멀리 선다 */
        float far = s->cfg.spray_range - 1.0f < 7.0f ? s->cfg.spray_range - 1.0f : 7.0f;
        float near = far - 3.5f > 1.5f ? far - 3.5f : 1.5f;
        if (d < near) return SK_DOWN;
        if (d > far) return SK_UP;
        return pick(s, i, BOSS_SPRAY);
    }
    float reach = s->cfg.fly_radius + s->cfg.player_radius + s->cfg.swat_range - 0.05f;
    if (d > reach) return (strat == SNEAK && f->stun == 0 && d < reach + 4.0f) ? (SK_UP | BOSS_SLOW) : SK_UP;
    return pick(s, i, BOSS_SWATTER);
}

typedef struct {
    int games, wins;
    double ticks, dealt, taken, charges, hits, escape, backward, near_wall, stuck_turns, takeoffs;
} stat_t;

static int near_wall(const boss_state_t *s)
{
    float r = s->cfg.fly_radius + 0.5f;
    for (int k = 0; k < 8; k++) {
        float a = (float)k * 3.14159265f / 4.0f;
        if (boss_solid(s, (int)floorf(s->fly.x + cosf(a) * r), (int)floorf(s->fly.y + sinf(a) * r))) return 1;
    }
    return 0;
}

static double now_ms(void)
{
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return (double)t.tv_sec * 1000.0 + (double)t.tv_nsec / 1e6;
}

/* 다가옴 자극의 세기별로 뇌가 내는 달아남 반응을 찍는다(자극을 1초 준 뒤의 값).
 * both가 1이면 왼쪽과 오른쪽에 같은 세기를 준다(정면의 벽이나 정면에서 오는 사람) */
static void probe(brain_mode_t mode, const char *name, int both)
{
    printf("%s: 다가옴 자극(%s) -> 달아남 반응:", name, both ? "양쪽에 각각" : "한쪽");
    for (int k = 1; k <= 10; k++) {
        float level = (float)k / (both ? 20.0f : 10.0f), e = 0.0f;
        brain_host_reset(1, mode);
        for (int t = 0; t < 20; t++) {
            brain_host_begin_tick();
            brain_stimulate(BR_LOOM_L, level);
            if (both) brain_stimulate(BR_LOOM_R, level);
            e = brain_rate(BR_ESCAPE);
            brain_host_end_tick();
        }
        printf(" %.2f->%.2f", level, e);
    }
    printf("\n");
}

static double tick_ms_sum, tick_ms_max;
static long tick_n;
static int start_lives = 3;          /* --lives */

static void play(const boss_cfg_t *c, brain_mode_t mode, int strat, int n, uint32_t seed, stat_t *st)
{
    static boss_state_t s;
    static const int mix[3] = { SNEAK, KITE, SNEAK };
    int lives[3] = { start_lives, start_lives, start_lives };
    bot_t bots[3];
    stage_rng_t r;
    memset(bots, 0, sizeof bots);
    stage_rng_seed(&r, seed * 2654435761u + 1u);
    boss_init(&s, c, seed, n, lives);
    brain_host_reset(seed, mode);
    while (s.outcome == BOSS_GOING) {
        if (s.tick % 3 == 0) {
            for (int i = 0; i < n; i++) {
                if (!s.p[i].alive) continue;
                int k = bot_key(&s, i, strat == N_STRAT ? mix[i] : strat, &bots[i], &r);
                if (bots[i].last == SK_UP && s.p[i].x == bots[i].lx && s.p[i].y == bots[i].ly) bots[i].blocked = 3;
                bots[i].last = k;
                bots[i].lx = s.p[i].x;
                bots[i].ly = s.p[i].y;
                boss_input(&s, i, k);
            }
        }
        float stim[BRAIN_GROUP_COUNT], rate[BRAIN_GROUP_COUNT];
        double t0 = now_ms();
        brain_host_begin_tick();
        boss_stimulus(&s, stim);
        for (int g = 0; g < BRAIN_GROUP_COUNT; g++) if (stim[g] > 0.0f) brain_stimulate((brain_group_t)g, stim[g]);
        for (int g = 0; g < BRAIN_GROUP_COUNT; g++) rate[g] = brain_rate((brain_group_t)g);
        brain_host_end_tick();
        int ang_before = s.fly.stuck_ticks;
        boss_tick(&s, rate);
        double dt = now_ms() - t0;
        tick_ms_sum += dt;
        tick_n++;
        if (dt > tick_ms_max) tick_ms_max = dt;
        if (ang_before == 19 && s.fly.stuck_ticks == 0) st->stuck_turns++;
        for (int e = 0; e < s.n_ev; e++) {
            st->charges += s.ev[e].kind == BOSS_EV_CHARGE;
            st->hits += s.ev[e].kind == BOSS_EV_HIT_PLAYER;
            st->takeoffs += s.ev[e].kind == BOSS_EV_TAKEOFF;
        }
        if (s.fly.stun == 0 && s.fly.windup + s.fly.charging == 0) {
            st->escape += rate[BR_ESCAPE] >= 0.5f;
            st->backward += rate[BR_BACKWARD] >= 0.5f;
        }
        st->near_wall += near_wall(&s);
    }
    st->games++;
    st->wins += s.outcome == BOSS_PLAYERS_WIN;
    st->ticks += s.tick;
    for (int i = 0; i < n; i++) { st->dealt += s.dmg_dealt[i]; st->taken += s.dmg_taken[i]; }
}

static void row(const char *brain, int strat, int n, const stat_t *st)
{
    double g = st->games, t = st->ticks;
    printf("%-5s %-6s %d명 %4d판  승률 %5.1f%%  평균 %5.1f초  준 피해/명 %6.1f  받은 피해/명 %4.2f  "
           "돌진/판 %5.1f  맞힘/판 %4.1f  도약/판 %5.1f  | 달아남 %4.1f%%  뒤로 %4.1f%%  벽 옆 %4.1f%%  끼어서 돎/판 %4.1f\n",
           brain, strat_name[strat], n, st->games, 100.0 * st->wins / g, t / g / 20.0, st->dealt / g / n, st->taken / g / n,
           st->charges / g, st->hits / g, st->takeoffs / g, 100.0 * st->escape / t, 100.0 * st->backward / t, 100.0 * st->near_wall / t,
           st->stuck_turns / g);
}

int main(int argc, char **argv)
{
    const char *config = "boss/config.ini";
    int real = 0, games = 40, real_games = 12;
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--brain=real") == 0) real = 1;
        else if (strncmp(argv[i], "--games=", 8) == 0) games = atoi(argv[i] + 8);
        else if (strncmp(argv[i], "--real-games=", 13) == 0) real_games = atoi(argv[i] + 13);
        else if (strncmp(argv[i], "--lives=", 8) == 0) start_lives = atoi(argv[i] + 8);
        else if (strncmp(argv[i], "--config=", 9) == 0) config = argv[i] + 9;
        else { printf("모르는 옵션: %s\n", argv[i]); return 2; }
    }
    if (start_lives < 1 || start_lives > 3) { printf("--lives는 1부터 3까지입니다\n"); return 2; }
    boss_cfg_t c;
    boss_cfg_load(&c, config);
    if (start_lives != 3) printf("참가자의 체력 %d로 시작\n", start_lives);

    for (int pass = 0; pass < 1 + real; pass++) {
        brain_mode_t mode = pass ? BRAIN_REAL : BRAIN_FAKE_FAST;
        int g = pass ? real_games : games;               /* 진짜 뇌는 발표용 빌드에서 한 판에 2초쯤 걸린다 */
        if (pass && brain_host_open_real("data") != 0) { printf("진짜 뇌의 자료(data/)가 없어 건너뜁니다\n"); break; }
        probe(mode, pass ? "real" : "fake", 0);
        probe(mode, pass ? "real" : "fake", 1);
        tick_ms_sum = tick_ms_max = 0.0;
        tick_n = 0;
        for (int strat = 0; strat <= N_STRAT; strat++) {
            for (int n = 1; n <= 3; n++) {
                if (strat == N_STRAT && n != 3) continue;
                stat_t st;
                memset(&st, 0, sizeof st);
                for (int k = 0; k < g; k++) play(&c, mode, strat, n, (uint32_t)(1000 * n + 17 * k + strat), &st);
                row(pass ? "real" : "fake", strat, n, &st);
            }
        }
        printf("%s: 한 틱(뇌 포함) 평균 %.3fms 최대 %.3fms (%ld틱)\n", pass ? "real" : "fake",
               tick_ms_sum / (double)(tick_n ? tick_n : 1), tick_ms_max, tick_n);
    }
    return 0;
}
