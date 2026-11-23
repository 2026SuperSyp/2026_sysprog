/* brain_model.h : 뇌 모델 (코어 전용)
 *
 * 자극을 받아 반응을 내는 계산부다. 가짜 뇌 둘과 진짜 뇌를 같은 모양으로 돌린다.
 * 같은 프로세스 안에서 쓸 때는 brain_api.c가, 뇌 프로세스에서는 brain_proc.c가 부른다. */
#ifndef SSP_BRAIN_MODEL_H
#define SSP_BRAIN_MODEL_H

#include <stdint.h>
#include "brain/brain_host.h"

/* 진짜 뇌의 자료(brain.bin, brain_groups.txt)가 있는 폴더를 연다. 0이면 성공. */
int  brain_model_open_real(const char *dir);

/* 판을 시작한다. rate에 첫 틱에 보여 줄 반응을 채운다(BRAIN_GROUP_COUNT개).
 * 진짜 뇌를 골랐는데 열려 있지 않으면 빠른 가짜 뇌로 바꾼다. 실제로 쓰는 모델을 돌려준다. */
brain_mode_t brain_model_reset(uint32_t seed, brain_mode_t mode, float *rate);

/* 한 틱(50ms)을 계산한다. stim은 이번 틱의 자극, rate는 다음 틱에 보여 줄 반응이다. */
void brain_model_tick(const float *stim, float *rate);

const brain_map_t *brain_model_map(void);     /* 진짜 뇌가 아니거나 위치 자료가 없으면 NULL */

#endif
