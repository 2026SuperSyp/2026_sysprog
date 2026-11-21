/* stage.h : 스테이지와 코어 사이의 약속
 *
 * 스테이지 하나는 C 파일 하나 이상과 config.ini 하나다. 아래 함수들을 구현한다.
 *
 * 부르는 규칙
 *  1. 아래 함수들은 한 스레드에서 차례로만 불린다. 시그널 처리기에서는 불리지 않는다.
 *  2. 프로세스 하나가 한 판이다. stage_start는 프로세스마다 한 번만 불린다.
 *     그래서 static 변수를 써도 되고, 처음 상태로 되돌리는 코드는 필요 없다.
 *  3. stage_info는 stage_start보다 먼저 불릴 수 있다.
 *  4. stage_start는 READY GO 전에 불린다. 첫 stage_tick 전에도 stage_draw가 불릴 수 있다.
 *  5. stage_tick은 한 판에 정확히 ctx->n_ticks번 불린다. dt는 항상 STAGE_TICK_DT다.
 *  6. 입력(stage_input, stage_pointer)은 GO 뒤부터 마지막 stage_tick 전까지만 온다.
 *     그 사이에 들어온 입력이 도착한 순서대로 먼저 불리고, 그다음 stage_tick이 한 번 불린다.
 *  7. 시간은 틱 수(정수)로 센다. 초가 필요하면 틱 수 * STAGE_TICK_DT로 구한다.
 *     시계를 읽거나 sleep하지 않는다.
 *  8. stage_draw와 stage_result는 몇 번이든 불릴 수 있고 상태를 바꾸지 않는다.
 *  9. 문제는 ctx->seed에서만 만든다. rand, srand, time을 쓰지 않고 stage_rng_*를 쓴다.
 * 10. 소켓, 스레드, fork, 파일 쓰기를 쓰지 않는다. 화면에 직접 쓰지 않는다.
 *     printf와 fprintf(stderr, ...)는 화면이 아니라 로그 파일로 간다.
 *
 * 그 밖의 약속
 *  - player는 항상 0부터 n_players-1까지 빈 번호 없이 온다. score[i]도 같은 번호다.
 *  - 유저마다 상태를 따로 둔다. 참가자의 결과는 다른 참가자의 입력에 영향받지 않는다.
 *  - 키는 누른 순간마다 온다. 뗀 순간은 오지 않는다. 누르고 있으면 같은 key가 계속 오지만
 *    한 틱에 한 번만 온다. 이동은 "누를 때마다 한 걸음"으로 만든다.
 *  - fly에 그리는 내용은 player와 무관해야 한다(모두가 같은 초파리 칸을 본다). */
#ifndef SSP_STAGE_API_H
#define SSP_STAGE_API_H

#include <stdint.h>
#include "ui.h"

#define STAGE_MAX_PLAYERS 8        /* 유저별 배열의 크기. 지금 게임은 최대 3명 */
#define STAGE_TICK_DT     0.05     /* 초 */

typedef struct {
    const char *title;      /* 게임 이름. 한글 8자(영문 16자)까지. 예: "거미줄 미로" */
    const char *rule;       /* 시프봇이 말해 주는 규칙. 두 줄까지('\n'으로 나눔), 한 줄에 한글 14자까지 */
    const char *controls;   /* 조작 한 줄. 한글 14자까지. 예: "마우스" */
} stage_info_t;

typedef struct {
    int      n_players;     /* 이번 판 참가자 수. 1 이상이고 판 도중 바뀌지 않는다 */
    uint32_t seed;          /* 문제 생성용 씨앗. 초파리와 모든 유저가 같은 문제를 받는다 */
    float    duration;      /* 판 길이(초). 참고용이다. 판의 끝은 n_ticks로 안다 */
    float    speed;         /* 지금은 항상 1.0. 스테이지는 곱하지 않는다 */
    int      n_ticks;       /* 이번 판에 stage_tick이 불리는 횟수. 400을 직접 쓰지 않는다 */
} stage_ctx_t;              /* stage_start가 받은 ctx는 프로세스가 끝날 때까지 유효하다 */

typedef struct {
    float score[STAGE_MAX_PLAYERS];   /* score[i]는 player i의 점수. 0 이상, 클수록 좋다.
                                         한 판을 잘하면 100 근처가 되게 config.ini로 맞춘다 */
    float fly_score;                  /* 초파리 점수 */
} stage_result_t;

enum {
    SK_NONE = 0,
    SK_LEFT, SK_RIGHT, SK_UP, SK_DOWN,   /* 방향키 또는 A, D, W, S */
    SK_ACT,                              /* 스페이스(주 행동) */
    SK_SUB,                              /* F(보조 행동) */
    SK_1, SK_2, SK_3, SK_4, SK_5, SK_6, SK_7, SK_8, SK_9
};

const stage_info_t *stage_info(void);
void stage_start  (const stage_ctx_t *ctx);
void stage_input  (int player, int key, double t);   /* t: 지금까지 불린 stage_tick 횟수 * STAGE_TICK_DT */

/* 마우스가 내 칸 위에 있을 때만 불린다. x는 0~93, y는 0~107이 보장된다.
 * 좌표가 몇 픽셀씩 변하는지는 가정하지 않는다. 중간 위치는 생략될 수 있다.
 * pressed는 왼쪽 버튼이 눌려 있는 동안 1이다.
 * 마우스를 쓰지 않는 스테이지는 이 함수를 만들지 않아도 된다. */
void stage_pointer(int player, int x, int y, int pressed);

void stage_tick   (double dt);
void stage_draw   (int player, canvas_t *fly, canvas_t *me);   /* fly와 me는 NULL이 아니다. 매번 전부 그린다 */
void stage_result (stage_result_t *out);                       /* 지금까지의 점수를 채우기만 한다 */

/* 설정. stages/이름/config.ini에서 읽는다. 없는 키는 fallback을 주고 로그에 경고를 남긴다 */
float cfg_get(const char *key, float fallback);

/* 난수. 상태를 직접 들고 있으므로 초파리용, 유저용을 따로 둘 수 있다 */
typedef struct { uint64_t s; } stage_rng_t;
void     stage_rng_seed (stage_rng_t *r, uint32_t seed);
uint32_t stage_rng_u32  (stage_rng_t *r);
float    stage_rng_float(stage_rng_t *r);                   /* 0.0 이상 1.0 미만 */
int      stage_rng_range(stage_rng_t *r, int lo, int hi);   /* lo 이상 hi 이하 */

#endif
