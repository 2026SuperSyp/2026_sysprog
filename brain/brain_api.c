/* 뇌 창구. brain.h의 세 함수는 여기 한 곳에만 있다.
 *
 * 지금은 뒤에 가짜 뇌가 붙어 있다. 가짜 뇌는 설계서 6.3의 반응 표를 흉내 낸다.
 *  - 자극이 없으면 조용하다(BR_FORWARD만 1).
 *  - 한쪽에서 다가오면 반대쪽으로 돌려 하고, 셀수록 도주가 커진다.
 *  - 양쪽에서 다가오면 방향 전환이 꺼지고 뒤로 물러난다.
 *  - 단맛은 일정 세기를 넘어야 먹고, 쓴맛과 짠맛은 먹기를 줄인다.
 * 진짜 뇌(BRAIN_REAL)를 고르면 반응을 brain_real.c가 실제 배선으로 계산한다.
 * 어느 쪽이든 스테이지 코드는 같다. */
#include "brain.h"
#include "brain/brain_host.h"
#include "brain/brain_real.h"
#include "core/log.h"

#include <string.h>

#define N          BRAIN_GROUP_COUNT
#define FIRE_LEVEL 0.5f        /* brain_fired의 기준 */
#define SLOW_TICKS 6           /* 느린 뇌: 방향 전환과 후진을 이만큼의 틱으로 평균한다 */

static float stim_now[N];      /* 이번 틱에 스테이지가 준 자극 */
static float rate[N];          /* 스테이지가 읽는 반응(틱 동안 고정) */
static float next_rate[N];     /* 다음 틱에 보여 줄 반응 */
static float history[SLOW_TICKS][N];
static int   hist_pos;
static int   in_tick;
static brain_mode_t mode;
static uint64_t noise_state;
static int   stim_ticks, fired_ticks, stim_this_tick, fired_this_tick;

static float clamp01(float v) { return v < 0.0f ? 0.0f : (v > 1.0f ? 1.0f : v); }
static float maxf(float a, float b) { return a > b ? a : b; }
static float minf(float a, float b) { return a < b ? a : b; }

/* -0.03 ~ +0.03의 작은 흔들림. 씨앗에서 나오므로 같은 씨앗이면 같은 값이다. */
static float noise(void)
{
    noise_state ^= noise_state >> 12;
    noise_state ^= noise_state << 25;
    noise_state ^= noise_state >> 27;
    uint32_t v = (uint32_t)((noise_state * 0x2545F4914F6CDD1DULL) >> 40);   /* 24비트 */
    return ((float)v / 16777216.0f - 0.5f) * 0.06f;
}

/* 자극에서 반응의 목표값을 만든다. */
static void fake_targets(const float *s, float *t)
{
    memset(t, 0, sizeof(float) * N);
    float l = s[BR_LOOM_L], r = s[BR_LOOM_R];
    float big = maxf(l, r);

    t[BR_ESCAPE]   = clamp01(big * 1.2f);
    t[BR_TURN_R]   = l > r ? clamp01((l - r) * 1.5f) : 0.0f;   /* 왼쪽에서 다가오면 오른쪽으로 */
    t[BR_TURN_L]   = r > l ? clamp01((r - l) * 1.5f) : 0.0f;
    t[BR_BACKWARD] = minf(l, r);
    t[BR_FORWARD]  = 1.0f - big;

    float drive = s[BR_SUGAR] + 0.5f * s[BR_WATER] - s[BR_BITTER] - 0.7f * s[BR_SALT];
    t[BR_FEED] = drive < 0.3f ? 0.0f : clamp01(drive);

    float sum = 0.0f;
    for (int i = BR_SUGAR; i <= BR_REPEL_R; i++) sum += s[i];
    t[BR_AROUSAL] = clamp01(sum * 0.5f);
}

int brain_host_open_real(const char *dir) { return brain_real_open(dir); }

void brain_host_reset(uint32_t seed, brain_mode_t m)
{
    memset(stim_now, 0, sizeof stim_now);
    memset(rate, 0, sizeof rate);
    memset(next_rate, 0, sizeof next_rate);
    memset(history, 0, sizeof history);
    hist_pos = 0;
    in_tick = 0;
    mode = (m == BRAIN_REAL && !brain_real_ready()) ? BRAIN_FAKE_FAST : m;
    if (mode == BRAIN_REAL) brain_real_reset(seed);
    noise_state = ((uint64_t)seed << 1) | 1ULL;
    stim_ticks = fired_ticks = 0;
    next_rate[BR_FORWARD] = 1.0f;
}

void brain_host_begin_tick(void)
{
    memcpy(rate, next_rate, sizeof rate);
    memset(stim_now, 0, sizeof stim_now);
    stim_this_tick = fired_this_tick = 0;
    in_tick = 1;
}

void brain_host_end_tick(void)
{
    float target[N];
    in_tick = 0;
    stim_ticks += stim_this_tick;
    fired_ticks += fired_this_tick;

    if (mode == BRAIN_REAL) {
        brain_real_tick(stim_now, next_rate);
        return;
    }

    fake_targets(stim_now, target);
    memcpy(history[hist_pos], target, sizeof target);
    hist_pos = (hist_pos + 1) % SLOW_TICKS;

    for (int i = 0; i < N; i++) {
        float v = target[i];
        int averaged = (i == BR_TURN_L || i == BR_TURN_R || i == BR_BACKWARD);
        if (mode == BRAIN_FAKE_SLOW && averaged) {
            v = 0.0f;
            for (int k = 0; k < SLOW_TICKS; k++) v += history[k][i];
            v /= SLOW_TICKS;
        }
        /* 0인 반응은 0으로 둔다. 자극이 없으면 조용해야 한다. */
        next_rate[i] = (v > 0.0f && i != BR_FORWARD) ? clamp01(v + noise()) : v;
    }
}

int brain_host_stim_ticks(void)  { return stim_ticks; }
int brain_host_fired_ticks(void) { return fired_ticks; }

void brain_host_peek(const float **stim, const float **r)
{
    *stim = stim_now;          /* 다음 틱이 시작될 때까지는 방금 틱의 자극이 남아 있다 */
    *r = next_rate;
}

const brain_map_t *brain_host_map(void) { return mode == BRAIN_REAL ? brain_real_map() : NULL; }

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
