/* 게임 밖의 장면 그리기. 진행자 시프봇이 말풍선으로 안내한다. */
#include "core/scenes.h"
#include "ui/ui_internal.h"

#include <stdio.h>
#include <string.h>

#define LINE_PITCH 14          /* 한글 여러 줄의 줄 간격 */
#define BUBBLE_PAD 5
#define MAX_LINES  4

static void center_text(canvas_t *c, int y, const char *s, color_t col)
{
    ui_text(c, (SCREEN_W - ui_text_w(s)) / 2, y, s, col);
}

/* 진행자 그림. 처음 쓸 때 한 번만 읽는다. */
static sprite_id_t sipbot(const char *name, sprite_id_t *slot)
{
    if (*slot == SPR_MISSING) *slot = ui_sprite_load(name);
    return *slot;
}

/* 글을 '\n'에서 나눠 줄마다 lines[i]에 복사한다. 줄 수를 돌려준다. */
static int split_lines(const char *text, char lines[MAX_LINES][96])
{
    int n = 0;
    const char *p = text ? text : "";
    while (n < MAX_LINES) {
        size_t len = strcspn(p, "\n");
        snprintf(lines[n], 96, "%.*s", (int)(len < 95 ? len : 95), p);
        n++;
        if (p[len] == '\0') break;
        p += len + 1;
    }
    return n;
}

static void bubble_size(const char *text, int *w, int *h)
{
    char lines[MAX_LINES][96];
    int n = split_lines(text, lines), widest = 0;
    for (int i = 0; i < n; i++)
        if (ui_text_w(lines[i]) > widest) widest = ui_text_w(lines[i]);
    *w = widest + 2 * BUBBLE_PAD + 2;
    *h = n * LINE_PITCH - 2 + 2 * BUBBLE_PAD + 2;
}

int scene_bubble_w(const char *text) { int w, h; bubble_size(text, &w, &h); return w; }
int scene_bubble_h(const char *text) { int w, h; bubble_size(text, &w, &h); return h; }

/* 말풍선을 그린다. hi_line 번째 줄(0부터)만 hi_ink 색으로 쓴다. 강조할 줄이 없으면 hi_line에 -1을 준다 */
static void bubble(canvas_t *c, int x, int y, const char *text, color_t ink, int tail_x, int hi_line, color_t hi_ink)
{
    char lines[MAX_LINES][96];
    int n = split_lines(text, lines), w, h;
    bubble_size(text, &w, &h);

    /* 모서리를 한 픽셀씩 깎은 상자 */
    ui_rect(c, x + 1, y, w - 2, h, C_BLACK);
    ui_rect(c, x, y + 1, w, h - 2, C_BLACK);
    ui_line(c, x + 2, y, x + w - 3, y, C_WHITE);
    ui_line(c, x + 2, y + h - 1, x + w - 3, y + h - 1, C_WHITE);
    ui_line(c, x, y + 2, x, y + h - 3, C_WHITE);
    ui_line(c, x + w - 1, y + 2, x + w - 1, y + h - 3, C_WHITE);
    ui_px(c, x + 1, y + 1, C_WHITE);
    ui_px(c, x + w - 2, y + 1, C_WHITE);
    ui_px(c, x + 1, y + h - 2, C_WHITE);
    ui_px(c, x + w - 2, y + h - 2, C_WHITE);

    /* 아래로 내려오는 꼬리. 말하는 쪽을 가리킨다 */
    if (tail_x >= x + 4 && tail_x <= x + w - 8) {
        for (int k = 0; k < 5; k++) {
            ui_line(c, tail_x + k / 2, y + h - 1 + k, tail_x + 4 - (k + 1) / 2, y + h - 1 + k, C_BLACK);
            ui_px(c, tail_x + k / 2, y + h - 1 + k, C_WHITE);
            ui_px(c, tail_x + 4 - (k + 1) / 2, y + h - 1 + k, C_WHITE);
        }
    }

    for (int i = 0; i < n; i++)
        ui_text(c, x + (w - ui_text_w(lines[i])) / 2, y + 1 + BUBBLE_PAD + i * LINE_PITCH, lines[i],
                i == hi_line ? hi_ink : ink);
}

void scene_bubble(canvas_t *c, int x, int y, const char *text, color_t ink, int tail_x)
{
    bubble(c, x, y, text, ink, tail_x, -1, ink);
}

void scene_card(canvas_t *full, const char *title, const char *rule, const char *controls, int tick)
{
    static sprite_id_t talk = SPR_MISSING;
    static const int talk_frames[] = { 0, 1, 2, 1 };        /* 말하는 입 모양: 1 > 2 > 3 > 2 */
    /* 시프봇이 규칙을 말하고 마지막 줄에 조작을 알려 준다. 조작 줄은 눈에 띄게 다른 색으로 쓴다 */
    char say[200];
    int n_rule = 1, has_keys = controls && controls[0];
    for (const char *p = (rule && rule[0]) ? rule : "..."; *p; p++) n_rule += *p == '\n';
    if (n_rule > MAX_LINES - 1) n_rule = MAX_LINES - 1;
    snprintf(say, sizeof say, "%s%s%s", (rule && rule[0]) ? rule : "...", has_keys ? "\n조작: " : "", has_keys ? controls : "");
    int bw = scene_bubble_w(say), bh = scene_bubble_h(say);
    int bx = (SCREEN_W - bw) / 2, by = 30;

    ui_clear(full, C_BG);
    center_text(full, 9, title ? title : "", C_YELLOW);
    ui_line(full, 40, 24, SCREEN_W - 41, 24, C_GRAY1);

    /* 시프봇이 왼쪽 아래에서 말하고, 말풍선의 꼬리가 그쪽으로 내려온다 */
    bubble(full, bx, by, say, C_TEXT, bx + 14 < 60 ? 60 : bx + 14, has_keys ? n_rule : -1, C_CYAN);
    ui_sprite(full, sipbot("sipbot_talk", &talk), 36, by + bh + 6, talk_frames[(tick / 4) % 4]);
}

