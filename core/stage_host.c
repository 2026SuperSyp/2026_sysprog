/* 스테이지 실행기. 스테이지의 함수들을 부르는 루프는 여기 하나뿐이다. */
#include "core/host.h"
#include "core/cfg.h"
#include "core/log.h"
#include "ui/ui_internal.h"

#include <stdio.h>
#include <string.h>

#define MAX_INPUTS 64

/* 마우스를 쓰지 않는 스테이지는 stage_pointer를 만들지 않아도 된다. 그때는 이것이 쓰인다. */
__attribute__((weak)) void stage_pointer(int player, int x, int y, int pressed)
{
    (void)player; (void)x; (void)y; (void)pressed;
}

static int fly_mismatch;
int host_fly_mismatch(void) { return fly_mismatch; }

static int clampi(int v, int lo, int hi) { return v < lo ? lo : (v > hi ? hi : v); }

int host_play(const host_opts_t *o, host_frontend_t *fe, stage_result_t *final)
{
    static stage_ctx_t ctx;            /* 스테이지가 포인터를 들고 있어도 되게 static으로 둔다 */
    int n = clampi(o->n_players, 1, STAGE_MAX_PLAYERS);
    char path[300];

    snprintf(path, sizeof path, "%s/config.ini", o->stage_dir);
    cfg_load(path);
    ui_set_stage_dir(o->stage_dir);
    brain_host_reset(o->seed, o->brain);
    fx_reset();
    fly_mismatch = 0;

    canvas_t *fly = canvas_new(PANE_W, PANE_H);
    canvas_t *fly_check = canvas_new(PANE_W, PANE_H);
    canvas_t *panes[STAGE_MAX_PLAYERS] = {0};
    for (int p = 0; p < n; p++) panes[p] = canvas_new(PANE_W, PANE_H);

    ctx.n_players = n;
    ctx.seed = o->seed;
    ctx.n_ticks = o->n_ticks;
    ctx.duration = (float)(o->n_ticks * STAGE_TICK_DT);
    ctx.speed = 1.0f;
    stage_start(&ctx);

    stage_result_t res;
    for (int tick = 0; tick < o->n_ticks; tick++) {
        /* 1. 입력. 같은 참가자의 같은 키는 한 틱에 한 번만 넘긴다. */
        host_input_t in[MAX_INPUTS];
        int n_in = fe->inputs ? fe->inputs(fe, tick, in, MAX_INPUTS) : 0;
        unsigned seen[STAGE_MAX_PLAYERS] = {0};
        double t = tick * STAGE_TICK_DT;
        for (int i = 0; i < n_in; i++) {
            int p = in[i].player;
            if (p < 0 || p >= n) continue;
            if (in[i].is_pointer) {
                stage_pointer(p, clampi(in[i].x, 0, PANE_W - 1), clampi(in[i].y, 0, PANE_H - 1),
                              in[i].pressed ? 1 : 0);
            } else if (in[i].key > SK_NONE && in[i].key <= SK_9) {
                if (seen[p] & (1u << in[i].key)) continue;
                seen[p] |= 1u << in[i].key;
                stage_input(p, in[i].key, t);
            }
        }

        /* 2~4. 뇌 값을 고정하고, 한 틱을 돌리고, 자극을 확정해 뇌를 전진시킨다. */
        brain_host_begin_tick();
        stage_tick(STAGE_TICK_DT);
        brain_host_end_tick();

        /* 5~6. 점수를 읽고 그린다. */
        memset(&res, 0, sizeof res);
        for (int d = 0; d < o->draws_per_tick; d++) {
            memset(&res, 0, sizeof res);
            stage_result(&res);
            fx_set_in_draw(1);
            for (int p = 0; p < n; p++) {
                canvas_t *f = (p == 0) ? fly : fly_check;
                ui_clear(f, C_BG);
                ui_clear(panes[p], C_BG);
                stage_draw(p, f, panes[p]);
                if (p > 0 && memcmp(fly->px, fly_check->px, (size_t)PANE_W * PANE_H) != 0) {
                    fly_mismatch = 1;
                    log_warn_once("fly-mismatch", "초파리 칸의 그림이 참가자마다 다릅니다. fly에는 모두에게 같은 그림을 그려야 합니다");
                }
            }
            fx_set_in_draw(0);
        }
        if (o->draws_per_tick > 0) {
            fx_apply(PANE_FLY, fly);
            for (int p = 0; p < n; p++) fx_apply(p, panes[p]);
        }

        /* 7. 앞단으로 넘긴다. 8. 이펙트의 남은 시간을 줄인다. */
        if (fe->frame) fe->frame(fe, tick, fly, panes, &res);
        fx_tick();
    }

    memset(final, 0, sizeof *final);
    stage_result(final);

    canvas_free(fly);
    canvas_free(fly_check);
    for (int p = 0; p < n; p++) canvas_free(panes[p]);
    return 0;
}
