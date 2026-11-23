/* brain_sup.h : 뇌 프로세스 관리 (서버 전용)
 *
 * 서버가 공유 메모리를 만들고 뇌 프로세스(서버 실행 파일 옆의 brain_proc)를 띄운다. 죽거나 멈추면 다시 띄우고,
 * 서버가 끝날 때 뇌를 거두고 공유 메모리를 지운다.
 * 판은 멈추지 않는다: fast가 아닌 모델이 시작할 때 준비되지 못하거나, 판 도중에 3번 연달아 제대로
 * 뜨지 못하면 규칙으로 움직이는 뇌(fast)로 바꾼다. strict이면 바꾸지 않는다(시작에 실패하면 서버도 서지 않는다).
 * 아래 함수는 모두 게임 마스터 스레드만 부른다. brain_sup_start를 부르지 않았으면 나머지는 아무 일도 하지 않는다. */
#ifndef SSP_BRAIN_SUP_H
#define SSP_BRAIN_SUP_H

#include <stdint.h>
#include <sys/types.h>

/* 공유 메모리를 만들고, 그 이름을 환경 변수에 넣고(스테이지와 뇌가 물려받는다), 뇌와 감시 스레드를 띄운다.
 * 다른 스레드를 만들기 전에, 시그널을 막은 뒤에 부른다. model은 fast, slow, real 중 하나다.
 * wait_mode가 1이면 스테이지가 틱마다 뇌를 기다린다(--fast).
 * 감시 스레드는 뇌가 멈춘 것을 알면 on_stall(그때의 재시작 횟수)을 부른다. 이 함수는 사건을 넣기만 해야 한다.
 * 0이면 성공. 모르는 모델 이름이면 -1. */
int  brain_sup_start(const char *model, int wait_mode, int strict, void (*on_stall)(uint32_t epoch));
const char *brain_sup_model(void);              /* 지금 쓰는 모델 이름. brain_sup_start 전에는 NULL */

void brain_sup_reaped(pid_t pid, int status);    /* waitpid로 거둔 자식마다 부른다. 뇌였으면 다시 띄운다 */
void brain_sup_stalled(uint32_t epoch);          /* 멈춤 사건을 받았을 때. 뇌를 죽인다(다시 띄우는 것은 거둔 뒤에) */
int  brain_sup_wait_ms(void);                    /* 다시 띄우기로 한 시각까지 남은 ms. 예정이 없으면 -1 */
void brain_sup_tick(void);                       /* 그 시각이 됐으면 다시 띄운다 */

#endif
