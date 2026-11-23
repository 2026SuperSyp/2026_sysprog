/* scenes.h : 게임 밖의 장면 그리기 (make run과 클라이언트가 같이 쓴다) */
#ifndef SSP_SCENES_H
#define SSP_SCENES_H

#include "ui.h"

/* 규칙 설명: 제목, 규칙(두 줄까지, '\n'에서 나눈다), 조작법 */
void scene_card(canvas_t *full, const char *title, const char *rule, const char *controls);
/* 카운트다운: tick 0~9는 3, 10~19는 2, 20~29는 1, 그 뒤는 GO! */
void scene_count(canvas_t *full, int tick);
/* 가운데에 한 줄 */
void scene_message(canvas_t *full, const char *line, color_t col);

#endif
