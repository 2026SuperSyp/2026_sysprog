/* 합성: 칸 그림들을 전체 화면(240x134)에 배치한다.
 *
 *   위 띠 14줄   : 게임 이름, 남은 시간, 라운드
 *   가운데 108줄 : 초파리 칸 | 내 칸 | 오른쪽 위: 다른 참가자 한 명(절반 크기)
 *                                      오른쪽 아래: 초파리 뇌 표시
 *   아래 띠 12줄 : 내 점수, 초파리 점수, 남은 기회 */
#include "ui/ui_internal.h"
#include "brain.h"
#include "brain/brain_host.h"

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

/* 초파리 뇌 표시(47x54): 점 격자. 점 하나는 2x2픽셀이고 가로 16개, 세로 18개다.
 * 앞에서 본 모습이고 화면의 왼쪽이 게임의 왼쪽이다.
 * 왼쪽 절반(8개)만 적는다. 오른쪽은 뒤집어 만들고, 그때 소문자는 대문자가 된다.
 * 글자 하나가 뇌의 한 부위다. 부위의 뜻은 아래 regions 표에 있다. */
#define DOT_COLS  16
#define DOT_ROWS  18
#define BAR_TOP   6           /* 가운데 막대가 시작하는 줄 */
#define BAR_ROWS  5
static const char *const brain_map[DOT_ROWS] = {
    "..o.....",
    ".oooo...",
    ".oooo.kk",
    "ooooohkk",
    "ooooo.kk",
    "oooooaaa",
    "oooootfg",
    "oooootfg",
    "oooootfg",
    "oooootfg",
    "oooootfg",
    "ooooo...",
    ".oooossb",
    ".oooossb",
    "..oo..pp",
    ".......n",
    ".......n",
    ".......n",
};

/* 부위마다 어느 값을 보여 주는지. is_rate가 0이면 들어가는 자극, 1이면 나오는 반응이다.
 * bar가 1이면 점이 아니라 막대이고, 값만큼 아래에서부터 차오른다. */
static const struct { char ch; uint8_t group, is_rate, bar; } regions[] = {
    { 'o', BR_LOOM_L,  0, 0 }, { 'O', BR_LOOM_R,  0, 0 },     /* 양옆의 큰 덩어리: 다가오는 것을 본다 */
    { 'h', BR_REPEL_L, 0, 0 }, { 'H', BR_REPEL_R, 0, 0 },     /* 싫은 냄새 */
    { 'a', BR_ODOR_L,  0, 0 }, { 'A', BR_ODOR_R,  0, 0 },     /* 끌리는 냄새 */
    { 's', BR_SUGAR,   0, 0 }, { 'S', BR_SALT,    0, 0 },     /* 맛: 왼쪽이 단맛과 쓴맛, 오른쪽이 짠맛과 물 */
    { 'b', BR_BITTER,  0, 0 }, { 'B', BR_WATER,   0, 0 },
    { 'k', BR_AROUSAL, 1, 0 }, { 'K', BR_AROUSAL, 1, 0 },     /* 위 가운데: 각성 */
    { 't', BR_TURN_L,  1, 1 }, { 'T', BR_TURN_R,  1, 1 },     /* 막대: 방향 전환 */
    { 'f', BR_FORWARD, 1, 1 }, { 'F', BR_BACKWARD, 1, 1 },    /* 막대: 왼쪽이 전진, 오른쪽이 후진 */
    { 'g', BR_ESCAPE,  1, 1 }, { 'G', BR_ESCAPE,  1, 1 },     /* 막대: 도주 */
    { 'p', BR_FEED,    1, 0 }, { 'P', BR_FEED,    1, 0 },     /* 먹기 */
    { 'n', BR_ESCAPE,  1, 0 }, { 'N', BR_ESCAPE,  1, 0 },     /* 몸으로 내려가는 줄: 도주 */
};

/* 좌표와 프레임 번호를 고르게 섞은 수. 점마다 켜지는 순서와 반짝임을 정한다. */
static uint32_t mix(uint32_t x, uint32_t y, uint32_t f)
{
    uint32_t h = (x * 73856093u) ^ (y * 19349663u) ^ (f * 83492791u);
    h ^= h >> 13;
    h *= 0x5bd1e995u;
    h ^= h >> 15;
    return h;
}

