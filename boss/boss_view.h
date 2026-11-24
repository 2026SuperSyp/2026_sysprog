/* boss_view.h : 보스전 그리기 (클라이언트용)
 *
 * 서버가 보낸 상태(boss_snap_t) 하나로 1인칭 3D 화면을 그린다. 벽은 화면의 세로줄마다 광선을 하나씩
 * 쏘아 닿은 거리만큼의 높이로 세우고(레이캐스팅), 초파리와 다른 참가자는 세워 둔 그림처럼 거리에 맞춰 그린다.
 * 계산부(boss.c)의 상태를 바꾸지 않는다. 그림 파일을 쓰지 않고 전부 코드로 그린다. */
#ifndef SSP_BOSS_VIEW_H
#define SSP_BOSS_VIEW_H

#include "boss/boss.h"
#include "ui.h"

/* 방의 높이. 계산부는 평면에서만 계산하고(뛰어오른 높이만 따로 가진다), 벽이 얼마나 높은가는 그리는 쪽의 일이다 */
#define BOSS_VIEW_WALL_H   6.0f          /* 벽의 높이(칸). 천장이 이 높이에 있다. 방의 너비가 22칸이라 이보다 낮으면 납작해 보인다 */
#define BOSS_VIEW_EYE_H    0.5f          /* 서 있을 때의 눈높이(칸). 뛰어오르면 그만큼 높아진다 */
#define BOSS_VIEW_FLY_HOVER 0.45f        /* 초파리가 평소에 떠 있는 높이(칸, 발끝에서 바닥까지) */

struct brain_map;                        /* brain/brain_host.h */
struct image;                            /* ui/hd.h */

typedef struct {
    int me;                              /* 내 참가자 번호. 관전자는 -1 */
    int watch;                           /* 내 눈으로 볼 수 없을 때(관전자, 쓰러짐) 누구의 눈으로 볼지. 쓰러진 사람이면 그다음 사람 */
    const char *name[BOSS_PLAYERS];      /* 참가자 이름. 없으면 NULL */
    int hit_ms, hurt_ms, wave_ms;        /* 내 공격이 맞은 뒤, 내가 맞은 뒤, 날갯짓 뒤로 지난 시간(ms). 없었으면 음수 */
    int block_ms;                        /* 내가 돌진을 막은 뒤로 지난 시간(ms). 없었으면 음수 */
    int dodge_ms;                        /* 내가 돌진을 뛰어넘은 뒤로 지난 시간(ms). 없었으면 음수 */
    int help;                            /* 1이면 시프봇의 키 안내를 띄운다(H로 켜고 끈다). 시작하기 전에는 이것과 무관하게 띄운다 */
    int age_ms;                          /* 보스전 단계에 들어온 뒤 지난 시간(ms). 시프봇이 움직이는 데 쓴다 */
    /* 내 눈의 방향. look_on이 1이면 상태에 든 내 방향 대신 look_ang(0..65535가 한 바퀴)으로 그린다.
     * 마우스로 돌린 방향은 서버를 거쳐 돌아오기 전에 화면에 먼저 보여야 해서 클라이언트가 따로 넘긴다 */
    int look_on;
    uint16_t look_ang;
    /* 초파리 뇌 표시(미니게임 화면의 것과 같은 그림). brain_rate가 NULL이면 그리지 않는다.
     * brain_map은 진짜 뇌일 때만 있다. takeoff는 초파리가 뛰어오르는 기준(달아남 반응, 0~1)이다 */
    const float *brain_stim, *brain_rate;
    const struct brain_map *brain_map;
    float takeoff;
} boss_view_t;

/* full은 240x134이거나 그것을 배율 N으로 키운 크기(240N x 134N)다. 키운 화면이면 3D는 그 크기에 맞춰
 * 촘촘하게 그리고, 글자와 상태 줄은 화면에 비해 작게 그려서 3D 화면이 넓어진다. */
void boss_view_draw(canvas_t *full, const boss_snap_t *b, const boss_view_t *v);

/* 초파리가 지금 떠 있는 높이(칸). 두 그리는 쪽이 같이 쓴다. 한 프레임에 한 번 부른다 */
float boss_view_fly_lift(const boss_snap_t *b);

/* 같은 화면을 픽셀 그림에 그린다(boss_view_hd.c). 화면을 Sixel 그림으로 보낼 때 쓴다. 색을 마음대로 쓸 수 있어서
 * 벽과 바닥에 무늬와 명암을 넣고 가장자리를 부드럽게 그린다. 그림의 크기는 240x134의 정수배여야 한다.
 * 시프봇의 키 안내는 그리지 않는다(그때는 boss_view_draw로 240x134에 그린 것을 키워 쓴다). */
void boss_view_draw_hd(struct image *im, const boss_snap_t *b, const boss_view_t *v);

#endif
