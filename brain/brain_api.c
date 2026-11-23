/* 뇌 창구. brain.h의 세 함수는 여기 한 곳에만 있다.
 *
 * 뒤에 뇌가 둘 중 하나로 붙는다. 어느 쪽이든 스테이지 코드는 같다.
 *  - 같은 프로세스 안의 모델(brain_model.c). make run과 make bot이 쓴다.
 *  - 뇌 프로세스. 환경 변수 SSP_BRAIN_SHM에 공유 메모리 이름이 있으면 거기에 붙는다(서버가 띄운 경우).
 *    틱 끝에 자극을 올리고, 다음 틱 직전에 뇌가 내놓은 반응을 가져온다. 뇌가 그 틱을 아직
 *    못 마쳤으면 직전 값을 쓰고 늦은 틱으로 센다. SSP_BRAIN_WAIT=1이면(화면 없이 빨리 돌 때)
 *    뇌가 마칠 때까지 기다리되, 시간 제한을 넘으면 직전 값을 쓴다. */
#include "brain.h"
#include "brain/brain_host.h"
#include "brain/brain_model.h"
#include "brain/brain_shm.h"
#include "core/log.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define N          BRAIN_GROUP_COUNT
#define FIRE_LEVEL 0.5f        /* brain_fired의 기준 */
#define WAIT_MS    500         /* 기다리는 방식에서 한 틱에 뇌를 기다리는 한도 */
#define WAIT_GIVE_UPS 6        /* 한 판에서 한도를 이만큼 넘겼으면, 뇌가 다시 움직이기 전에는 기다리지 않는다 */
#define WAIT_STEP_MS 20        /* 그동안 이 간격으로 깨어 다시 본다 */

static float stim_now[N];      /* 이번 틱에 스테이지가 준 자극 */
static float rate[N];          /* 스테이지가 읽는 반응(틱 동안 고정) */
static float next_rate[N];     /* 다음 틱에 보여 줄 반응 */
static int   in_tick;
static int   stim_ticks, fired_ticks, stim_this_tick, fired_this_tick;

static float clamp01(float v) { return v < 0.0f ? 0.0f : (v > 1.0f ? 1.0f : v); }

/* ---- 뇌 프로세스에 붙었을 때 --------------------------------------------------- */
#define HIST_N 7
static const double hist_edge_ms[HIST_N - 1] = { 1.0, 5.0, 10.0, 20.0, 35.0, 50.0 };

static brain_shm_t *shm;                 /* NULL이면 같은 프로세스 안의 모델을 쓴다 */
static int  shm_checked, shm_wait;
static char shm_name[64];
static brain_shm_game_t  game;           /* 내가 올린 게임 쪽 칸 */
static brain_shm_brain_t seen;           /* 마지막으로 제대로 읽은 뇌 쪽 칸 */
static brain_map_t shm_map;
static struct {
    int      late;                       /* 뇌가 그 틱을 못 마쳐 직전 값을 쓴 틱 수 */
    int      timeouts;                   /* 기다리다 한도를 넘긴 횟수 */
    uint32_t timeout_beat;               /* 마지막으로 넘겼을 때의 심장 박동 수 */
    uint32_t sampled_tick;
    int      samples, hist[HIST_N];
    double   wait_sum, wait_max, took_sum, took_max, calc_sum, calc_max;     /* ms */
} ls;

/* 판이 끝날 때 한 줄로 남긴다. 스테이지의 표준 오류는 build/logs/stage_이름.log로 간다. */
static void shm_report(void)
{
    if (!shm || game.game_no == 0) return;
    int n = ls.samples > 0 ? ls.samples : 1;
    fprintf(stderr, "BRAIN shm=%s mode=%s game=%u ticks=%u stim_ticks=%d fired_ticks=%d late=%d skipped=%u "
            "restarts=%u wait_timeouts=%d wait_ms_sum=%.2f wait_ms_max=%.2f took_ms_avg=%.3f took_ms_max=%.3f "
            "calc_ms_avg=%.3f calc_ms_max=%.3f took_hist=%d/%d/%d/%d/%d/%d/%d\n",
            shm_name, shm_wait ? "wait" : "realtime", game.game_no, game.tick, stim_ticks, fired_ticks,
            ls.late, seen.game_no == game.game_no ? seen.skipped : 0, brain_shm_load(&shm->restarts),
            ls.timeouts, ls.wait_sum, ls.wait_max, ls.took_sum / n, ls.took_max, ls.calc_sum / n, ls.calc_max,
            ls.hist[0], ls.hist[1], ls.hist[2], ls.hist[3], ls.hist[4], ls.hist[5], ls.hist[6]);
    game.game_no = 0;
}

