/* screen.h : 화면(로비, 판정, 랭킹, 관전)과 코어 사이의 약속
 *
 * 코어가 상태를 채워서 넘기면 화면 함수가 전체 240x134에 그린다.
 *  - 화면 함수는 받은 상태만 그린다. 상태를 바꾸지 않는다.
 *  - static 변수와 시계 읽기를 쓰지 않는다. 움직임과 깜빡임은 t로만 계산한다.
 *  - 화면이 떠 있는 동안 초당 20번까지 반복해서 불린다.
 *  - 그림은 화면 함수 안에서 ui_sprite_load로 불러도 된다(screens/assets/에서 찾는다).
 *  - 새 필드는 구조체 끝에만 추가된다.
 *  - 코어에 글자만 찍는 기본 화면이 들어 있다. screens/ 안에 같은 이름의 함수를
 *    만들면 그쪽이 쓰인다. */
#ifndef SSP_SCREEN_API_H
#define SSP_SCREEN_API_H

#include "ui.h"

typedef struct { char name[16]; int ready; int ping_ms; int is_host; } lobby_player_t;

/* 로비. 준비는 SK_ACT, 시작은 방장의 SK_SUB다(키 입력은 코어가 처리한다). */
typedef struct {
    int n; lobby_player_t p[8];
    int me;                    /* 보는 사람의 줄 번호. 관전자이거나 목록에 없으면 -1 */
    double t;                  /* 이 화면이 뜬 뒤 흐른 초 */
} lobby_state_t;

enum { NEXT_ROUND = 0, NEXT_SPECTATE = 1, NEXT_BOSS = 2 };

/* 판정. 목록은 이번 라운드에 뛴 사람이다. 승패는 passed를 그대로 쓴다. */
typedef struct {
    int n; char name[8][16]; float score[8];
    int passed[8];             /* 이번 라운드에서 초파리를 이겼으면 1 */
    float fly_score; const char *stage_name;
    int lives[8];              /* 이번 판정을 반영한 뒤 남은 기회 */
    int next[8];               /* 다음에 가는 곳. NEXT_* */
    int me; double t;
} round_result_t;

/* 랭킹. total이 큰 순서로 정렬돼서 온다. i번째 줄이 i+1등이다. */
typedef struct {
    int n; char name[16][16];
    int total[16];             /* 코어가 계산한 정수 점수 */
    char best_stage[16][32];   /* 가장 잘한 스테이지의 제목 */
    int me; double t;
} ranking_t;

/* 관전. 코어가 화면을 전부 그린 뒤 마지막에 부른다.
 * ui_clear를 부르지 않고 테두리와 안내 글자만 덧그린다. */
typedef struct { char watching[16]; double t; } spectate_state_t;

void screen_lobby   (canvas_t *full, const lobby_state_t *s);
void screen_result  (canvas_t *full, const round_result_t *r);
void screen_ranking (canvas_t *full, const ranking_t *r);
void screen_spectate(canvas_t *full, const spectate_state_t *s);

#endif
