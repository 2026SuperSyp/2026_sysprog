/* brain_host.h : 실행기가 뇌 창구를 움직일 때 쓰는 함수
 *
 * 스테이지는 include/brain.h의 세 함수만 쓴다. 이 헤더는 코어 전용이다.
 * 뇌의 시간은 게임 틱이다. 한 틱은 아래 순서로 돈다.
 *   brain_host_begin_tick()  반응 값을 고정한다
 *   stage_tick()             이 안에서만 brain_*를 부를 수 있다
 *   brain_host_end_tick()    이번 틱의 자극을 확정하고 뇌를 한 틱 전진시킨다 */
#ifndef SSP_BRAIN_HOST_H
#define SSP_BRAIN_HOST_H

#include <stdint.h>

typedef enum {
    BRAIN_FAKE_FAST,             /* 모든 반응이 다음 틱에 나온다 */
    BRAIN_FAKE_SLOW,             /* 방향 전환과 후진이 여러 틱에 걸쳐 오른다 */
    BRAIN_REAL                   /* 진짜 배선으로 계산한다. 먼저 brain_host_open_real이 성공해야 한다 */
} brain_mode_t;

/* 진짜 뇌의 자료(brain.bin, brain_groups.txt)가 있는 폴더를 연다. 0이면 성공. */
int  brain_host_open_real(const char *dir);

void brain_host_reset(uint32_t seed, brain_mode_t mode);   /* 판을 시작할 때 한 번 */
void brain_host_begin_tick(void);
void brain_host_end_tick(void);

int  brain_host_stim_ticks(void);    /* 자극이 하나라도 들어온 틱의 수 */
int  brain_host_fired_ticks(void);   /* brain_fired가 1을 돌려준 틱의 수 */

/* 화면의 뇌 표시용. 방금 틱의 자극과 지금의 반응을 넘겨준다(각각 BRAIN_GROUP_COUNT개). */
void brain_host_peek(const float **stim, const float **rate);

#endif
