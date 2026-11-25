/* 뇌 시뮬레이터 점검 도구. 게임 없이 자극만 넣어 보고 반응과 계산 시간을 표로 보여 준다.
 *   build/dev/brainprobe [data 폴더]
 * 볼 것: 자극이 없으면 조용한가, 자극이 셀수록 반응이 커지는가, 좌우가 구분되는가,
 *        쓴맛이 먹기를 줄이는가, 게임의 한 틱(50ms)을 계산하는 데 얼마나 걸리는가. */
#include "brain/lif.h"

#include <stdio.h>
#include <string.h>
#include <time.h>

#define TICK_STEPS (50 * LIF_STEPS_PER_MS)

typedef struct { const char *group; float hz; } stim_t;

static const char *const OUTS[] = { "BR_ESCAPE", "BR_TURN_L", "BR_TURN_R", "BR_BACKWARD", "BR_FEED" };
#define N_OUTS ((int)(sizeof OUTS / sizeof OUTS[0]))

static double now_ms(void)
{
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return (double)t.tv_sec * 1000.0 + (double)t.tv_nsec / 1e6;
}

/* 한 경우를 ms 동안 돌리고 한 줄을 찍는다. */
static void run_case(lif_t *l, const char *label, int ms, const stim_t *stims, int n_stims)
{
    lif_reset(l, 7);
    lif_stim_clear(l);
    for (int i = 0; i < n_stims; i++) {
        const uint32_t *idx;
        int n = lif_group(l, stims[i].group, &idx);
        lif_stim_add(l, idx, n, stims[i].hz);
    }

    double worst = 0.0, total = 0.0;
    uint32_t most_active = 0;
    int ticks = ms / 50;
    for (int t = 0; t < ticks; t++) {
        double t0 = now_ms();
        lif_run(l, TICK_STEPS);
        double dt = now_ms() - t0;
        total += dt;
        if (dt > worst) worst = dt;
        if (lif_active(l) > most_active) most_active = lif_active(l);
    }

    printf("%-26s", label);
    for (int o = 0; o < N_OUTS; o++) {
        const uint32_t *idx;
        int n = lif_group(l, OUTS[o], &idx);
        double spikes = 0.0;
        for (int i = 0; i < n; i++) spikes += lif_count(l, idx[i]);
        printf(" %7.1f", n > 0 ? spikes / n / (ms / 1000.0) : 0.0);       /* 뉴런 하나의 초당 발화 수 */
    }
    printf(" | %8llu %7u | %6.2f %6.2f\n", (unsigned long long)lif_total_spikes(l), most_active,
           total / ticks, worst);
}

/* 그림용 기록(--dump=폴더): 행동마다 그 자극을 0.5초 넣고, 그동안 발화한 뉴런을 파일에 적는다.
 * 첫 줄은 자극을 넣은 집단, 둘째 줄은 반응을 읽는 집단, 그 뒤는 "뉴런 번호 발화 수"다. tools/brain_fig.py가 그림으로 만든다 */
static int dump_all(lif_t *l, const char *out_dir)
{
    static const struct { const char *name; const char *in[2]; float hz; const char *out[2]; } C[] = {
        { "escape",   { "BR_LOOM_L", NULL },          150.0f, { "BR_ESCAPE", NULL } },
        { "walk",     { "BR_OBJECT_L", NULL },        150.0f, { "BR_WALK_L", NULL } },
        { "steer",    { "BR_OBJECT_L", NULL },        150.0f, { "BR_STEER_L", NULL } },
        { "backward", { "BR_LOOM_L", "BR_LOOM_R" },   150.0f, { "BR_BACKWARD", NULL } },
        { "feed",     { "BR_SUGAR", NULL },           120.0f, { "BR_FEED", NULL } },
        { "airturn",  { "BR_LOOM_L", NULL },          150.0f, { "BR_AIRTURN_L", "BR_AIRTURN_R" } },
        { "land",     { "BR_SURFACE_L", "BR_SURFACE_R" }, 75.0f, { "BR_LAND", NULL } },
    };
    for (size_t c = 0; c < sizeof C / sizeof C[0]; c++) {
        char path[600];
        const uint32_t *idx;
        snprintf(path, sizeof path, "%s/%s.txt", out_dir, C[c].name);
        FILE *f = fopen(path, "w");
        if (!f) { perror(path); return 1; }
        lif_reset(l, 7);
        lif_stim_clear(l);
        fprintf(f, "in");
        for (int k = 0; k < 2 && C[c].in[k]; k++) {
            int n = lif_group(l, C[c].in[k], &idx);
            lif_stim_add(l, idx, n, C[c].hz);
            fprintf(f, " %s", C[c].in[k]);
        }
        fprintf(f, "\nout");
        for (int k = 0; k < 2 && C[c].out[k]; k++) fprintf(f, " %s", C[c].out[k]);
        fprintf(f, "\n");
        lif_run(l, 500 * LIF_STEPS_PER_MS);
        uint32_t fired = 0;
        for (uint32_t i = 0; i < lif_neurons(l); i++) {
            uint32_t n = lif_count(l, i);
            if (n > 0) { fprintf(f, "%u %u\n", i, n); fired++; }
        }
        fclose(f);
        printf("%-9s 발화한 뉴런 %u개\n", C[c].name, fired);
    }
    return 0;
}

