/* brain.h : 스테이지와 초파리 뇌 사이의 약속
 *
 *  - brain_*는 stage_tick 안에서만 부른다.
 *  - 뇌의 값은 stage_tick 직전에 한 번 고정된다. 같은 틱 안에서는 몇 번 읽어도 같다.
 *  - brain_stimulate는 그 틱에만 유효하다. 다음 틱에 다시 부르지 않으면 0이 된다.
 *    같은 집단을 한 틱에 여러 번 부르면 마지막 값이 남는다.
 *  - 이번 틱에 준 자극의 반응은 빨라야 다음 틱에 보인다.
 *  - brain_rate는 뇌 쪽이 집단마다 정한 구간으로 평균한 값이다. 다시 평균 내지 않는다.
 *  - brain_fired는 그 틱의 반응이 기준 이상이면 1이다. 자극이 이어지면 계속 1이다.
 *    한 번만 반응해야 하면 스테이지가 자기 쿨다운을 두고 그 값을 config.ini에 둔다.
 *  - brain_*는 멈추지 않고 항상 범위 안의 값을 준다.
 *  - L과 R은 초파리 기준의 왼쪽과 오른쪽이다. BR_LOOM_L(왼쪽에서 다가옴)을 주면
 *    BR_TURN_R(오른쪽으로 돌려는 정도)이 오른다. */
#ifndef SSP_BRAIN_API_H
#define SSP_BRAIN_API_H

typedef enum {
    /* 입력(자극) */
    BR_SUGAR, BR_BITTER, BR_SOUR, BR_WATER,
    BR_LOOM_L, BR_LOOM_R,
    BR_ODOR_L, BR_ODOR_R,
    BR_REPEL_L, BR_REPEL_R,
    /* 출력(반응): 스테이지용 */
    BR_FEED, BR_ESCAPE,
    BR_TURN_L, BR_TURN_R,        /* 좌, 우로 돌려는 정도 */
    BR_FORWARD,                  /* 앞으로 가려는 정도 */
    /* 출력(반응): 보스전용 */
    BR_BACKWARD, BR_LAND, BR_AROUSAL,
    /* 출력(반응): 카드 암기 스테이지용 */
    BR_VALENCE,
    BRAIN_GROUP_COUNT            /* 새 항목은 이 줄 바로 앞에만 추가한다 */
} brain_group_t;

void  brain_stimulate(brain_group_t in, float level);   /* 0.0~1.0. 그 틱에만 유효 */
float brain_rate     (brain_group_t out);               /* 0.0~1.0 */
int   brain_fired    (brain_group_t out);               /* 기준 이상인 동안 1 */

#endif