static brain_shm_t *shm_get(void)
{
    if (shm_checked) return shm;
    shm_checked = 1;
    const char *name = getenv(BRAIN_SHM_ENV);
    if (!name || !name[0]) return NULL;
    shm = brain_shm_attach(name);
    if (!shm) {
        fprintf(stderr, "뇌 창구: 공유 메모리 %s 에 붙지 못했습니다. 같은 프로세스 안의 뇌를 씁니다\n", name);
        return NULL;
    }
    const char *w = getenv(BRAIN_SHM_WAIT_ENV);
    shm_wait = (w && w[0] == '1');
    snprintf(shm_name, sizeof shm_name, "%s", name);
    atexit(shm_report);
    return shm;
}

static void shm_reset(uint32_t seed)
{
    /* 게임 쪽 칸을 쓰는 것은 스테이지뿐이므로 앞 판의 번호는 그냥 읽는다 */
    uint32_t no = shm->game.game_no + 1u;
    memset(&game, 0, sizeof game);
    game.game_no = no ? no : 1u;
    game.seed = seed;
    game.sent_ns = brain_shm_now_ns();
    memset(&seen, 0, sizeof seen);
    memset(&ls, 0, sizeof ls);
    /* 뇌가 답하기 전에 쓸 값. 모델이 판을 시작할 때 내는 값과 같다 */
    memset(next_rate, 0, sizeof next_rate);
    next_rate[BR_FORWARD] = 1.0f;
    brain_shm_put_game(shm, &game);
    brain_shm_post(&shm->wake);
}

/* 틱 직전: 뇌가 내놓은 것을 가져온다. 지금까지 올린 틱을 다 마쳤으면 제때 온 것이다. */
static void shm_fetch(void)
{
    static brain_shm_brain_t b;
    int64_t t0 = brain_shm_now_ns();
    int on_time = 0;

    for (;;) {
        if (brain_shm_get_brain(shm, &b) && b.game_no == game.game_no) {
            /* 다시 뜬 뇌는 0틱부터 시작한다. 내가 가진 것보다 옛 답이면 직전 값을 그대로 둔다 */
            if (seen.game_no != game.game_no || b.done_tick >= seen.done_tick) seen = b;
            if (b.done_tick == game.tick) { on_time = 1; break; }
        }
        if (!shm_wait) break;
        uint32_t beat = brain_shm_load(&shm->heartbeat);
        /* 뇌가 죽었거나 멈췄으면 서버가 다시 띄울 때까지 몇 틱은 기다려 준다. 그래도 안 오면
         * 판 전체가 느려지지 않게, 심장 박동이 다시 움직일 때까지는 기다리지 않는다 */
        if (ls.timeouts >= WAIT_GIVE_UPS && beat == ls.timeout_beat) break;
        if (brain_shm_now_ns() - t0 >= (int64_t)WAIT_MS * 1000000LL) {
            ls.timeouts++;
            ls.timeout_beat = beat;
            break;
        }
        brain_shm_wait(&shm->done, WAIT_STEP_MS);
    }

    double waited = (double)(brain_shm_now_ns() - t0) / 1e6;
    ls.wait_sum += waited;
    if (waited > ls.wait_max) ls.wait_max = waited;
    if (!on_time) ls.late++;

    if (seen.game_no != game.game_no) return;                    /* 이 판의 답이 아직 없다. 직전 값을 둔다 */
    memcpy(next_rate, seen.rate, sizeof next_rate);
    if (seen.done_tick > 0 && seen.done_tick != ls.sampled_tick) {
        double took = seen.took_us / 1000.0, calc = seen.calc_us / 1000.0;
        int k = 0;
        while (k < HIST_N - 1 && took >= hist_edge_ms[k]) k++;
        ls.hist[k]++;
        ls.samples++;
        ls.took_sum += took;
        ls.calc_sum += calc;
        if (took > ls.took_max) ls.took_max = took;
        if (calc > ls.calc_max) ls.calc_max = calc;
        ls.sampled_tick = seen.done_tick;
    }
}

