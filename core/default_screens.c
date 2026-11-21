/* 기본 화면. 글자만 찍는다.
 * screens/ 안에 같은 이름의 함수를 만들면 그쪽이 쓰인다(약한 심볼). */
#include "screen.h"

#include <stdio.h>

#define WEAK __attribute__((weak))

static void center(canvas_t *c, int y, const char *s, color_t col)
{
    ui_text(c, (SCREEN_W - ui_text_w(s)) / 2, y, s, col);
}

WEAK void screen_lobby(canvas_t *full, const lobby_state_t *s)
{
    char buf[64];
    ui_clear(full, C_BG);
    center(full, 8, "SUPER SYSPROG PARTY", C_YELLOW);
    for (int i = 0; i < s->n && i < 8; i++) {
        snprintf(buf, sizeof buf, "%c %-15s %s%s", i == s->me ? '>' : ' ', s->p[i].name,
                 s->p[i].ready ? "READY" : "...", s->p[i].is_host ? "  HOST" : "");
        ui_text(full, 30, 30 + i * 10, buf, i == s->me ? C_WHITE : C_GRAY3);
    }
    if ((int)(s->t * 2) % 2 == 0)
        center(full, 120, "SPACE: READY   F: START", C_CYAN);
}

WEAK void screen_result(canvas_t *full, const round_result_t *r)
{
    char buf[64];
    ui_clear(full, C_BG);
    center(full, 8, r->stage_name ? r->stage_name : "", C_YELLOW);
    if (r->me >= 0 && r->me < r->n)
        center(full, 26, r->passed[r->me] ? "WIN" : "LOSE", r->passed[r->me] ? C_GOOD : C_BAD);
    snprintf(buf, sizeof buf, "FLY %.0f", (double)r->fly_score);
    center(full, 44, buf, C_TEXT);
    for (int i = 0; i < r->n && i < 8; i++) {
        snprintf(buf, sizeof buf, "%c %-15s %4.0f  %s  LIVES %d", i == r->me ? '>' : ' ', r->name[i],
                 (double)r->score[i], r->passed[i] ? "WIN " : "LOSE", r->lives[i]);
        ui_text(full, 20, 62 + i * 10, buf, r->passed[i] ? C_GOOD : C_BAD);
    }
}

WEAK void screen_ranking(canvas_t *full, const ranking_t *r)
{
    char buf[80];
    ui_clear(full, C_BG);
    center(full, 4, "RANKING", C_YELLOW);
    for (int i = 0; i < r->n && i < 12; i++) {
        snprintf(buf, sizeof buf, "%2d %-15s %5d", i + 1, r->name[i], r->total[i]);
        ui_text(full, 20, 16 + i * 9, buf, i == r->me ? C_WHITE : C_GRAY3);
        ui_text(full, 170, 16 + i * 9, r->best_stage[i], C_CYAN);
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
