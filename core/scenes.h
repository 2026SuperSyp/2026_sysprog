/* scenes.h : 게임 밖의 장면 그리기 (make run과 클라이언트가 같이 쓴다) */
#ifndef SSP_SCENES_H
#define SSP_SCENES_H

#include "ui.h"

/* 말풍선. (x, y)는 왼쪽 위이고 크기는 글에 맞춰진다. 글은 '\n'에서 줄을 나눈다(4줄까지).
 * tail_x는 아래로 내려오는 꼬리의 가로 위치다. 풍선 밖이면 꼬리를 그리지 않는다. */
void scene_bubble(canvas_t *c, int x, int y, const char *text, color_t ink, int tail_x);
int  scene_bubble_w(const char *text);
int  scene_bubble_h(const char *text);

/* 규칙 설명: 위에 제목, 시프봇이 말풍선으로 규칙(두 줄까지, '\n'에서 나눈다)을 말하고, 아래에 조작법.
 * tick은 이 장면이 뜬 뒤 지난 틱 수다(입 모양이 움직인다). */
void scene_card(canvas_t *full, const char *title, const char *rule, const char *controls, int tick);
/* 카운트다운: tick 0~9는 3, 10~19는 2, 20~29는 1, 그 뒤는 GO! 시프봇 그림이 크게 나온다 */
void scene_count(canvas_t *full, int tick);
/* 가운데에 한 줄 */
void scene_message(canvas_t *full, const char *line, color_t col);

#endif
