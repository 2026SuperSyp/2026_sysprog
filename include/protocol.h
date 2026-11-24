/* protocol.h : 통신 규격 (코어 전용)
 *
 * 메시지는 8바이트 머리와 내용으로 되어 있다. 같은 틀을 두 곳에서 쓴다.
 *   - 클라이언트와 서버 사이의 TCP 소켓
 *   - 서버와 스테이지 프로세스 사이의 파이프
 * 숫자는 리틀 엔디언이다. 구조체는 양쪽이 같은 컴파일러로 빌드된다고 보고 그대로 보낸다
 * (채움 바이트가 생기지 않게 크기 순서를 맞춰 두었다).
 * 모르는 종류의 메시지는 len만큼 건너뛴다. len이 상한을 넘으면 접속을 끊는다. */
#ifndef SSP_PROTOCOL_H
#define SSP_PROTOCOL_H

#include <stdint.h>

#define PROTO_VERSION    2             /* 2: 보스전의 시선 입력(INPUT_LOOK), MSG_BOSS에 붙는 뇌 표시, boss_snap_t의 각도 */
#define PROTO_PORT       4620
#define PROTO_MAX_LEN    (256u * 1024u)
#define PROTO_NAME_LEN   16
#define PROTO_SEATS      3             /* 참가자 자리 수. 그 뒤로 들어온 사람은 관전자다 */
#define PROTO_PANES      8             /* 화면 메시지 하나에 담는 칸의 최대 수 */
#define PROTO_GROUPS     32            /* 뇌 집단 값을 담는 칸 수(BRAIN_GROUP_COUNT 이상) */

typedef struct {
    uint32_t len;                      /* 머리 뒤에 오는 내용의 길이 */
    uint8_t  type;                     /* MSG_* */
    uint8_t  flags;
    uint16_t slot;                     /* 보낸 사람 또는 대상의 자리 */
} msg_hdr_t;

enum {
    /* 클라이언트 <-> 서버 */
    MSG_HELLO = 1,                     /* C->S  hello_msg_t. 접속 뒤 첫 메시지 */
    MSG_WELCOME,                       /* S->C  welcome_msg_t */
    MSG_REJECT,                        /* S->C  이유를 적은 글자열. 보낸 뒤 서버가 끊는다 */
    MSG_PHASE,                         /* S->C  phase_msg_t. 단계가 바뀌거나 로비 상태가 바뀔 때 */
    MSG_INPUT,                         /* C->S, S->스테이지  input_msg_t */
    MSG_FRAME,                         /* 스테이지->S->C  frame_head_t와 줄여 담은 그림들 */
    MSG_PING,
    MSG_PONG,
    MSG_TOP,                           /* S->C  top_msg_t. 접속한 직후와 한 판이 끝났을 때 */
    MSG_BOSS,                          /* S->C  boss_snap_t(boss/boss.h), boss_brain_msg_t, 뇌 지도(flags에 FRAME_HAS_MAP이
                                          있을 때)를 차례로 붙인 것. 보스전 동안 틱마다. 최신 것만 있으면 된다 */
    /* 서버 <-> 스테이지 */
    MSG_INFO = 32,                     /* 스테이지->S  info_msg_t. 뜨자마자 한 번 */
    MSG_TICK,                          /* S->스테이지  내용 없음. 한 틱을 진행하라 */
    MSG_RESULT,                        /* 스테이지->S  result_msg_t. 마지막 틱 뒤에 한 번 */
};

/* 단계. 게임 마스터의 상태 기계와 같다. */
enum { PH_LOBBY = 0, PH_CARD, PH_READY, PH_PLAY, PH_VERDICT, PH_BOSS, PH_RANK };

typedef struct {
    uint16_t version;                  /* PROTO_VERSION */
    uint16_t peek;                     /* 1이면 역대 순위(MSG_TOP)만 받고 끊는다. 자리에 앉지 않는다 */
    char     name[PROTO_NAME_LEN];
} hello_msg_t;

typedef struct {
    uint16_t version;
    uint16_t slot;                     /* 서버가 준 접속 번호 */
} welcome_msg_t;

