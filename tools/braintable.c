/* 뇌 반응 표. 스테이지가 쓰는 함수(brain_stimulate, brain_rate)로 자극을 주고 반응을 표로 찍는다.
 *   build/dev/braintable [--brain=real]
 *
 * 스테이지를 만들 때 "자극을 얼마로 주면 초파리가 반응하는가"를 보는 데 쓴다. 가짜 뇌와 진짜 뇌는 같은 자극에
 * 다르게 반응하므로(진짜 뇌가 대체로 더 예민하다) 수치는 진짜 뇌에서도 확인한다.
 * 자극을 1.5초(30틱) 동안 계속 주고, 마지막 0.5초의 평균 반응과, 반응이 처음 0.5(brain_fired의 기준)를 넘은 틱을 찍는다. */
#include "brain.h"
#include "brain/brain_host.h"

#include <stdio.h>
#include <string.h>

#define TICKS 30
#define TAIL  10

static const struct { const char *name; brain_group_t a, b; float b_level; } rows[] = {
    { "단맛",                  BR_SUGAR,   BRAIN_GROUP_COUNT, 0.0f },
    { "쓴맛",                  BR_BITTER,  BRAIN_GROUP_COUNT, 0.0f },
    { "짠맛",                  BR_SALT,    BRAIN_GROUP_COUNT, 0.0f },
    { "물",                    BR_WATER,   BRAIN_GROUP_COUNT, 0.0f },
    { "단맛 1.0 + 쓴맛",       BR_BITTER,  BR_SUGAR,          1.0f },
    { "단맛 1.0 + 짠맛",       BR_SALT,    BR_SUGAR,          1.0f },
    { "단맛 0.5 + 쓴맛",       BR_BITTER,  BR_SUGAR,          0.5f },
    { "단맛 0.5 + 짠맛",       BR_SALT,    BR_SUGAR,          0.5f },
    { "왼쪽에서 다가옴",       BR_LOOM_L,  BRAIN_GROUP_COUNT, 0.0f },
    { "양쪽에서 다가옴(각각)", BR_LOOM_L,  BR_LOOM_R,         -1.0f },      /* -1: 같은 세기 */
    { "왼쪽 냄새",             BR_ODOR_L,  BRAIN_GROUP_COUNT, 0.0f },
    { "왼쪽 싫은 냄새",        BR_REPEL_L, BRAIN_GROUP_COUNT, 0.0f },
};
static const brain_group_t outs[] = { BR_FEED, BR_ESCAPE, BR_TURN_L, BR_TURN_R, BR_FORWARD, BR_BACKWARD };
#define N_OUT ((int)(sizeof outs / sizeof outs[0]))

static void table(brain_mode_t mode, const char *title)
{
    static const float levels[] = { 0.1f, 0.2f, 0.3f, 0.5f, 0.7f, 1.0f };
    printf("\n== %s ==\n", title);
    printf("%-24s 세기 |  먹기 달아남 왼돌기 오른돌기 앞으로  뒤로 | 0.5를 처음 넘은 틱(먹기/달아남)\n", "자극");
    for (size_t r = 0; r < sizeof rows / sizeof rows[0]; r++) {
        for (size_t k = 0; k < sizeof levels / sizeof levels[0]; k++) {
            float sum[N_OUT] = { 0 };
            int first[N_OUT];
            for (int o = 0; o < N_OUT; o++) first[o] = -1;
            brain_host_reset(1, mode);
            for (int t = 0; t < TICKS; t++) {
                brain_host_begin_tick();
                brain_stimulate(rows[r].a, levels[k]);
                if (rows[r].b != BRAIN_GROUP_COUNT) brain_stimulate(rows[r].b, rows[r].b_level < 0.0f ? levels[k] : rows[r].b_level);
                for (int o = 0; o < N_OUT; o++) {
                    float v = brain_rate(outs[o]);
                    if (t >= TICKS - TAIL) sum[o] += v;
                    if (first[o] < 0 && v >= 0.5f) first[o] = t;
                }
                brain_host_end_tick();
            }
            printf("%-24s  %.1f |", k == 0 ? rows[r].name : "", levels[k]);
            for (int o = 0; o < N_OUT; o++) printf("  %4.2f ", sum[o] / TAIL);
            printf("|");
            for (int o = 0; o < 2; o++) {
                if (first[o] < 0) printf("   -"); else printf(" %3d", first[o]);
            }
            printf("\n");
        }
    }
}

int main(int argc, char **argv)
{
    int real = argc > 1 && strcmp(argv[1], "--brain=real") == 0;
    table(BRAIN_FAKE_FAST, "가짜 뇌 (BRAIN=fast)");
    if (real) {
        if (brain_host_open_real("data") != 0) { printf("진짜 뇌의 자료(data/)가 없습니다. 먼저 make brain-data\n"); return 1; }
        table(BRAIN_REAL, "진짜 뇌 (BRAIN=real)");
    }
    return 0;
}
