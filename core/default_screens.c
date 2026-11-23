/* 기본 화면. 글자만 찍는다.
 * screens/ 안에 같은 이름의 함수를 만들면 그쪽이 쓰인다(약한 심볼).
 *
 * 이름과 스테이지 제목은 한글일 수 있다. 그래서 목록은 줄 간격을 한글 높이에 맞추고,
 * 이름, 점수 같은 조각을 정해진 x에 따로 찍는다(한글은 글자 수와 바이트 수가 달라서
 * 빈칸으로 폭을 맞출 수 없다). */
#include "screen.h"
#include "ui/ui_internal.h"

#include <stdio.h>

#define WEAK __attribute__((weak))

static void center(canvas_t *c, int y, const char *s, color_t col)
{
    ui_text(c, (SCREEN_W - ui_text_w(s)) / 2, y, s, col);
}

/* 목록의 한 줄에 글자열을 놓는다. 영문만 있는 조각은 한글 줄의 영문과 같은 높이로 내린다. */
static void row_text(canvas_t *c, int x, int y, const char *s, color_t col)
{
    ui_text(c, x, ui_text_h(s) > UI_FONT_H ? y : y + UI_KFONT_LATIN_DY, s, col);
}

/* n줄을 top부터 bottom 앞까지 놓을 때의 줄 간격.
 * 14픽셀이면 한글 줄 사이가 두 픽셀 비고, 12픽셀이면 위아래 줄이 맞닿는다.
 * 그래도 넘치면 영문용 간격(tight)으로 좁힌다. 그때는 한글 이름끼리 겹친다. */
static int row_pitch(int n, int top, int bottom, int tight)
{
    if (top + (n - 1) * 14 + UI_KFONT_H <= bottom) return 14;
    if (top + (n - 1) * UI_KFONT_H + UI_KFONT_H <= bottom) return UI_KFONT_H;
    return tight;
}

WEAK void screen_lobby(canvas_t *full, const lobby_state_t *s)
{
    char buf[64];
    int n = s->n < 8 ? s->n : 8;
    int pitch = row_pitch(n, 22, 120, 10);
    ui_clear(full, C_BG);
    center(full, 8, "SUPER SYSPROG PARTY", C_YELLOW);
    for (int i = 0; i < n; i++) {
        int y = 22 + i * pitch;
        color_t col = i == s->me ? C_WHITE : C_GRAY3;
        if (i == s->me) row_text(full, 30, y, ">", col);
        row_text(full, 42, y, s->p[i].name, col);
        snprintf(buf, sizeof buf, "%s%s", s->p[i].ready ? "READY" : "...", s->p[i].is_host ? "  HOST" : "");
        row_text(full, 138, y, buf, col);
    }
    if ((int)(s->t * 2) % 2 == 0)
        center(full, 120, "SPACE: READY   F: START", C_CYAN);
}

WEAK void screen_result(canvas_t *full, const round_result_t *r)
{
    char buf[64];
    int n = r->n < 8 ? r->n : 8;
    int pitch = row_pitch(n, 62, SCREEN_H, 8);
    ui_clear(full, C_BG);
    center(full, 8, r->stage_name ? r->stage_name : "", C_YELLOW);
    if (r->me >= 0 && r->me < r->n)
        center(full, 26, r->passed[r->me] ? "WIN" : "LOSE", r->passed[r->me] ? C_GOOD : C_BAD);
    snprintf(buf, sizeof buf, "FLY %.0f", (double)r->fly_score);
    center(full, 44, buf, C_TEXT);
    for (int i = 0; i < n; i++) {
        int y = 62 + i * pitch;
        color_t col = r->passed[i] ? C_GOOD : C_BAD;
        if (i == r->me) row_text(full, 20, y, ">", col);
        row_text(full, 32, y, r->name[i], col);
        snprintf(buf, sizeof buf, "%4.0f  %s LIVES %d", (double)r->score[i], r->passed[i] ? "WIN " : "LOSE", r->lives[i]);
        row_text(full, 128, y, buf, col);
    }
}

WEAK void screen_ranking(canvas_t *full, const ranking_t *r)
{
    char buf[80];
    int n = r->n < 12 ? r->n : 12;
    int pitch = row_pitch(n, 16, SCREEN_H, 9);
    ui_clear(full, C_BG);
    center(full, 4, "RANKING", C_YELLOW);
    for (int i = 0; i < n; i++) {
        int y = 16 + i * pitch;
        color_t col = i == r->me ? C_WHITE : C_GRAY3;
        snprintf(buf, sizeof buf, "%2d", i + 1);
        row_text(full, 20, y, buf, col);
        row_text(full, 38, y, r->name[i], col);
        snprintf(buf, sizeof buf, "%5d", r->total[i]);
        row_text(full, 134, y, buf, col);
        /* 스테이지 제목은 170부터 놓는다. 길어서 화면을 넘으면 점수 바로 뒤(166)까지만 왼쪽으로 당긴다.
         * 한글 6자까지 다 보이고 그보다 긴 제목은 오른쪽이 잘린다 */
        int bx = SCREEN_W - 2 - ui_text_w(r->best_stage[i]);
        row_text(full, bx > 170 ? 170 : (bx < 166 ? 166 : bx), y, r->best_stage[i], C_CYAN);
    }
}

WEAK void screen_spectate(canvas_t *full, const spectate_state_t *s)
{
    char buf[48];
    snprintf(buf, sizeof buf, "WATCHING %s", s->watching);
    ui_line(full, 0, 0, SCREEN_W - 1, 0, C_PURPLE);
    ui_line(full, 0, SCREEN_H - 1, SCREEN_W - 1, SCREEN_H - 1, C_PURPLE);
    ui_line(full, 0, 0, 0, SCREEN_H - 1, C_PURPLE);
    ui_line(full, SCREEN_W - 1, 0, SCREEN_W - 1, SCREEN_H - 1, C_PURPLE);
    if ((int)(s->t * 2) % 2 == 0)
        ui_text(full, SCREEN_W - 4 - ui_text_w(buf), 3, buf, C_PURPLE);
}
