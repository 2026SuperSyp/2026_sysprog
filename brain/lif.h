/* lif.h : 초파리 뇌 시뮬레이터 (코어 전용)
 *
 * FlyWire v783의 실제 배선(뉴런 138,639개, 연결 약 1,509만 개) 위에서
 * Shiu 등(Nature 2024)의 수식과 상수로 뉴런의 발화를 계산한다. 코드는 직접 짠 것이다.
 * 스테이지는 이 헤더를 쓰지 않는다. include/brain.h의 세 함수만 쓴다.
 *
 * 시간의 단위는 스텝이고 한 스텝은 0.1ms다. 게임의 한 틱(50ms)은 500스텝이다. */
#ifndef SSP_LIF_H
#define SSP_LIF_H

#include <stdint.h>

#define LIF_STEPS_PER_MS 10

typedef struct lif lif_t;

/* 배선 파일(brain.bin)과 집단 파일(brain_groups.txt)을 연다. 실패하면 NULL을 돌려주고 이유를 stderr에 적는다. */
lif_t *lif_open(const char *bin_path, const char *groups_path);
void   lif_close(lif_t *l);

/* 모든 뉴런을 쉬는 상태로 되돌린다. 같은 씨앗이면 같은 결과가 나온다. */
void   lif_reset(lif_t *l, uint64_t seed);

/* 집단을 이름으로 찾는다("BR_SUGAR" 등). 뉴런 수를 돌려주고 *idx에 뉴런 번호 배열을 넣는다. 없으면 0. */
int    lif_group(const lif_t *l, const char *name, const uint32_t **idx);

/* 외부 자극. 지정한 뉴런들이 초당 hz번쯤 무작위로 발화하게 한다. lif_stim_clear로 모두 지운다. */
void   lif_stim_clear(lif_t *l);
void   lif_stim_add(lif_t *l, const uint32_t *idx, int n, float hz);

/* steps만큼 시간을 진행한다. */
void   lif_run(lif_t *l, int steps);

uint32_t lif_neurons(const lif_t *l);
uint32_t lif_count(const lif_t *l, uint32_t idx);        /* 이 뉴런이 지금까지 발화한 횟수 */
uint64_t lif_total_spikes(const lif_t *l);               /* 모든 뉴런의 발화 횟수 합 */
/* 마지막 lif_run 동안 발화한 뉴런 번호들. 여러 번 발화한 뉴런은 여러 번 들어 있다. 개수를 돌려준다. */
uint32_t lif_fired(const lif_t *l, const uint32_t **idx);
uint32_t lif_active(const lif_t *l);                     /* 지금 계산 중인(쉬지 않는) 뉴런 수 */

#endif
