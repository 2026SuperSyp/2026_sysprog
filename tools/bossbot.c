/* 보스전 봇. 계산부를 화면 없이 여러 판 돌려 전략별 승률과 초파리의 행동을 표로 낸다.
 *   build/dev/bossbot [--brain=real] [--games=N] [--real-games=N] [--lives=N] [--config=boss/config.ini]
 *   --games는 가짜 뇌로 도는 판 수(기본 40), --real-games는 진짜 뇌로 도는 판 수(기본 12)다
 *   build/dev/bossbot --train [--gens=N] [--pop=N] [--workers=N] [--train-games=N] [--target-sec=초] [--target-win=%] [--out=파일]
 *   병렬 자가 대전 훈련(아래 "훈련"). 결과를 boss/trained.ini에 쓴다
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
    double ticks, dealt, taken, charges, hits, escape, backward, near_wall, stuck_turns, takeoffs, descends;
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
            st->descends += s.ev[e].kind == BOSS_EV_DESCEND;      /* 뇌의 착지 명령으로 내려앉은 횟수 */
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
           "돌진/판 %5.1f  맞힘/판 %4.1f  도약/판 %5.1f  착지 명령/판 %5.1f  | 달아남 %4.1f%%  뒤로 %4.1f%%  벽 옆 %4.1f%%  끼어서 돎/판 %4.1f\n",
           brain, strat_name[strat], n, st->games, 100.0 * st->wins / g, t / g / 20.0, st->dealt / g / n, st->taken / g / n,
           st->charges / g, st->hits / g, st->takeoffs / g, st->descends / g, 100.0 * st->escape / t, 100.0 * st->backward / t, 100.0 * st->near_wall / t,
           st->stuck_turns / g);
}

/* ---- 병렬 자가 대전 훈련(--train) ------------------------------------------------------
 * 보스전의 게임 규칙 수치(체력, 붙어 있는 시간, 다시 뛰지 못하는 시간 등)를 봇과의 대전으로 맞춘다. 잰 값(걷는 빠르기 등)과
 * 뇌는 건드리지 않는다.
 *   부모: 후보(수치 한 벌)를 여러 개 만들어 공유 메모리의 표에 적고, 일꾼들에게 "몇 번 후보를 해 봐라"를 파이프로 보낸다.
 *   일꾼(fork한 프로세스, CPU 수만큼): 그 후보로 화면 없는 보스전을 전략 3가지 x 인원 3가지로 돌리고, 결과를 표의 제 칸에
 *     적은 뒤 "끝났다"를 파이프로 알린다. 표는 mmap(MAP_SHARED)이라 부모와 일꾼이 같은 메모리를 본다.
 *   부모는 가장 좋은 후보를 남기고, 그것을 조금씩 바꾼 후보들로 다음 세대를 만든다. 일꾼이 죽으면 그 일을 다른 일꾼에게 준다.
 * 좋은 정도: 판의 길이가 목표(--target-sec)에, 봇의 승률이 목표(--target-win)에 가까울수록 좋다.
 * 결과는 boss/trained.ini에 쓴다(임시 파일에 쓰고 fsync한 뒤 rename). 게임은 그 파일이 있으면 설정 위에 덧씌운다 */
#include <fcntl.h>
#include <poll.h>
#include <stddef.h>
#include <sys/mman.h>
#include <sys/wait.h>
#include <unistd.h>

typedef struct { const char *key; size_t off; int is_int; float lo, hi; } knob_t;
#define KF(name, lo, hi) { #name, offsetof(boss_cfg_t, name), 0, lo, hi }
#define KI(name, lo, hi) { #name, offsetof(boss_cfg_t, name), 1, lo, hi }
static const knob_t knobs[] = {
    { "fly_hp_1", offsetof(boss_cfg_t, fly_hp), 0, 30.0f, 120.0f },
    { "fly_hp_2", offsetof(boss_cfg_t, fly_hp) + sizeof(float), 0, 60.0f, 240.0f },
    { "fly_hp_3", offsetof(boss_cfg_t, fly_hp) + 2 * sizeof(float), 0, 90.0f, 360.0f },
    KI(stun_ticks, 20, 80), KI(stun_struggle, 0, 3), KI(perch_rest_ticks, 4, 20), KI(air_rest_ticks, 6, 30),
    KF(trap_see, 0.0f, 6.0f), KI(charge_cooldown, 30, 120), KI(charge_windup, 6, 20),
};
#define N_KNOBS ((int)(sizeof knobs / sizeof knobs[0]))
#define POP_MAX 64
#define WORKERS_MAX 32

