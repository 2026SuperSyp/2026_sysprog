/* brain_shm.h : 뇌 프로세스와 주고받는 공유 메모리 (코어 전용)
 *
 * 주인은 서버다. 서버가 만들고 지운다. 뇌와 스테이지는 환경 변수로 받은 이름에 붙기만 한다.
 * 칸은 쓰는 쪽에 따라 둘로 나뉜다.
 *   게임 쪽 칸  스테이지가 쓴다(판 번호, 씨앗, 틱 번호, 집단별 자극)
 *   뇌 쪽 칸    뇌가 쓴다(계산을 마친 틱 번호, 집단별 반응, 뇌 지도)
 *
 * 읽고 쓰기: 묶음마다 순번(seqlock)이 하나 있다. 쓰는 쪽은 쓰기 전에 순번을 홀수로, 쓴 뒤에
 * 짝수로 만든다. 읽는 쪽은 앞뒤 순번이 같고 짝수일 때만 값을 받아들인다. 정해진 횟수만큼 해 보고
 * 안 되면 직전에 읽은 값을 그대로 둔다. 쓰다가 죽은 프로세스가 순번을 홀수로 남겨도 다음에 쓰는
 * 쪽이 바로잡는다. 잠금이 없으므로 한쪽이 죽어도 다른 쪽이 멈추지 않는다.
 *
 * 깨우기: 매핑 안의 세마포어 둘(wake, done)로 한다. 세마포어는 "보러 오라"는 신호일 뿐이고
 * 기준은 틱 번호다. 기다리는 쪽은 항상 시간 제한을 두고, 깨면 틱 번호를 다시 본다. 그래서 신호가
 * 하나 사라지거나 상대가 죽어도 영원히 기다리지 않는다. 짧게 자면서 들여다보는 방식도 되지만
 * 자는 간격만큼 반응이 늦어지고 쉴 때도 계속 깨야 해서 세마포어를 골랐다. */
#ifndef SSP_BRAIN_SHM_H
#define SSP_BRAIN_SHM_H

#include <semaphore.h>
#include <stdint.h>
#include "brain.h"
#include "brain/brain_host.h"

#define BRAIN_SHM_ENV      "SSP_BRAIN_SHM"     /* 공유 메모리 이름. 없으면 같은 프로세스 안의 뇌를 쓴다 */
#define BRAIN_SHM_WAIT_ENV "SSP_BRAIN_WAIT"    /* "1"이면 스테이지가 뇌를 기다린다(화면 없이 빨리 돌 때) */
#define BRAIN_DATA_ENV     "SSP_BRAIN_DATA"    /* 뇌 프로세스가 진짜 뇌의 자료를 읽을 폴더. 없으면 data */
#define BRAIN_SHM_MAGIC    0x42505353u         /* 'S' 'S' 'P' 'B' */
#define BRAIN_SHM_VERSION  1u
#define BRAIN_SHM_CELLS    (BRAIN_MAP_W * BRAIN_MAP_H)

/* 게임 쪽 칸 */
typedef struct {
    uint32_t game_no;                  /* 판 번호. 0은 아직 판이 없다는 뜻. 바뀌면 뇌가 처음 상태로 돌아간다 */
    uint32_t seed;
    uint32_t tick;                     /* 지금까지 자극을 확정한 틱 수 */
    uint32_t reserved;
    int64_t  sent_ns;                  /* 이 틱을 올린 시각(단조 시계). 걸린 시간을 재는 데만 쓴다 */
    float    stim[BRAIN_GROUP_COUNT];
} brain_shm_game_t;

/* 뇌 쪽 칸 */
typedef struct {
    uint32_t game_no;                  /* 이 반응이 속한 판 */
    uint32_t done_tick;                /* 계산을 마친 틱 수. 게임 쪽의 tick과 같으면 다 따라온 것이다 */
    uint32_t took_us;                  /* 자극이 올라온 때부터 반응을 내놓을 때까지 걸린 시간 */
    uint32_t calc_us;                  /* 그중 계산에 쓴 시간 */
    uint32_t skipped;                  /* 이 판에서 계산하지 못하고 건너뛴 틱 수 */
    uint32_t has_map;                  /* 아래 뇌 지도에 값이 있는가(진짜 뇌일 때만) */
    float    rate[BRAIN_GROUP_COUNT];
    float    heat[BRAIN_SHM_CELLS];
    uint16_t density[BRAIN_SHM_CELLS];
    uint8_t  mark[BRAIN_SHM_CELLS];
} brain_shm_brain_t;

typedef struct {
    /* 머리 */
    uint32_t magic, version, size;
    uint32_t restarts;                 /* 뇌를 다시 띄운 횟수. 서버만 올린다 */
    /* 게임 쪽 */
    uint32_t game_seq;
    brain_shm_game_t game;
    /* 뇌 쪽 */
    uint32_t brain_seq;
    brain_shm_brain_t brain;
    uint32_t heartbeat;                /* 뇌가 살아서 돌 때마다 올린다. 서버의 감시 스레드가 본다 */
    /* 깨우기 */
    sem_t wake;                        /* 게임 쪽이 틱을 올린 뒤에 올린다. 뇌가 기다린다 */
    sem_t done;                        /* 뇌가 반응을 내놓은 뒤에 올린다. 스테이지가 기다린다 */
} brain_shm_t;

/* 서버용. 새로 만든다(이미 있으면 실패). 실패하면 NULL. 지울 때는 shm_unlink를 쓴다. */
brain_shm_t *brain_shm_create(const char *name);
/* 뇌와 스테이지용. 이미 있는 것에 붙는다. 없거나 배치가 다르면 NULL. */
brain_shm_t *brain_shm_attach(const char *name);

/* 쓰기. 묶음마다 쓰는 프로세스는 한때에 하나여야 한다. */
void brain_shm_put_game(brain_shm_t *m, const brain_shm_game_t *g);
void brain_shm_put_brain(brain_shm_t *m, const brain_shm_brain_t *b);
void brain_shm_clear_brain(brain_shm_t *m);    /* 서버용. 뇌가 죽은 뒤에 뇌 쪽 칸을 비운다 */

/* 읽기. 1이면 out에 새 값을 담았다. 0이면 읽지 못했고 out은 건드리지 않았다(직전 값이 남는다). */
int  brain_shm_get_game(brain_shm_t *m, brain_shm_game_t *out);
int  brain_shm_get_brain(brain_shm_t *m, brain_shm_brain_t *out);

/* 깨우기. post는 밀린 신호가 없을 때만 하나 올린다. wait는 ms까지만 기다리고, 신호를 받았으면 1이다. */
void brain_shm_post(sem_t *s);
int  brain_shm_wait(sem_t *s, int ms);

int64_t brain_shm_now_ns(void);                /* 단조 시계 */

static inline uint32_t brain_shm_load(const uint32_t *p) { return __atomic_load_n(p, __ATOMIC_SEQ_CST); }
static inline void     brain_shm_bump(uint32_t *p)       { __atomic_add_fetch(p, 1u, __ATOMIC_SEQ_CST); }

#endif
