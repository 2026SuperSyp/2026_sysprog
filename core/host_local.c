/* 터미널 앞단: make run이 쓴다.
 * 흐름: 규칙 설명 3초 -> 3, 2, 1, GO 2초 -> 게임 -> 판정 2초
 * 0번 참가자는 키보드와 마우스, 나머지는 무작위 입력이다. q로 나간다. */
#include "core/host.h"
#include "core/log.h"
#include "core/scenes.h"
#include "screen.h"
#include "ui/ui_internal.h"

#include <poll.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#define TICK_NS   50000000L
#define OUT_CAP   (1 << 20)

typedef struct {
    const host_opts_t *o;
    canvas_t *full;
    present_state_t *ps;
    char *out;
    struct timespec next;      /* 다음 틱 시각(단조 시계) */
    uint8_t inbuf[512];
    size_t in_len;
    const stage_info_t *info;
    int notice_shown;
    int watch;                 /* 오른쪽 위에 보여 줄 것: 다른 참가자 또는 WATCH_FLY. Tab으로 바꾼다 */
} local_t;

static void write_all(const char *buf, size_t n)
{
    while (n > 0) {
        ssize_t w = write(STDOUT_FILENO, buf, n);
        if (w <= 0) return;
        buf += w;
        n -= (size_t)w;
    }
}

static void quit_now(void)
{
    term_leave();
    exit(0);
}

/* 전체 화면을 터미널에 보낸다. 터미널이 작으면 안내만 보인다. */
static void show(local_t *L)
{
    int cols = 0, rows = 0;
    term_size(&cols, &rows);
    size_t n = present_encode(L->ps, L->full, cols, rows, L->out, OUT_CAP);
    if (n > 0) {
        write_all(L->out, n);
        L->notice_shown = 0;
    } else if (cols < SCREEN_W || rows < SCREEN_H / 2) {
        if (!L->notice_shown) {
            char msg[256];
            int m = snprintf(msg, sizeof msg,
                "\033[0m\033[2J\033[H지금 %d칸 %d줄입니다. 240칸 67줄 이상이 필요합니다.\r\n"
                "Ctrl 과 - 를 눌러 글자를 줄이거나 창을 키워 주세요. (q: 나가기)\r\n", cols, rows);
            write_all(msg, (size_t)m);
            L->notice_shown = 1;
        }
    }
}

/* 다음 틱 시각까지 기다리면서 그동안 들어온 바이트를 모은다. */
static void wait_tick(local_t *L)
{
    for (;;) {
        struct timespec now;
        clock_gettime(CLOCK_MONOTONIC, &now);
        long ms = (L->next.tv_sec - now.tv_sec) * 1000L + (L->next.tv_nsec - now.tv_nsec) / 1000000L;
        if (ms <= 0) break;
        struct pollfd pf = { STDIN_FILENO, POLLIN, 0 };
        if (poll(&pf, 1, (int)ms) > 0 && L->in_len < sizeof L->inbuf) {
            ssize_t r = read(STDIN_FILENO, L->inbuf + L->in_len, sizeof L->inbuf - L->in_len);
            if (r > 0) L->in_len += (size_t)r;
        }
    }
    L->next.tv_nsec += TICK_NS;
    if (L->next.tv_nsec >= 1000000000L) { L->next.tv_nsec -= 1000000000L; L->next.tv_sec++; }
}

/* 모아 둔 바이트에서 사건을 꺼낸다. 1이면 ev가 채워졌다. */
static int next_event(local_t *L, in_event_t *ev)
{
    while (L->in_len > 0) {
        size_t used = 0;
        if (!input_parse(L->inbuf, L->in_len, ev, &used)) return 0;
        memmove(L->inbuf, L->inbuf + used, L->in_len - used);
        L->in_len -= used;
        if (ev->kind == IN_KEY && (ev->code == 'q' || ev->code == 'Q' || ev->code == 3)) quit_now();
        if (ev->kind != 0) return 1;
    }
    return 0;
}