typedef struct { float v[N_KNOBS]; float fit, secs, win; } cand_t;
typedef struct { int worker, job; } done_t;

static float knob_get(const boss_cfg_t *c, int k)
{
    const char *p = (const char *)c + knobs[k].off;
    return knobs[k].is_int ? (float)*(const int *)(const void *)p : *(const float *)(const void *)p;
}
static void knob_set(boss_cfg_t *c, int k, float v)
{
    char *p = (char *)c + knobs[k].off;
    if (knobs[k].is_int) *(int *)(void *)p = (int)lroundf(v); else *(float *)(void *)p = v;
}

/* 후보 하나를 해 본다. 일꾼이 부른다 */
static void train_eval(const boss_cfg_t *base, cand_t *cd, brain_mode_t mode, int games, float target_sec, float target_win)
{
    boss_cfg_t c = *base;
    float fit = 0.0f, secs = 0.0f, win = 0.0f;
    for (int k = 0; k < N_KNOBS; k++) knob_set(&c, k, cd->v[k]);
    for (int strat = 0; strat < 3; strat++) {
        for (int n = 1; n <= 3; n++) {
            stat_t st;
            memset(&st, 0, sizeof st);
            for (int g = 0; g < games; g++) play(&c, mode, strat, n, (uint32_t)(1000 * n + 17 * g + strat), &st);
            float s = (float)(st.ticks / st.games / 20.0), w = (float)st.wins / (float)st.games;
            fit -= fabsf(s - target_sec) / target_sec + 2.0f * fabsf(w - target_win);
            secs += s / 9.0f;
            win += w / 9.0f;
        }
    }
    cd->fit = fit; cd->secs = secs; cd->win = win;
}

