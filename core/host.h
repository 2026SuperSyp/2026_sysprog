/* host.h : 스테이지 실행기
 *
 * 스테이지의 함수들을 부르는 루프는 host_play 하나뿐이다.
 * 달라지는 것은 앞단(입력이 오는 곳, 그림이 가는 곳)이다. */
#ifndef SSP_HOST_H
#define SSP_HOST_H

#include <stdint.h>
#include "stage.h"
#include "brain/brain_host.h"

typedef struct {
    int player;                  /* 0부터 n_players-1 */
    int is_pointer;              /* 0: 키, 1: 마우스 */
    int key;                     /* 키일 때 SK_* */
    int x, y, pressed;           /* 마우스일 때. 칸 기준 좌표 */
} host_input_t;

typedef struct host_frontend {
    /* 이번 틱 전에 들어온 입력을 out에 채우고 개수를 돌려준다. */
    int  (*inputs)(struct host_frontend *fe, int tick, host_input_t *out, int max);
    /* 한 틱의 그림과 점수. panes[i]는 참가자 i의 칸이다. 이펙트는 이미 입혀져 있다. */
    void (*frame)(struct host_frontend *fe, int tick, const canvas_t *fly,
                  canvas_t *const panes[], const stage_result_t *res);
    void *self;
} host_frontend_t;

typedef struct {
    const char  *stage_dir;      /* 예: "stages/example". 설정과 그림을 여기서 찾는다 */
    uint32_t     seed;
    int          n_players;      /* 1~3 */
    int          n_ticks;        /* 한 판의 틱 수. 기본 400 */
    brain_mode_t brain;
    int          draws_per_tick; /* 틱마다 stage_draw와 stage_result를 부르는 횟수. 보통 1 */
} host_opts_t;

/* 한 판을 돈다. 0이면 정상. final에 마지막 점수를 채운다.
 * 한 틱의 순서: 입력 넘기기 -> 뇌 값 고정 -> stage_tick -> 자극 확정과 뇌 전진
 *              -> 점수 읽기 -> 그리기와 이펙트 입히기 -> 앞단으로 넘기기 -> 이펙트 시간 줄이기 */
int host_play(const host_opts_t *o, host_frontend_t *fe, stage_result_t *final);

/* 앞단 (host_bot.c, host_local.c) */
int host_bot_main(const host_opts_t *o, int solo);     /* 화면 없이 돌리고 RESULT, STATS 줄을 찍는다 */
int host_local_main(const host_opts_t *o, int argc, char **argv);   /* 터미널에서 돌린다 */

#endif