/* 단계가 바뀔 때 클라이언트가 화면을 그리는 데 필요한 것을 한 번에 싣는다. */
typedef struct {
    uint32_t ms_left;                  /* 이 단계가 끝날 때까지 남은 시간. 로비는 0 */
    int32_t  score[PROTO_SEATS];       /* 이번 라운드 점수(판정 단계) */
    int32_t  total[PROTO_SEATS];       /* 누적 점수 */
    int32_t  fly_score;                /* 이번 라운드 초파리 점수 */
    uint8_t  phase;                    /* PH_* */
    uint8_t  round;                    /* 1부터. 로비에서는 0 */
    uint8_t  n_rounds;
    uint8_t  n_seats;                  /* 지금 앉아 있는 참가자 수 */
    int8_t   me;                       /* 받는 사람의 자리. 관전자는 -1 */
    int8_t   host;                     /* 방장의 자리. 없으면 -1 */
    int8_t   pane_of[PROTO_SEATS];     /* 그 자리의 사람이 이번 라운드에 쓰는 칸. 안 뛰면 -1 */
    uint8_t  seated[PROTO_SEATS];      /* 자리에 사람이 있는가 */
    uint8_t  ready[PROTO_SEATS];
    uint8_t  lives[PROTO_SEATS];
    uint8_t  passed[PROTO_SEATS];      /* 이번 라운드에서 초파리를 이겼는가(판정 단계) */
    uint8_t  pad[3];
    char     name[PROTO_SEATS][PROTO_NAME_LEN];
    char     title[32];                /* 이번 스테이지 */
    char     rule[96];
    char     controls[48];
} phase_msg_t;

/* 역대 순위. 랭킹 파일에서 점수가 높은 순서로 뽑은 것이다. i번째가 i+1등이다. */
#define PROTO_TOP_N 10
typedef struct {
    int32_t  score[PROTO_TOP_N];
    uint8_t  n;                        /* 채워진 줄 수 */
    uint8_t  fresh[PROTO_TOP_N];       /* 방금 끝난 판에서 나온 기록이면 1 */
    uint8_t  pad;
    char     name[PROTO_TOP_N][PROTO_NAME_LEN];
} top_msg_t;

/* 입력. 클라이언트는 해석만 한 원시 사건을 올린다.
 *   C->S        kind, flags, code는 ui의 in_event_t와 같다. 마우스의 x, y는 전체 화면(240x134) 픽셀이다.
 *               보스전에서는 kind가 INPUT_LOOK인 것도 올린다: code가 내가 보는 방향(0..65535가 한 바퀴)이다.
 *               마우스로 도는 양은 클라이언트가 계산하고 서버에는 결과인 방향만 알린다.
 *   S->스테이지  player가 채워진다. 키는 code가 SK_*이고, 마우스의 x, y는 칸 기준 좌표다. */
#define INPUT_LOOK 3
typedef struct {
    uint8_t  kind;                     /* 1: 키, 2: 마우스, 3: 보스전의 시선(INPUT_LOOK) */
    uint8_t  flags;                    /* 마우스: 1이면 왼쪽 버튼이 눌려 있다 */
    uint16_t code;
    int16_t  x, y;
    uint8_t  player;
    uint8_t  pad[3];
} input_msg_t;

/* 화면. 앞 화면에 기대지 않는 완결된 그림이다.
 * 머리 뒤에 (n_panes + 1)개의 그림이 온다. 첫째가 초파리 칸이고 그 뒤가 참가자 칸이다.
 * 그림 하나는 4바이트 길이와 그만큼의 바이트이고, (개수, 색 번호) 쌍으로 줄여 담는다.
 * flags에 FRAME_HAS_MAP이 있으면 뇌 지도 하나가 더 온다(칸마다 뉴런 유무, 발화, 표시 순서로 w*h씩). */
#define FRAME_HAS_MAP 1
typedef struct {
    uint16_t tick, n_ticks;
    int32_t  score[PROTO_PANES];
    int32_t  fly_score;
    uint8_t  n_panes;
    uint8_t  map_w, map_h;
    uint8_t  pad;
    uint8_t  stim[PROTO_GROUPS];       /* 뇌 집단별 자극(0~255) */
    uint8_t  rate[PROTO_GROUPS];       /* 뇌 집단별 반응(0~255) */
} frame_head_t;

/* 보스전의 뇌 표시. 미니게임의 화면 메시지에 실리는 뇌 값과 같은 것이다. 보스전에서는 MSG_BOSS의 상태 뒤에 붙는다
 * (따로 보내면 "최신 것만" 보내는 칸에서 상태와 서로 밀어낸다).
 * flags에 FRAME_HAS_MAP이 있으면 뒤에 뇌 지도가 온다(화면 메시지의 뇌 지도와 같은 모양). */
typedef struct {
    uint8_t  stim[PROTO_GROUPS];       /* 뇌 집단별 자극(0~255) */
    uint8_t  rate[PROTO_GROUPS];       /* 뇌 집단별 반응(0~255) */
    uint8_t  map_w, map_h;
    uint8_t  takeoff;                  /* 초파리가 뛰어오르는 기준. 달아남 반응이 이 값 이상이면 뛴다(0~255) */
    uint8_t  pad;
} boss_brain_msg_t;

typedef struct {
    uint16_t n_ticks;
    uint16_t reserved;
    char     title[32];
    char     rule[96];
    char     controls[48];
} info_msg_t;

typedef struct {
    int32_t  score[PROTO_PANES];       /* 반올림한 점수 */
    int32_t  fly_score;
} result_msg_t;

#endif
