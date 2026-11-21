/* 합성: 칸 그림들을 전체 화면(240x134)에 배치한다.
 *
 *   위 띠 14줄   : 게임 이름, 남은 시간, 라운드
 *   가운데 108줄 : 초파리 칸 | 내 칸 | 다른 두 명(절반 크기, 위아래)
 *   아래 띠 12줄 : 내 점수, 초파리 점수, 남은 기회 */
#include "ui/ui_internal.h"

#include <stdio.h>

#define TOP_H    14
#define FLY_X    1
#define ME_X     97
#define OTHER_X  193
#define OTHER_W  (PANE_W / 2)
#define OTHER_H  (PANE_H / 2)

static void frame_rect(canvas_t *c, int x, int y, int w, int h, color_t col)
{
    ui_line(c, x, y, x + w - 1, y, col);
    ui_line(c, x, y + h - 1, x + w - 1, y + h - 1, col);
    ui_line(c, x, y, x, y + h - 1, col);
    ui_line(c, x + w - 1, y, x + w - 1, y + h - 1, col);
}

void compose_stage_frame(canvas_t *full, const canvas_t *fly, canvas_t *const panes[],
                         int n_players, int me, const hud_t *hud)
{
    char buf[64];
    ui_clear(full, C_BLACK);

    /* 위 띠 */
    ui_rect(full, 0, 0, SCREEN_W, TOP_H - 1, C_GRAY1);
    ui_text(full, 3, 3, hud->title ? hud->title : "", C_YELLOW);
    snprintf(buf, sizeof buf, "R%d  TIME %d", hud->round, (hud->ticks_left + 19) / 20);
    ui_text(full, SCREEN_W - 3 - ui_text_w(buf), 3, buf, C_TEXT);

    /* 초파리 칸 */
    canvas_blit(full, FLY_X, TOP_H, fly);
    frame_rect(full, FLY_X - 1, TOP_H - 1, PANE_W + 2, PANE_H + 2, C_GRAY2);

    /* 내 칸(테두리 강조) */
    if (me >= 0 && me < n_players)
        canvas_blit(full, ME_X, TOP_H, panes[me]);
    frame_rect(full, ME_X - 1, TOP_H - 1, PANE_W + 2, PANE_H + 2, C_YELLOW);

    /* 다른 사람 칸: 절반 크기로 위아래에 놓는다 */
    int slot = 0;
    for (int p = 0; p < n_players && slot < 2; p++) {
        if (p == me) continue;
        int y = TOP_H + slot * OTHER_H;
        canvas_blit_half(full, OTHER_X, y, panes[p]);
        frame_rect(full, OTHER_X - 1, y - 1, OTHER_W + 1, OTHER_H + 1, C_GRAY2);
        slot++;
    }

    /* 아래 띠 */
    int by = TOP_H + PANE_H + 1;
    ui_rect(full, 0, by, SCREEN_W, SCREEN_H - by, C_GRAY1);
    snprintf(buf, sizeof buf, "YOU %d   FLY %d", hud->my_score, hud->fly_score);
    ui_text(full, 3, by + 2, buf, hud->my_score >= hud->fly_score ? C_GOOD : C_BAD);
    snprintf(buf, sizeof buf, "LIVES %d", hud->lives);
    ui_text(full, SCREEN_W - 3 - ui_text_w(buf), by + 2, buf, C_TEXT);
}

int compose_my_pane_xy(int screen_x, int screen_y, int *pane_x, int *pane_y)
{
    int x = screen_x - ME_X, y = screen_y - TOP_H;
    if (x < 0 || y < 0 || x >= PANE_W || y >= PANE_H) return 0;
    *pane_x = x;
    *pane_y = y;
    return 1;
}