/* 틱 끝: 이번 틱의 자극을 올리고 뇌를 깨운다. */
static void shm_publish(void)
{
    game.tick++;
    memcpy(game.stim, stim_now, sizeof game.stim);
    game.sent_ns = brain_shm_now_ns();
    brain_shm_put_game(shm, &game);
    brain_shm_post(&shm->wake);
}

/* ---- 실행기가 부르는 함수 ------------------------------------------------------ */
int brain_host_open_real(const char *dir)
{
    if (shm_get()) return 0;                     /* 모델은 뇌 프로세스가 연다 */
    return brain_model_open_real(dir);
}

void brain_host_reset(uint32_t seed, brain_mode_t m)
{
    shm_report();                                /* 같은 프로세스에서 판을 또 시작하면 앞 판을 남긴다 */
    memset(stim_now, 0, sizeof stim_now);
    memset(rate, 0, sizeof rate);
    in_tick = 0;
    stim_ticks = fired_ticks = 0;
    if (shm_get()) shm_reset(seed);              /* 어떤 모델인지는 뇌 프로세스가 정한다 */
    else brain_model_reset(seed, m, next_rate);
}

void brain_host_begin_tick(void)
{
    if (shm) shm_fetch();
    memcpy(rate, next_rate, sizeof rate);
    memset(stim_now, 0, sizeof stim_now);
    stim_this_tick = fired_this_tick = 0;
    in_tick = 1;
}

void brain_host_end_tick(void)
{
    in_tick = 0;
    stim_ticks += stim_this_tick;
    fired_ticks += fired_this_tick;
    if (shm) shm_publish();
    else brain_model_tick(stim_now, next_rate);
}

int brain_host_stim_ticks(void)  { return stim_ticks; }
int brain_host_fired_ticks(void) { return fired_ticks; }

void brain_host_peek(const float **stim, const float **r)
{
    *stim = stim_now;          /* 다음 틱이 시작될 때까지는 방금 틱의 자극이 남아 있다 */
    *r = next_rate;            /* 뇌 프로세스일 때는 이번 틱 직전에 가져온 값이다 */
}

const brain_map_t *brain_host_map(void)
{
    if (!shm) return brain_model_map();
    if (seen.game_no != game.game_no || !seen.has_map) return NULL;
    shm_map = (brain_map_t){ BRAIN_MAP_W, BRAIN_MAP_H, seen.density, seen.heat, seen.mark };
    return &shm_map;
}

/* ---- 스테이지가 부르는 함수 (brain.h) ------------------------------------------- */
static int valid_group(brain_group_t g) { return (int)g >= 0 && (int)g < N; }

void brain_stimulate(brain_group_t in, float level)
{
    if (!in_tick) {
        log_warn_once("brain-outside-tick", "brain_*는 stage_tick 안에서만 부릅니다");
        return;
    }
    if (!valid_group(in)) return;
    stim_now[in] = clamp01(level);
    if (level > 0.0f) stim_this_tick = 1;
}

float brain_rate(brain_group_t out)
{
    if (!in_tick)
        log_warn_once("brain-outside-tick", "brain_*는 stage_tick 안에서만 부릅니다");
    return valid_group(out) ? rate[out] : 0.0f;
}

int brain_fired(brain_group_t out)
{
    int fired = brain_rate(out) >= FIRE_LEVEL;
    if (fired) fired_this_tick = 1;
    return fired;
}
