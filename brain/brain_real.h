/* brain_real.h : 진짜 뇌를 게임 틱에 맞춰 돌리는 부분 (코어 전용) */
#ifndef SSP_BRAIN_REAL_H
#define SSP_BRAIN_REAL_H

#include <stdint.h>
#include "brain/brain_host.h"

/* dir 안의 brain.bin과 brain_groups.txt를 연다. 0이면 성공. */
int  brain_real_open(const char *dir);
int  brain_real_ready(void);
void brain_real_reset(uint32_t seed);

/* 한 틱을 계산한다. stim은 이번 틱의 자극, rate는 다음 틱에 보여 줄 반응이다(각각 BRAIN_GROUP_COUNT개). */
void brain_real_tick(const float *stim, float *rate);

const brain_map_t *brain_real_map(void);      /* 위치 자료(brain_map.bin)가 없으면 NULL */

#endif
