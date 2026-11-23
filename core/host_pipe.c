/* 파이프 앞단: 서버가 스테이지를 띄울 때 쓴다.
 *
 * 서버와는 물려받은 fd 두 개로 메시지를 주고받는다(규격은 protocol.h).
 *   뜨자마자        스테이지 -> 서버  INFO (제목, 규칙, 틱 수)
 *   틱마다          서버 -> 스테이지  INPUT 여러 개, 그리고 TICK 하나
 *                   스테이지 -> 서버  FRAME 하나
 *   마지막 틱 뒤    스테이지 -> 서버  RESULT
 * 한 번에 하나씩 주고받으므로 여기에는 타이머도 시그널도 없다.
 * 서버가 사라지면 읽기가 끝나므로 조용히 나간다. */
#include "core/host.h"
#include "core/frame.h"
#include "core/msg.h"
#include "ui/ui_internal.h"
#include "brain.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define OUT_CAP (128 * 1024)

typedef struct {
    int rfd, wfd;
    const host_opts_t *o;
    uint8_t *out;
} pipe_t;

static int32_t rounded(float v) { return (int32_t)lroundf(v); }

static int pipe_inputs(host_frontend_t *fe, int tick, host_input_t *out, int max)
{
    pipe_t *p = fe->self;
    int n = 0;
    (void)tick;
    for (;;) {
        msg_hdr_t h;
        input_msg_t in;
        uint8_t body[64];
        if (msg_recv(p->rfd, &h, body, sizeof body) != 1) exit(0);     /* 서버가 닫았다 */
        if (h.type == MSG_TICK) return n;
        if (h.type != MSG_INPUT || h.len != sizeof in || n >= max) continue;
        memcpy(&in, body, sizeof in);
        if (in.kind == 2)
            out[n++] = (host_input_t){ .player = in.player, .is_pointer = 1, .x = in.x, .y = in.y,
                                       .pressed = (in.flags & 1) ? 1 : 0 };
        else
            out[n++] = (host_input_t){ .player = in.player, .key = in.code };
    }
}

static void pipe_frame(host_frontend_t *fe, int tick, const canvas_t *fly,
                       canvas_t *const panes[], const stage_result_t *res)
{
    pipe_t *p = fe->self;
    const float *stim, *rate;
    frame_head_t head;
    uint8_t flags;

    memset(&head, 0, sizeof head);
    head.tick = (uint16_t)tick;
    head.n_ticks = (uint16_t)p->o->n_ticks;
    head.n_panes = (uint8_t)p->o->n_players;
    for (int i = 0; i < p->o->n_players && i < PROTO_PANES; i++) head.score[i] = rounded(res->score[i]);
    head.fly_score = rounded(res->fly_score);
    brain_host_peek(&stim, &rate);
    for (int g = 0; g < BRAIN_GROUP_COUNT && g < PROTO_GROUPS; g++) {
        head.stim[g] = (uint8_t)(stim[g] * 255.0f);
        head.rate[g] = (uint8_t)(rate[g] * 255.0f);
    }

    size_t len = frame_pack(p->out, OUT_CAP, &head, fly, panes, brain_host_map(), &flags);
    if (len == 0 || msg_send(p->wfd, MSG_FRAME, flags, 0, p->out, (uint32_t)len) != 0) exit(0);
}

int host_pipe_main(const host_opts_t *o, int rfd, int wfd)
{
    pipe_t p = { rfd, wfd, o, malloc(OUT_CAP) };
    const stage_info_t *si = stage_info();
    info_msg_t info;
    memset(&info, 0, sizeof info);
    info.n_ticks = (uint16_t)o->n_ticks;
    utf8_copy(info.title, sizeof info.title, si->title);          /* 넘치면 글자 경계에서 자른다 */
    utf8_copy(info.rule, sizeof info.rule, si->rule);
    utf8_copy(info.controls, sizeof info.controls, si->controls);
    if (msg_send(wfd, MSG_INFO, 0, 0, &info, sizeof info) != 0) return 0;

    host_frontend_t fe = { pipe_inputs, pipe_frame, &p };
    stage_result_t res;
    host_play(o, &fe, &res);

    result_msg_t rm;
    memset(&rm, 0, sizeof rm);
    for (int i = 0; i < o->n_players && i < PROTO_PANES; i++) rm.score[i] = rounded(res.score[i]);
    rm.fly_score = rounded(res.fly_score);
    msg_send(wfd, MSG_RESULT, 0, 0, &rm, sizeof rm);
    free(p.out);
    return 0;
}