int main(int argc, char **argv)
{
    char bin[512], groups[512];
    const char *dir = argc > 1 ? argv[1] : "data";
    snprintf(bin, sizeof bin, "%s/brain.bin", dir);
    snprintf(groups, sizeof groups, "%s/brain_groups.txt", dir);
    lif_t *l = lif_open(bin, groups);
    if (!l) return 1;
    if (argc > 2 && strncmp(argv[2], "--dump=", 7) == 0) return dump_all(l, argv[2] + 7);

    printf("뉴런 %u개\n", lif_neurons(l));
    printf("%-26s %7s %7s %7s %7s %7s | %8s %7s | %6s %6s\n", "자극(초당 발화 수)",
           "도주", "왼돌기", "오른돌기", "후진", "먹기", "총 발화", "계산뉴런", "틱 ms", "최대");

#define CASE(label, ms, ...) do { const stim_t s[] = { __VA_ARGS__ }; \
        run_case(l, label, ms, s, (int)(sizeof s / sizeof s[0])); } while (0)

    CASE("없음", 400, { "none", 0 });
    CASE("왼쪽 다가옴 20", 400, { "BR_LOOM_L", 20 });
    CASE("왼쪽 다가옴 50", 400, { "BR_LOOM_L", 50 });
    CASE("왼쪽 다가옴 100", 400, { "BR_LOOM_L", 100 });
    CASE("왼쪽 다가옴 150", 400, { "BR_LOOM_L", 150 });
    CASE("오른쪽 다가옴 150", 400, { "BR_LOOM_R", 150 });
    CASE("양쪽 다가옴 150", 400, { "BR_LOOM_L", 150 }, { "BR_LOOM_R", 150 });
    CASE("단맛 20", 1000, { "BR_SUGAR", 20 });
    CASE("단맛 40", 1000, { "BR_SUGAR", 40 });
    CASE("단맛 80", 1000, { "BR_SUGAR", 80 });
    CASE("단맛 120", 1000, { "BR_SUGAR", 120 });
    CASE("단맛 160", 1000, { "BR_SUGAR", 160 });
    CASE("단맛 120 + 쓴맛 40", 1000, { "BR_SUGAR", 120 }, { "BR_BITTER", 40 });
    CASE("단맛 120 + 쓴맛 80", 1000, { "BR_SUGAR", 120 }, { "BR_BITTER", 80 });
    CASE("단맛 120 + 쓴맛 160", 1000, { "BR_SUGAR", 120 }, { "BR_BITTER", 160 });
    CASE("쓴맛 120", 1000, { "BR_BITTER", 120 });
    /* 짠맛은 60까지만 넣는다. 게임이 넣는 최대가 60이고, 90 이상에서는 뇌 전체가 폭주한다. */
    CASE("단맛 80 + 짠맛 30", 1000, { "BR_SUGAR", 80 }, { "BR_SALT", 30 });
    CASE("단맛 80 + 짠맛 60", 1000, { "BR_SUGAR", 80 }, { "BR_SALT", 60 });
    CASE("단맛 120 + 짠맛 60", 1000, { "BR_SUGAR", 120 }, { "BR_SALT", 60 });
    CASE("짠맛 60", 1000, { "BR_SALT", 60 });
    CASE("물 120", 1000, { "BR_WATER", 120 });
    CASE("단맛 120 + 물 160", 1000, { "BR_SUGAR", 120 }, { "BR_WATER", 160 });
    CASE("전부 최대", 1000, { "BR_LOOM_L", 150 }, { "BR_LOOM_R", 150 }, { "BR_SUGAR", 160 },
         { "BR_BITTER", 160 }, { "BR_SALT", 60 }, { "BR_WATER", 160 });

    lif_close(l);
    return 0;
}