static int local_inputs(host_frontend_t *fe, int tick, host_input_t *out, int max)
{
    local_t *L = fe->self;
    int n = 0, cols = 0, rows = 0;
    in_event_t ev;

    wait_tick(L);
    term_size(&cols, &rows);
    while (n < max - 6 && next_event(L, &ev)) {
        if (ev.kind == IN_KEY && ev.code == '\t') {
            /* Tab: 오른쪽 위를 다음 사람으로 바꾼다. 마지막 사람 다음은 키보드 치는 초파리다 */
            if (L->watch == WATCH_FLY)
                L->watch = L->o->n_players > 1 ? 1 : WATCH_FLY;
            else
                L->watch = L->watch >= L->o->n_players - 1 ? WATCH_FLY : L->watch + 1;
        } else if (ev.kind == IN_KEY) {
            int key = input_stage_key(&ev);
            if (key != SK_NONE) out[n++] = (host_input_t){ .player = 0, .key = key };
        } else if (ev.kind == IN_POINTER) {
            int sx, sy, px, py;
            if (present_cell_to_screen(cols, rows, ev.x, ev.y, &sx, &sy) &&
                compose_my_pane_xy(sx, sy, &px, &py))
                out[n++] = (host_input_t){ .player = 0, .is_pointer = 1, .x = px, .y = py,
                                           .pressed = (ev.flags & IN_F_PRESSED) ? 1 : 0 };
        }
    }
    for (int p = 1; p < L->o->n_players && n + 2 <= max; p++)
        n += host_bot_inputs(L->o->seed, p, p, tick, out + n, max - n);
    return n;
}

static void local_frame(host_frontend_t *fe, int tick, const canvas_t *fly,
                        canvas_t *const panes[], const stage_result_t *res)
{
    local_t *L = fe->self;
    const float *stim, *rate;
    brain_host_peek(&stim, &rate);
    hud_t hud = {
        .title = L->info->title,
        .ticks_left = L->o->n_ticks - tick - 1,
        .round = 1,
        .my_score = (int)(res->score[0] + 0.5f),
        .fly_score = (int)(res->fly_score + 0.5f),
        .lives = 3,
        .controls = L->info->controls,
        .tick = tick,
        .watch = L->watch,
        .brain_stim = stim,
        .brain_rate = rate,
        .brain_map = brain_host_map(),
    };
    compose_stage_frame(L->full, fly, panes, L->o->n_players, 0, &hud);
    show(L);
}

/* 게임 밖의 장면(규칙 설명, 카운트다운, 판정)을 ticks만큼 보여 준다. */
static void scene(local_t *L, int ticks, void (*draw)(local_t *, int tick, void *arg), void *arg)
{
    in_event_t ev;
    for (int t = 0; t < ticks; t++) {
        wait_tick(L);
        while (next_event(L, &ev)) { }
        draw(L, t, arg);
        show(L);
    }
}

static void draw_card(local_t *L, int tick, void *arg)
{
    (void)tick; (void)arg;
    scene_card(L->full, L->info->title, L->info->rule, L->info->controls, tick);
}

static void draw_count(local_t *L, int tick, void *arg)
{
    (void)arg;
    scene_count(L->full, tick);
}

static void draw_verdict(local_t *L, int tick, void *arg)
{
    round_result_t *rr = arg;
    rr->t = tick * STAGE_TICK_DT;
    ui_clear(L->full, C_BG);
    screen_result(L->full, rr);
}

int host_local_main(const host_opts_t *o, int argc, char **argv)
{
    (void)argc; (void)argv;
    local_t L = { .o = o, .watch = o->n_players > 1 ? 1 : WATCH_FLY };
    if (term_enter() != 0) {
        fprintf(stdout, "터미널에서 실행해 주세요 (make run STAGE=이름)\n");
        return 2;
    }
    L.full = canvas_new(SCREEN_W, SCREEN_H);
    L.ps = present_new();
    L.out = malloc(OUT_CAP);
    L.info = stage_info();
    clock_gettime(CLOCK_MONOTONIC, &L.next);

    scene(&L, 60, draw_card, NULL);
    scene(&L, 40, draw_count, NULL);

    host_frontend_t fe = { local_inputs, local_frame, &L };
    stage_result_t res;
    host_play(o, &fe, &res);

    round_result_t rr = { .n = o->n_players, .fly_score = (float)(int)(res.fly_score + 0.5f),
                          .stage_name = L.info->title, .me = 0 };
    for (int p = 0; p < o->n_players && p < 8; p++) {
        snprintf(rr.name[p], sizeof rr.name[p], p == 0 ? "YOU" : "BOT%d", p);
        rr.score[p] = (float)(int)(res.score[p] + 0.5f);
        rr.passed[p] = rr.score[p] >= rr.fly_score;
        rr.lives[p] = rr.passed[p] ? 3 : 2;
        rr.next[p] = NEXT_ROUND;
    }
    scene(&L, 40, draw_verdict, &rr);

    term_leave();
    /* 결과는 로그로 보낸다. make run이 끝나면서 로그의 마지막 줄들을 보여 준다. */
    fprintf(stderr, "결과: %s  YOU %.0f  FLY %.0f  %s\n", L.info->title ? L.info->title : "",
            (double)rr.score[0], (double)rr.fly_score, rr.passed[0] ? "WIN" : "LOSE");
    canvas_free(L.full);
    present_free(L.ps);
    free(L.out);
    return 0;
}