/* 초파리 뇌 표시. 값이 클수록 그 부위의 점이 더 많이, 더 밝게 켜진다. */
static void draw_brain(canvas_t *c, int x, int y, const float *stim, const float *rate, int frame)
{
    static const float zero[BRAIN_GROUP_COUNT];
    if (!stim) stim = zero;
    if (!rate) rate = zero;

    ui_rect(c, x, y, OTHER_W, OTHER_H, C_BG);
    frame_rect(c, x - 1, y - 1, OTHER_W + 1, OTHER_H + 1, C_GRAY2);

    for (int row = 0; row < DOT_ROWS; row++) {
        for (int col = 0; col < DOT_COLS; col++) {
            char ch = brain_map[row][col < DOT_COLS / 2 ? col : DOT_COLS - 1 - col];
            if (col >= DOT_COLS / 2 && ch >= 'a' && ch <= 'z') ch = (char)(ch - 'a' + 'A');
            for (size_t i = 0; i < sizeof regions / sizeof regions[0]; i++) {
                if (regions[i].ch != ch) continue;
                float v = (regions[i].is_rate ? rate : stim)[regions[i].group];
                int px = x + col * 3, py = y + row * 3;
                if (regions[i].bar) {
                    int from_bottom = BAR_TOP + BAR_ROWS - 1 - row;
                    int lit = v >= 0.05f && v * BAR_ROWS > (float)from_bottom;
                    ui_rect(c, px, py, 2, from_bottom == 0 ? 2 : 3, lit ? C_CYAN : CX_SLATE);
                } else {
                    uint32_t order = mix((uint32_t)col, (uint32_t)row, 0u) % 100u;
                    uint32_t bright = mix((uint32_t)col, (uint32_t)row, 7u) % 100u;
                    uint32_t now = mix((uint32_t)col, (uint32_t)row, (uint32_t)frame + 9u);
                    color_t dot = CX_SLATE;
                    if (v < 0.05f) {
                        if (now % 50u == 0u) dot = CX_TEAL;                  /* 조용할 때도 가끔 깜빡인다 */
                    } else if ((float)order < 30.0f + 70.0f * v) {
                        dot = ((float)bright < 100.0f * v || now % 5u == 0u) ? C_CYAN : CX_TEAL;
                    }
                    ui_rect(c, px, py, 2, 2, dot);
                }
                break;
            }
        }
    }
}

/* 진짜 뇌일 때의 뇌 표시(47x54).
 *   위: 뇌 지도. 뉴런을 실제 위치(앞에서 본 모습)에 놓았고, 발화가 많은 칸일수록 밝다.
 *       노란 칸은 게임이 읽는 출력 뉴런이 방금 발화한 자리다.
 *   아래: 게임이 읽는 반응 다섯 가지. L, R 방향 전환, E 도주, B 후진, F 먹기. */
static void draw_brain_map(canvas_t *c, int x, int y, const brain_map_t *m, const float *rate)
{
    static const float zero[BRAIN_GROUP_COUNT];
    if (!rate) rate = zero;
    ui_rect(c, x, y, OTHER_W, OTHER_H, C_BG);
    frame_rect(c, x - 1, y - 1, OTHER_W + 1, OTHER_H + 1, C_GRAY2);

    for (int my = 0; my < m->h; my++) {
        for (int mx = 0; mx < m->w && mx < OTHER_W; mx++) {
            int k = my * m->w + mx;
            if (m->density[k] < 3) continue;                 /* 뉴런이 거의 없는 칸은 비운다 */
            float h = m->heat[k];
            color_t col = h < 0.5f ? CX_SLATE : (h < 6.0f ? CX_TEAL : (h < 25.0f ? C_CYAN : C_WHITE));
            if (m->mark[k]) col = C_YELLOW;
            ui_px(c, x + mx, y + 1 + my, col);
        }
    }

    static const struct { int group; const char *label; } bars[] = {
        { BR_TURN_L, "L" }, { BR_ESCAPE, "E" }, { BR_BACKWARD, "B" }, { BR_FEED, "F" }, { BR_TURN_R, "R" },
    };
    for (int i = 0; i < 5; i++) {
        int bx = x + i * 10, lit = (int)(rate[bars[i].group] * 16.0f + 0.5f);
        ui_rect(c, bx, y + 27, 7, 16, CX_SLATE);
        ui_rect(c, bx, y + 27 + 16 - lit, 7, lit, rate[bars[i].group] >= 0.5f ? C_CYAN : CX_TEAL);
        ui_text(c, bx + 1, y + 45, bars[i].label, C_GRAY3);
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

    /* 오른쪽 위: 다른 참가자 한 명을 절반 크기로, 또는 키보드 치는 초파리. hud->watch가 정한다 */
    frame_rect(full, OTHER_X - 1, TOP_H - 1, OTHER_W + 1, OTHER_H + 1, C_GRAY2);
    if (hud->watch == WATCH_FLY) {
        flycam_draw(full, OTHER_X, TOP_H, hud->brain_rate, hud->ticks_left / 2, fly);
    } else if (hud->watch >= 0 && hud->watch < n_players && hud->watch != me) {
        canvas_blit_half(full, OTHER_X, TOP_H, panes[hud->watch]);
        snprintf(buf, sizeof buf, "P%d", hud->watch + 1);
        ui_text(full, OTHER_X + 2, TOP_H + 2, buf, C_WHITE);
    }

    /* 오른쪽 아래: 초파리 뇌 표시 */
    if (hud->brain_map)
        draw_brain_map(full, OTHER_X, TOP_H + OTHER_H, hud->brain_map, hud->brain_rate);
    else
        draw_brain(full, OTHER_X, TOP_H + OTHER_H, hud->brain_stim, hud->brain_rate, hud->ticks_left / 2);

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