static int train(const boss_cfg_t *base, brain_mode_t mode, int gens, int pop, int workers, int games,
                 float target_sec, float target_win, const char *out_path)
{
    long cpus = sysconf(_SC_NPROCESSORS_ONLN);
    if (workers <= 0) workers = cpus > 0 ? (int)cpus : 2;
    if (workers > WORKERS_MAX) workers = WORKERS_MAX;
    if (pop <= 0) pop = workers * 2;
    if (pop < 2) pop = 2;
    if (pop > POP_MAX) pop = POP_MAX;

    /* 후보 표: 부모와 일꾼이 같이 보는 메모리 */
    cand_t *tab = mmap(NULL, sizeof(cand_t) * POP_MAX, PROT_READ | PROT_WRITE, MAP_SHARED | MAP_ANONYMOUS, -1, 0);
    int done_pipe[2], job_w[WORKERS_MAX], busy[WORKERS_MAX], alive[WORKERS_MAX];
    pid_t pid[WORKERS_MAX];
    if (tab == MAP_FAILED || pipe(done_pipe) != 0) { perror("훈련을 시작할 수 없습니다"); return 1; }
    fflush(stdout);
    for (int w = 0; w < workers; w++) {
        int job[2];
        if (pipe(job) != 0) { perror("pipe"); return 1; }
        pid[w] = fork();
        if (pid[w] < 0) { perror("fork"); return 1; }
        if (pid[w] == 0) {                           /* 일꾼: 일을 받아 하고, 끝났다고 알린다. 파이프가 닫히면 끝낸다 */
            close(job[1]);
            close(done_pipe[0]);
            for (int k = 0; k < w; k++) close(job_w[k]);
            int j;
            while (read(job[0], &j, sizeof j) == (ssize_t)sizeof j) {
                train_eval(base, &tab[j], mode, games, target_sec, target_win);
                done_t d = { w, j };
                if (write(done_pipe[1], &d, sizeof d) != (ssize_t)sizeof d) break;      /* PIPE_BUF보다 작아 한 번에 쓰인다 */
            }
            _exit(0);
        }
        close(job[0]);
        job_w[w] = job[1];
        busy[w] = -1;
        alive[w] = 1;
    }
    close(done_pipe[1]);

    stage_rng_t r;
    stage_rng_seed(&r, 20261125u);
    cand_t best;
    for (int k = 0; k < N_KNOBS; k++) best.v[k] = knob_get(base, k);
    best.fit = -1e9f;
    printf("훈련: 일꾼 %d개, 세대 %d번, 세대마다 후보 %d개, 후보마다 %d판 x 9가지. 목표: 판 길이 %.0f초, 봇 승률 %.0f%%\n",
           workers, gens, pop, games, target_sec, target_win * 100.0f);

    for (int gen = 0; gen <= gens; gen++) {
        /* 0번은 지금까지의 가장 좋은 것, 나머지는 그것을 조금씩 바꾼 것. 첫 세대(gen 0)는 지금 설정만 재 본다 */
        int n = gen == 0 ? 1 : pop, queue[POP_MAX], q_n = 0, left;
        tab[0] = best;
        for (int i = 1; i < n; i++) {
            tab[i] = best;
            for (int k = 0; k < N_KNOBS; k++) {
                if (stage_rng_float(&r) > 0.35f) continue;
                float g = stage_rng_float(&r) + stage_rng_float(&r) + stage_rng_float(&r) - 1.5f;     /* 종 모양에 가까운 흔들림 */
                float v = tab[i].v[k] + g * 0.3f * (knobs[k].hi - knobs[k].lo);
                v = v < knobs[k].lo ? knobs[k].lo : (v > knobs[k].hi ? knobs[k].hi : v);
                tab[i].v[k] = knobs[k].is_int ? roundf(v) : v;
            }
        }
        for (int i = gen == 0 ? 0 : 1; i < n; i++) queue[q_n++] = i;
        left = q_n;
        while (left > 0) {
            int any = 0;
            for (int w = 0; w < workers && q_n > 0; w++) {       /* 노는 일꾼에게 일을 준다 */
                if (!alive[w] || busy[w] >= 0) continue;
                int j = queue[--q_n];
                if (write(job_w[w], &j, sizeof j) == (ssize_t)sizeof j) busy[w] = j;
                else { alive[w] = 0; queue[q_n++] = j; }
            }
            for (int w = 0; w < workers; w++) any += alive[w];
            if (!any) { printf("훈련: 일꾼이 모두 죽었습니다\n"); return 1; }
            struct pollfd pf = { done_pipe[0], POLLIN, 0 };
            if (poll(&pf, 1, 1000) > 0) {
                done_t d;
                if (read(done_pipe[0], &d, sizeof d) == (ssize_t)sizeof d) { busy[d.worker] = -1; left--; }
            }
            /* 죽은 일꾼이 있으면 거두고, 하던 일을 다시 줄에 세운다 */
            int status;
            pid_t dead;
            while ((dead = waitpid(-1, &status, WNOHANG)) > 0) {
                for (int w = 0; w < workers; w++) {
                    if (pid[w] != dead || !alive[w]) continue;
                    alive[w] = 0;
                    close(job_w[w]);
                    if (busy[w] >= 0) { queue[q_n++] = busy[w]; busy[w] = -1; }
                    printf("훈련: 일꾼 %d번이 죽어서 하던 일을 다른 일꾼에게 넘깁니다\n", w);
                }
            }
        }
        int top = 0;
        for (int i = 1; i < n; i++) if (tab[i].fit > tab[top].fit) top = i;
        if (gen == 0) printf("  지금 설정    점수 %7.3f  평균 %5.1f초  승률 %5.1f%%\n", tab[0].fit, tab[0].secs, tab[0].win * 100.0f);
        else printf("  세대 %2d      점수 %7.3f  평균 %5.1f초  승률 %5.1f%%%s\n", gen, tab[top].fit, tab[top].secs, tab[top].win * 100.0f,
                    top ? "  (더 좋은 것을 찾음)" : "");
        best = tab[top];
        fflush(stdout);
    }
    for (int w = 0; w < workers; w++) if (alive[w]) close(job_w[w]);          /* 파이프를 닫으면 일꾼이 끝낸다 */
    while (waitpid(-1, NULL, 0) > 0) { }

    /* 결과를 쓴다: 임시 파일에 다 쓰고 디스크에 적은 뒤 이름을 바꾼다. 도중에 죽어도 반만 쓰인 파일이 남지 않는다 */
    char tmp[512], text[2048];
    int len = snprintf(text, sizeof text, "; 훈련 결과(make boss-train). 이 파일이 있으면 config.ini 위에 덧씌운다. 지우면 원래대로다\n"
                       "; 목표: 판 길이 %.0f초, 봇 승률 %.0f%%. 얻은 것: 평균 %.1f초, 승률 %.1f%%\n",
                       target_sec, target_win * 100.0f, best.secs, best.win * 100.0f);
    for (int k = 0; k < N_KNOBS; k++) {
        printf("  %-18s %8.2f -> %8.2f\n", knobs[k].key, knob_get(base, k), best.v[k]);
        len += snprintf(text + len, sizeof text - (size_t)len, knobs[k].is_int ? "%s = %.0f\n" : "%s = %.3f\n", knobs[k].key, best.v[k]);
    }
    snprintf(tmp, sizeof tmp, "%s.tmp", out_path);
    int fd = open(tmp, O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, 0644);
    if (fd < 0 || write(fd, text, (size_t)len) != (ssize_t)len || fsync(fd) != 0) { perror(tmp); if (fd >= 0) close(fd); return 1; }
    close(fd);
    if (rename(tmp, out_path) != 0) { perror(out_path); return 1; }
    printf("훈련 결과를 %s 에 썼습니다\n", out_path);
    munmap(tab, sizeof(cand_t) * POP_MAX);
    return 0;
}

