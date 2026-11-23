/* 뇌 모델. 자극에서 반응을 계산한다.
 *
 * 가짜 뇌는 설계서 6.3의 반응 표를 흉내 낸다.
 *  - 자극이 없으면 조용하다(BR_FORWARD만 1).
 *  - 한쪽에서 다가오면 반대쪽으로 돌려 하고, 셀수록 도주가 커진다.
 *  - 양쪽에서 다가오면 방향 전환이 꺼지고 뒤로 물러난다.
 *  - 단맛은 일정 세기를 넘어야 먹고, 쓴맛과 짠맛은 먹기를 줄인다.
 * 진짜 뇌(BRAIN_REAL)를 고르면 반응을 brain_real.c가 실제 배선으로 계산한다.
 * 이 파일은 같은 프로세스 안의 뇌(brain_api.c)와 뇌 프로세스(brain_proc.c)가 같이 쓴다. */
#include "brain/brain_model.h"
#include "brain/brain_real.h"
#include "brain.h"

#include <string.h>

#define N          BRAIN_GROUP_COUNT
#define SLOW_TICKS 6           /* 느린 뇌: 방향 전환과 후진을 이만큼의 틱으로 평균한다 */

static float history[SLOW_TICKS][N];
static int   hist_pos;
static brain_mode_t mode;
static uint64_t noise_state;

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

int brain_model_open_real(const char *dir) { return brain_real_open(dir); }

brain_mode_t brain_model_reset(uint32_t seed, brain_mode_t m, float *rate)
{
    memset(history, 0, sizeof history);
    hist_pos = 0;
    mode = (m == BRAIN_REAL && !brain_real_ready()) ? BRAIN_FAKE_FAST : m;
    if (mode == BRAIN_REAL) brain_real_reset(seed);
    noise_state = ((uint64_t)seed << 1) | 1ULL;
    memset(rate, 0, sizeof(float) * N);
    rate[BR_FORWARD] = 1.0f;
    return mode;
}

void brain_model_tick(const float *stim, float *rate)
{
    float target[N];

    if (mode == BRAIN_REAL) {
        brain_real_tick(stim, rate);
        return;
    }

    fake_targets(stim, target);
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
        rate[i] = (v > 0.0f && i != BR_FORWARD) ? clamp01(v + noise()) : v;
    }
}

const brain_map_t *brain_model_map(void) { return mode == BRAIN_REAL ? brain_real_map() : NULL; }
