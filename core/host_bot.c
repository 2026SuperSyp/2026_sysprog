/* 봇 앞단: 화면 없이 무작위 입력으로 한 판을 돌리고 결과를 한 줄씩 찍는다.
 *   RESULT fly=<점수> p0=<점수> ...
 *   STATS warnings=<n> stim_ticks=<n> fired_ticks=<n> fx_calls=<n> fly_mismatch=<0|1> */
#include "core/host.h"
#include "core/log.h"
#include "ui/ui_internal.h"

#include <math.h>
#include <stdio.h>

int host_bot_inputs(uint32_t seed, int stream, int player, int tick, host_input_t *out, int max)
{
    static const int keys[] = { SK_LEFT, SK_RIGHT, SK_UP, SK_DOWN, SK_ACT, SK_SUB, SK_1, SK_2, SK_3 };
    const int n_keys = (int)(sizeof keys / sizeof keys[0]);
    stage_rng_t r;
    int n = 0;
    if (max < 2) return 0;

    if (stream == 2) {
        /* 같은 키를 16틱 동안 계속 누른다. */
        stage_rng_seed(&r, seed * 2654435761u + (uint32_t)stream * 40503u + (uint32_t)(tick / 16) * 97u);
        out[n++] = (host_input_t){ .player = player, .key = keys[stage_rng_range(&r, 0, 3)] };
    } else {
        /* 톡톡 누른다: 틱마다 20% 확률. */
        stage_rng_seed(&r, seed * 2654435761u + (uint32_t)stream * 40503u + (uint32_t)tick * 31u + 7u);
        if (stage_rng_float(&r) < 0.2f)
            out[n++] = (host_input_t){ .player = player, .key = keys[stage_rng_range(&r, 0, n_keys - 1)] };
    }

    /* 가끔 마우스도 움직인다. 경계 값도 섞는다. */
    stage_rng_seed(&r, seed * 40503u + (uint32_t)stream * 7919u + (uint32_t)tick * 131u + 3u);
    if (stage_rng_float(&r) < 0.1f) {
        int edge = stage_rng_range(&r, 0, 9);
        int x = edge == 0 ? 0 : (edge == 1 ? PANE_W - 1 : stage_rng_range(&r, 0, PANE_W - 1));
        int y = edge == 2 ? 0 : (edge == 3 ? PANE_H - 1 : stage_rng_range(&r, 0, PANE_H - 1));
        out[n++] = (host_input_t){ .player = player, .is_pointer = 1, .x = x, .y = y,
                                   .pressed = stage_rng_range(&r, 0, 1) };
    }
    return n;
}

typedef struct { const host_opts_t *o; int solo; } bot_t;

static int bot_inputs(host_frontend_t *fe, int tick, host_input_t *out, int max)
{
    bot_t *b = fe->self;
    int n = 0;
    for (int p = 0; p < b->o->n_players && n + 2 <= max; p++) {
        int stream = (b->solo >= 0) ? b->solo : p;
        n += host_bot_inputs(b->o->seed, stream, p, tick, out + n, max - n);
    }
    return n;
}

int host_bot_main(const host_opts_t *o, int solo)
{
    bot_t b = { o, solo };
    host_frontend_t fe = { bot_inputs, NULL, &b };
    stage_result_t res;
    int bad = 0;

    host_play(o, &fe, &res);

    printf("RESULT fly=%.3f", (double)res.fly_score);
    if (!isfinite(res.fly_score) || res.fly_score < 0.0f) bad = 1;
    for (int p = 0; p < o->n_players; p++) {
        printf(" p%d=%.3f", p, (double)res.score[p]);
        if (!isfinite(res.score[p]) || res.score[p] < 0.0f) bad = 1;
    }
    printf("\n");
    printf("STATS warnings=%d stim_ticks=%d fired_ticks=%d fx_calls=%d fly_mismatch=%d\n",
           log_warning_count(), brain_host_stim_ticks(), brain_host_fired_ticks(),
           fx_call_count(), host_fly_mismatch());
    if (bad) {
        printf("FAIL 점수가 0 이상의 유한한 숫자가 아닙니다\n");
        return 3;
    }
    return 0;
}
