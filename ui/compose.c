/* 합성: 칸 그림들을 전체 화면(240x134)에 배치한다.
 *
 *   위 띠 14줄   : 게임 이름, 남은 시간, 라운드
 *   가운데 108줄 : 초파리 칸 | 내 칸 | 오른쪽 위: 다른 참가자 한 명(절반 크기)
 *                                      오른쪽 아래: 초파리 뇌 표시
 *   아래 띠 12줄 : 내 점수, 초파리 점수, 남은 기회 */
#include "ui/ui_internal.h"
#include "brain.h"

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

/* 세기를 색으로 바꾼다. 약하면 어둡고, 셀수록 파랑, 노랑, 빨강이 된다. */
static color_t heat(float v)
{
    if (v < 0.05f) return C_GRAY1;
    if (v < 0.35f) return C_BLUE;
    if (v < 0.70f) return C_YELLOW;
    return C_RED;
}

/* 초파리 뇌 표시(47x54).
 *   위쪽 그림: 뇌에 들어가는 자극. 왼쪽과 오른쪽 덩어리는 양쪽 눈(다가옴),
 *              가운데 아래의 띠는 맛(단맛 초록, 쓴맛 보라, 짠맛 하늘)이다.
 *   아래쪽 글자 칸: 뇌에서 나오는 반응. L, R 방향 전환, E 도주, B 후진, F 먹기. */
static void draw_brain(canvas_t *c, int x, int y, const float *stim, const float *rate)
{
    static const float zero[BRAIN_GROUP_COUNT];
    if (!stim) stim = zero;
    if (!rate) rate = zero;

    ui_rect(c, x, y, OTHER_W, OTHER_H, C_BLACK);
    frame_rect(c, x - 1, y - 1, OTHER_W + 1, OTHER_H + 1, C_GRAY2);
    ui_text(c, x + 9, y + 2, "BRAIN", C_GRAY3);

    /* 뇌의 몸통 */
    ui_rect(c, x + 12, y + 14, 23, 20, C_GRAY1);
    ui_rect(c, x + 14, y + 12, 19, 24, C_GRAY1);
    ui_line(c, x + 23, y + 13, x + 23, y + 34, C_BLACK);          /* 가운데 골 */

    /* 눈 쪽 덩어리: 왼쪽, 오른쪽에서 다가오는 정도 */
    ui_rect(c, x + 3,  y + 16, 8, 16, heat(stim[BR_LOOM_L]));
    ui_rect(c, x + 36, y + 16, 8, 16, heat(stim[BR_LOOM_R]));

    /* 맛 */
    color_t taste = C_GRAY2;
    float best = 0.05f;
    if (stim[BR_SUGAR]  > best) { best = stim[BR_SUGAR];  taste = C_GREEN; }
    if (stim[BR_BITTER] > best) { best = stim[BR_BITTER]; taste = C_PURPLE; }
    if (stim[BR_SALT]   > best) { best = stim[BR_SALT];   taste = C_CYAN; }
    ui_rect(c, x + 17, y + 29, 13, 5, taste);

    /* 반응 다섯 가지 */
    static const struct { int group; const char *label; } outs[] = {
        { BR_TURN_L, "L" }, { BR_TURN_R, "R" }, { BR_ESCAPE, "E" }, { BR_BACKWARD, "B" }, { BR_FEED, "F" },
    };
    for (int i = 0; i < 5; i++) {
        int bx = x + 1 + i * 9;
        color_t col = heat(rate[outs[i].group]);
        ui_rect(c, bx, y + 41, 8, 11, col);
        ui_text(c, bx + 2, y + 43, outs[i].label, col == C_YELLOW ? C_BLACK : C_WHITE);
    }
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

    /* 오른쪽 위: 다른 참가자 한 명을 절반 크기로. 누구를 볼지는 hud->watch가 정한다 */
    frame_rect(full, OTHER_X - 1, TOP_H - 1, OTHER_W + 1, OTHER_H + 1, C_GRAY2);
    if (hud->watch >= 0 && hud->watch < n_players && hud->watch != me) {
        canvas_blit_half(full, OTHER_X, TOP_H, panes[hud->watch]);
        snprintf(buf, sizeof buf, "P%d", hud->watch + 1);
        ui_text(full, OTHER_X + 2, TOP_H + 2, buf, C_WHITE);
    }

    /* 오른쪽 아래: 초파리 뇌 표시 */
    draw_brain(full, OTHER_X, TOP_H + OTHER_H, hud->brain_stim, hud->brain_rate);

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
