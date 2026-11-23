/* 게임 밖의 장면 그리기. */
#include "core/scenes.h"

#include <stdio.h>
#include <string.h>

static void center_text(canvas_t *c, int y, const char *s, color_t col)
{
    ui_text(c, (SCREEN_W - ui_text_w(s)) / 2, y, s, col);
}

void scene_card(canvas_t *full, const char *title, const char *rule, const char *controls)
{
    char line[128];
    ui_clear(full, C_BG);
    center_text(full, 30, title ? title : "", C_YELLOW);
    snprintf(line, sizeof line, "%s", rule ? rule : "");
    char *second = strchr(line, '\n');
    if (second) *second++ = '\0';
    center_text(full, 56, line, C_TEXT);
    if (second) center_text(full, 70, second, C_TEXT);
    center_text(full, 96, controls ? controls : "", C_CYAN);
}

void scene_count(canvas_t *full, int tick)
{
    static const char *const words[] = { "3", "2", "1", "GO!" };
    int k = tick < 0 ? 0 : (tick / 10 > 3 ? 3 : tick / 10);
    ui_clear(full, C_BG);
    center_text(full, 60, words[k], k == 3 ? C_GOOD : C_YELLOW);
}

void scene_message(canvas_t *full, const char *line, color_t col)
{
    ui_clear(full, C_BG);
    center_text(full, 60, line, col);
}
