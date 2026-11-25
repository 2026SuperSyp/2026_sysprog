/* ranking.h : 역대 기록 파일
 *
 * 한 판이 끝날 때마다 참가자의 누적 점수를 글자 파일 끝에 붙인다. 한 줄이 기록 하나다.
 *
 *     이름 점수 시각\n            예) HENRY 245 2026-11-23T10:21:05Z
 *
 *  - 칸은 빈칸 하나로 나눈다. 시각은 UTC이고 항상 20글자이며 Z로 끝난다.
 *    그래서 중간에 끊긴 줄은 시각 칸이 모자라서 걸러진다.
 *  - 이름은 1~15바이트다. 보이는 ASCII 글자('%' 제외)와 올바른 UTF-8 글자는 그대로 적고,
 *    나머지 바이트(빈칸, 줄바꿈, 제어 글자, '%', 깨진 바이트)는 %XX(16진수 두 자리)로 적는다.
 *    이름에 무엇이 들어 있어도 기록 하나는 한 줄이고, 읽으면 원래 바이트로 돌아온다.
 *    빈 이름은 PLAYER로 적는다.
 *  - 읽을 때 틀에 맞지 않는 줄은 건너뛴다(끊긴 마지막 줄, 손으로 고치다 틀린 줄).
 *
 * 여러 프로세스가 같은 파일을 같이 써도 된다.
 *  - 붙일 때는 flock 배타 잠금을, 읽을 때는 공유 잠금을 잡는다(권고 잠금).
 *  - 파일은 O_APPEND로 열고 기록 하나를 write 한 번으로 쓴다.
 *  - 한 판의 기록은 잠금 하나 안에서 쓰므로 파일에서 서로 붙어 있다.
 *    쓰다가 실패하면 그 판의 기록을 모두 지워 원래 길이로 되돌린다.
 *  - 잠금은 2초까지만 기다린다. 못 잡으면 파일을 건드리지 않고 실패를 돌려준다.
 *
 * 전원이 나가도 남게 쓴다.
 *  - 붙인 뒤 fsync로 디스크에 적힐 때까지 기다린다. 새로 만든 파일이면 폴더도 fsync한다.
 *  - 파일이 ranking_compact_bytes보다 커지면 높은 순위 RANKING_KEEP개만 남긴다. 새 내용을 임시 파일(이름 뒤에 .tmp)에
 *    쓰고 fsync한 뒤 rename으로 바꿔 치운다. rename은 한 번에 일어나므로 도중에 죽어도 파일은 옛 것 아니면 새 것이다.
 *  - rename으로 파일이 바뀌면, 그 전에 열고 잠금을 기다리던 프로세스는 이름 없는 옛 파일을 잠근 것이 된다.
 *    그래서 잠근 뒤에 그 이름의 파일과 같은지(inode) 확인하고 다르면 새로 연다. */
#ifndef SSP_RANKING_H
#define SSP_RANKING_H

#include <stdint.h>

#define RANKING_PATH      "data/ranking.txt"
#define RANKING_NAME_LEN  16
#define RANKING_KEEP      200         /* 정리할 때 남기는 기록 수 */

/* 잠금을 기다리는 한도(ms). 검사에서만 줄여 쓴다. */
extern int ranking_lock_wait_ms;
/* 파일이 이보다 커지면 정리한다(바이트). 검사에서만 줄여 쓴다. */
extern int ranking_compact_bytes;

typedef struct {
    char    name[RANKING_NAME_LEN];    /* 읽은 것은 '\0'으로 끝난다 */
    int32_t score;                     /* 한 판의 누적 점수 */
    int64_t when;                      /* 기록한 시각. 1970년부터 센 초(UTC) */
} rank_rec_t;

/* 한 판의 기록 n개를 파일 끝에 붙인다. 폴더와 파일이 없으면 만든다.
 * 0이면 성공, -1이면 실패(이때 파일은 부르기 전과 같다). */
int ranking_append(const char *path, const rank_rec_t *recs, int n);

/* 점수가 높은 순서로 cap개까지 읽는다. 점수가 같으면 먼저 기록한 것이 앞이다.
 * 돌려주는 값은 채운 개수다. 파일이 없으면 0, 읽지 못하면 -1. */
int ranking_load_top(const char *path, rank_rec_t *out, int cap);

#endif
