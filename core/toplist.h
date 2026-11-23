/* toplist.h : 역대 순위를 메시지에 담고 화면에 그린다 (서버와 클라이언트가 같이 쓴다) */
#ifndef SSP_TOPLIST_H
#define SSP_TOPLIST_H

#include "core/ranking.h"
#include "protocol.h"
#include "ui.h"

/* 랭킹 파일에서 위 PROTO_TOP_N개를 읽어 메시지를 채운다. 파일이 없으면 빈 목록이 된다.
 * just[0..n_just)와 같은 기록에는 fresh 표시를 한다(방금 끝난 판의 기록). 없으면 NULL, 0.
 * 0이면 성공. 읽지 못하면 -1을 돌려주고 m은 그대로 둔다. */
int  toplist_load(top_msg_t *m, const char *path, const rank_rec_t *just, int n_just);

/* 전체 화면의 y0 이상 y1 미만인 띠에 역대 순위를 그린다. 화면 함수가 그린 뒤에 덧그린다(지우지 않는다).
 * 띠가 높으면 한 줄에 한 명씩 그리고, 낮으면 두 단으로 나누며 긴 이름은 줄인다.
 * mark_fresh가 1이면 방금 끝난 판의 기록을 밝게 그린다. */
void toplist_draw(canvas_t *full, const top_msg_t *m, int y0, int y1, int mark_fresh);

/* 직전에 불린 화면 함수가 코어의 기본 화면이었으면 1을 돌려준다(한 번 읽으면 지워진다).
 * 팀원이 만든 화면 위에는 덧그리지 않으려고 쓴다. 화면 함수를 부르기 전에 한 번 읽어 지워 둔다. */
int  default_screen_drew(void);

/* 기본 화면(core/default_screens.c)에서 비어 있는 띠 */
#define TOPLIST_RANK_Y0   (16 + 9 * PROTO_SEATS)     /* 이번 판 순위 줄들의 아래 */
#define TOPLIST_RANK_Y1   SCREEN_H
#define TOPLIST_LOBBY_Y0  (32 + 10 * PROTO_SEATS)    /* 참가자 줄들의 아래 */
#define TOPLIST_LOBBY_Y1  118                        /* 아래 안내 글자의 위 */

#endif