int main(int argc, char **argv)
{
    const char *config = "boss/config.ini";
    int real = 0, games = 40, real_games = 12;
    int do_train = 0, gens = 12, pop = 0, workers = 0, train_games = 3;
    float target_sec = 40.0f, target_win = 0.8f;
    const char *out_path = "boss/" BOSS_TRAINED_FILE;
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--brain=real") == 0) real = 1;
        else if (strncmp(argv[i], "--games=", 8) == 0) games = atoi(argv[i] + 8);
        else if (strncmp(argv[i], "--real-games=", 13) == 0) real_games = atoi(argv[i] + 13);
        else if (strncmp(argv[i], "--lives=", 8) == 0) start_lives = atoi(argv[i] + 8);
        else if (strncmp(argv[i], "--config=", 9) == 0) config = argv[i] + 9;
        else if (strcmp(argv[i], "--train") == 0) do_train = 1;
        else if (strncmp(argv[i], "--gens=", 7) == 0) gens = atoi(argv[i] + 7);
        else if (strncmp(argv[i], "--pop=", 6) == 0) pop = atoi(argv[i] + 6);
        else if (strncmp(argv[i], "--workers=", 10) == 0) workers = atoi(argv[i] + 10);
        else if (strncmp(argv[i], "--train-games=", 14) == 0) train_games = atoi(argv[i] + 14);
        else if (strncmp(argv[i], "--target-sec=", 13) == 0) target_sec = (float)atof(argv[i] + 13);
        else if (strncmp(argv[i], "--target-win=", 13) == 0) target_win = (float)atof(argv[i] + 13) / 100.0f;
        else if (strncmp(argv[i], "--out=", 6) == 0) out_path = argv[i] + 6;
        else { printf("모르는 옵션: %s\n", argv[i]); return 2; }
    }
    if (start_lives < 1 || start_lives > 3) { printf("--lives는 1부터 3까지입니다\n"); return 2; }
    boss_cfg_t c;
    boss_cfg_load(&c, config);
    if (start_lives != 3) printf("참가자의 체력 %d로 시작\n", start_lives);
    if (do_train) {
        /* 진짜 뇌로 훈련하면 배선을 fork 전에 한 번만 읽는다. 일꾼들은 그 메모리를 그대로 같이 쓴다(쓰지 않는 한 복사되지 않는다) */
        if (real && brain_host_open_real("data") != 0) { printf("진짜 뇌의 자료(data/)가 없습니다\n"); return 1; }
        return train(&c, real ? BRAIN_REAL : BRAIN_FAKE_FAST, gens, pop, workers, train_games < 1 ? 1 : train_games,
                     target_sec, target_win, out_path);
    }

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