void scene_count(canvas_t *full, int tick)
{
    static sprite_id_t count = SPR_MISSING;
    static canvas_t *small;                        /* 그림 한 장을 받아 두 배로 키우는 데 쓴다 */
    sprite_id_t id = sipbot("sipbot_count", &count);
    int k = tick < 0 ? 0 : (tick / 10 > 3 ? 3 : tick / 10);
    int w = ui_sprite_w(id), h = ui_sprite_h(id);

    ui_clear(full, C_BG);
    if (!small || small->w != w || small->h != h) {
        canvas_free(small);
        small = canvas_new(w, h);
    }
    ui_clear(small, C_BG);
    ui_sprite(small, id, 0, 0, k);

    /* 3, 2, 1, GO 그림을 두 배로 키워 가운데에 놓는다. 숫자는 그림 안에 들어 있다 */
    int ox = (SCREEN_W - 2 * w) / 2, oy = (SCREEN_H - 2 * h) / 2;
    for (int y = 0; y < h; y++)
        for (int x = 0; x < w; x++)
            ui_rect(full, ox + 2 * x, oy + 2 * y, 2, 2, small->px[y * w + x]);
}

void scene_title(canvas_t *full, int tick, int selected)
{
    static sprite_id_t idle = SPR_MISSING;
    static const char *const menu[3] = { "1 입장", "2 지난 랭킹", "3 나가기" };
    static const color_t party[] = { C_PINK, C_CYAN, C_GREEN, C_YELLOW, C_ORANGE, C_PURPLE };
    const int n_party = (int)(sizeof party / sizeof party[0]);
    const char *name = "슈퍼 시프 파티";
    const int ground = 110;

    ui_clear(full, C_BG);

    /* 간판: 둘레의 전구가 번갈아 켜진다 */
    int sw = ui_text_w(name) + 24, sx = (SCREEN_W - sw) / 2;
    ui_rect(full, sx, 4, sw, 20, C_BLACK);
    for (int x = sx; x < sx + sw; x += 4) {
        color_t on = ((x / 4 + tick / 5) % 2) ? C_YELLOW : C_GRAY1;
        ui_rect(full, x, 4, 2, 2, on);
        ui_rect(full, x, 22, 2, 2, on);
    }
    ui_text(full, sx + 12, 8, name, C_YELLOW);

    /* 파티장: 건물, 색이 바뀌는 창문 둘, 빛이 새어 나오는 문 */
    ui_rect(full, 142, 44, 90, ground - 44, C_GRAY1);
    ui_rect(full, 138, 40, 98, 5, C_GRAY2);
    for (int k = 0; k < 2; k++)
        ui_rect(full, 152 + k * 52, 54, 18, 14, party[(tick / 8 + k * 2) % n_party]);
    ui_rect(full, 174, 68, 26, ground - 68, C_BLACK);
    ui_rect(full, 176, 70, 22, ground - 70, (tick / 6) % 2 ? C_WARN : C_YELLOW);
    for (int k = 0; k < 6; k++)                                  /* 문에서 길로 퍼지는 빛 */
        ui_rect(full, 174 - 2 * k, ground + k, 26 + 4 * k, 1, k % 2 ? C_GRAY1 : C_WARN);
    ui_line(full, 0, ground, SCREEN_W - 1, ground, C_GRAY2);

    /* 시프봇과 인사말 */
    const char *hello = "어서 와요!\n파티가 곧 시작돼요";
    scene_bubble(full, 8, 30, hello, C_TEXT, 40);
    ui_sprite(full, sipbot("sipbot_idle", &idle), 30, ground - 40, (tick / 6) % 6);

    /* 메뉴 셋. 고른 것은 노랗게 */
    for (int i = 0; i < 3; i++) {
        int bx = 8 + i * 77, bw = 70;
        color_t col = i == selected ? C_YELLOW : C_GRAY2;
        ui_rect(full, bx, 118, bw, 15, C_BLACK);
        ui_line(full, bx, 118, bx + bw - 1, 118, col);
        ui_line(full, bx, 132, bx + bw - 1, 132, col);
        ui_line(full, bx, 118, bx, 132, col);
        ui_line(full, bx + bw - 1, 118, bx + bw - 1, 132, col);
        ui_text(full, bx + (bw - ui_text_w(menu[i])) / 2, 119, menu[i], i == selected ? C_YELLOW : C_GRAY3);
    }
}

void scene_message(canvas_t *full, const char *line, color_t col)
{
    ui_clear(full, C_BG);
    center_text(full, 60, line, col);
}
