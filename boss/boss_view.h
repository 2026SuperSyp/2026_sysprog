/* boss_view.h : 보스전 그리기 (클라이언트용)
 *
 * 서버가 보낸 상태(boss_snap_t) 하나로 1인칭 3D 화면을 그린다. 벽은 화면의 세로줄마다 광선을 하나씩
 * 쏘아 닿은 거리만큼의 높이로 세우고(레이캐스팅), 초파리와 다른 참가자는 세워 둔 그림처럼 거리에 맞춰 그린다.
 * 계산부(boss.c)의 상태를 바꾸지 않는다. 그림 파일을 쓰지 않고 전부 코드로 그린다. */
#ifndef SSP_BOSS_VIEW_H
#define SSP_BOSS_VIEW_H

#include "boss/boss.h"
#include "ui.h"

typedef struct {
    int me;                              /* 내 참가자 번호. 관전자는 -1 */
    int watch;                           /* 내 눈으로 볼 수 없을 때(관전자, 쓰러짐) 누구의 눈으로 볼지. 쓰러진 사람이면 그다음 사람 */
    const char *name[BOSS_PLAYERS];      /* 참가자 이름. 없으면 NULL */
    int hit_ms, hurt_ms, wave_ms;        /* 내 공격이 맞은 뒤, 내가 맞은 뒤, 날갯짓 뒤로 지난 시간(ms). 없었으면 음수 */
    int help;                            /* 1이면 시프봇의 키 안내를 띄운다(H로 켜고 끈다). 시작하기 전에는 이것과 무관하게 띄운다 */
    int age_ms;                          /* 보스전 단계에 들어온 뒤 지난 시간(ms). 시프봇이 움직이는 데 쓴다 */
} boss_view_t;

void boss_view_draw(canvas_t *full, const boss_snap_t *b, const boss_view_t *v);

#endif
