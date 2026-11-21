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

/* 초파리 뇌 그림(47x54). 앞에서 본 모습이고 화면의 왼쪽이 게임의 왼쪽이다.
 * 왼쪽 절반(24칸)만 적는다. 오른쪽은 뒤집어 만들고, 그때 소문자는 대문자가 된다.
 * 글자 하나가 뇌의 한 부위다. 부위의 뜻은 아래 regions 표에 있다.
 *   .  빈 곳     #  테두리     -  부위 사이의 틈     x  나머지 뇌 조직 */
#define BRAIN_HALF 24
static const char *const brain_map[OTHER_H] = {
    "........................",
    "........................",
    "...............#####....",
    ".............##kkkkk##..",
    "............#xkkkkkkkx#.",
    "...........#xxkkkkkkkxx#",
    "...........#xxxkkkkkxxxx",
    "...........#hhxxkkxxxxxx",
    "...........#hhhxkkxxxxxx",
    ".....####..#hhhxkkxxxxxx",
    "...##oooo#.#hhhxkkxfffff",
    "..#ooo-mmm#-hhxxkkffffff",
    ".#oo-mmm-ll-xxxxkkffffff",
    "#oo-mmm-lll-xxxxkkfffxxx",
    "#oo-mmm-lll-xxxxkkxxxxxx",
    "#oo-mmm-lll-xxxxkkxxxeee",
    "#oo-mmm-lll-xxxxkkxxeexx",
    "#oo-mmm-lllgggxxkkxxexxx",
    "#oo-mmm-lllgggxxkkxxeexx",
    "#oo-mmm-lll-xgxxkkxxxeee",
    "#oo-mmm-lll-xgxxkkxxxxxx",
    "#oo-mmm-lll-xgxxkkkkkkkx",
    "#oo-mmm-lll-xgxxkkkkkkkx",
    "#oo-mmm-lll-xgxxxxxxxxxx",
    "#oo-mmm-lll-xgxxxttttxxx",
    "#oo-mmm-lll-xgxxxttttxxx",
    "#oo-mmm-lll-xgxxxttttxxx",
    "#oo-mmm-lll-xgxxxxxxxxxx",
    "#oo-mmm-lll-xgxxxxxaaaxx",
    "#oo-mmm-lll-xgxxxxaaaaax",
    "#oo-mmm-lll-xgxxxaaaaaax",
    "#oo-mmm-lll-xgxxxaaaaaax",
    ".#oo-mmm-ll-xxgxxaaaaaax",
    "..#ooo-mmm#-xxxgxxaaaaax",
    "...##oooo#..#xxxgxxaaaxx",
    ".....####....#xxxgxxxx--",
    "..............#xxxgxxx--",
    "..............#xxxxgxx--",
    "..............#xxxxxgxxx",
    "..............#ssssxgxxx",
    "..............#ssssxgxpp",
    "..............#ssssxgxpp",
    "..............#xxxxxgxpp",
    "..............#bbbbxgxpp",
    "..............#bbbbxgxpp",
    "..............#bbbbxgxpp",
    "...............#xxxxgxpp",
    ".................#xxgxxx",
    "...................#gxxx",
    "...................#gxxx",
    "...................#gxxx",
    "...................#gxxx",
    "...................#gxxx",
    "...................#gxxx",
};

/* 부위마다 어느 값을 보여 주는지. is_rate가 0이면 들어가는 자극, 1이면 나오는 반응이다.
 * hot은 켜졌을 때의 색, rest는 조용할 때의 색이다. */
static const struct { char ch; uint8_t group, is_rate; color_t hot, rest; } regions[] = {
    /* 눈 뒤의 세 겹: 다가오는 것을 본다 */
    { 'o', BR_LOOM_L, 0, C_ORANGE, C_GRAY1 },    { 'O', BR_LOOM_R, 0, C_ORANGE, C_GRAY1 },
    { 'm', BR_LOOM_L, 0, C_YELLOW, C_FLY_BODY }, { 'M', BR_LOOM_R, 0, C_YELLOW, C_FLY_BODY },
    { 'l', BR_LOOM_L, 0, C_RED,    C_GRAY1 },    { 'L', BR_LOOM_R, 0, C_RED,    C_GRAY1 },
    /* 냄새: 아래 가운데의 둥근 덩어리(끌리는 냄새), 위쪽 바깥(싫은 냄새) */
    { 'a', BR_ODOR_L,  0, C_YELLOW, C_FLY_BODY }, { 'A', BR_ODOR_R,  0, C_YELLOW, C_FLY_BODY },
    { 'h', BR_REPEL_L, 0, C_PINK,   C_FLY_BODY }, { 'H', BR_REPEL_R, 0, C_PINK,   C_FLY_BODY },
    /* 맛: 맨 아래. 왼쪽이 단맛과 쓴맛, 오른쪽이 짠맛과 물 */
    { 's', BR_SUGAR,  0, C_GREEN,  C_GRAY1 },     { 'S', BR_SALT,  0, C_CYAN, C_GRAY1 },
    { 'b', BR_BITTER, 0, C_PURPLE, C_FLY_BODY },  { 'B', BR_WATER, 0, C_BLUE, C_FLY_BODY },
    /* 반응 */
    { 'k', BR_AROUSAL,  1, C_PURPLE, C_GRAY1 },    { 'K', BR_AROUSAL,  1, C_PURPLE, C_GRAY1 },     /* 버섯 모양: 각성 */
    { 'f', BR_FORWARD,  1, C_GREEN,  C_FLY_BODY }, { 'F', BR_FORWARD,  1, C_GREEN,  C_FLY_BODY },  /* 부채: 전진 */
    { 'e', BR_BACKWARD, 1, C_ORANGE, C_GRAY1 },    { 'E', BR_BACKWARD, 1, C_ORANGE, C_GRAY1 },     /* 고리: 후진 */
    { 't', BR_TURN_L,   1, C_CYAN,   C_GRAY1 },    { 'T', BR_TURN_R,   1, C_CYAN,   C_GRAY1 },     /* 방향 전환 */
    { 'p', BR_FEED,     1, C_PINK,   C_GRAY1 },    { 'P', BR_FEED,     1, C_PINK,   C_GRAY1 },     /* 먹기 */
    { 'g', BR_ESCAPE,   1, C_RED,    C_GRAY1 },    { 'G', BR_ESCAPE,   1, C_RED,    C_GRAY1 },     /* 몸으로 내려가는 굵은 줄: 도주 */
};

/* 좌표와 프레임 번호를 고르게 섞은 수. 픽셀마다 켜지는 순서와 반짝임을 정한다. */
static uint32_t mix(uint32_t x, uint32_t y, uint32_t f)
{
    uint32_t h = (x * 73856093u) ^ (y * 19349663u) ^ (f * 83492791u);
    h ^= h >> 13;
    h *= 0x5bd1e995u;
    h ^= h >> 15;
    return h;
}

/* 초파리 뇌 표시. 값이 클수록 그 부위의 픽셀이 더 많이 켜지고, 켜진 픽셀은 가끔 하얗게 반짝인다. */
static void draw_brain(canvas_t *c, int x, int y, const float *stim, const float *rate, int frame)
{
    static const float zero[BRAIN_GROUP_COUNT];
    if (!stim) stim = zero;
    if (!rate) rate = zero;

    ui_rect(c, x, y, OTHER_W, OTHER_H, C_BLACK);
    frame_rect(c, x - 1, y - 1, OTHER_W + 1, OTHER_H + 1, C_GRAY2);

    int busy = (int)(rate[BR_AROUSAL] * 15.0f);
    for (int py = 0; py < OTHER_H; py++) {
        for (int px = 0; px < OTHER_W; px++) {
            char ch = brain_map[py][px < BRAIN_HALF ? px : OTHER_W - 1 - px];
            if (px >= BRAIN_HALF && ch >= 'a' && ch <= 'z') ch = (char)(ch - 'a' + 'A');
            uint32_t now = mix((uint32_t)px, (uint32_t)py, (uint32_t)frame + 1u);

            if (ch == '#') { ui_px(c, x + px, y + py, C_GRAY2); continue; }
            if (ch == 'x' || ch == 'X') {
                /* 나머지 조직: 조용할 때도 조금씩 깜빡이고, 각성하면 파랗게 더 자주 깜빡인다 */
                color_t col = C_BG;
                if ((int)(now % 100u) < 2 + busy) col = busy > 1 ? C_BLUE : C_GRAY1;
                ui_px(c, x + px, y + py, col);
                continue;
            }
            for (size_t i = 0; i < sizeof regions / sizeof regions[0]; i++) {
                if (regions[i].ch != ch) continue;
                float v = (regions[i].is_rate ? rate : stim)[regions[i].group];
                color_t col = regions[i].rest;
                if (v >= 0.05f && (float)(mix((uint32_t)px, (uint32_t)py, 0u) % 100u) < 25.0f + 75.0f * v)
                    col = (now % 8u == 0u) ? C_WHITE : regions[i].hot;
                ui_px(c, x + px, y + py, col);
                break;
            }
        }
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
        flycam_draw(full, OTHER_X, TOP_H, hud->brain_rate, hud->ticks_left / 2);
    } else if (hud->watch >= 0 && hud->watch < n_players && hud->watch != me) {
        canvas_blit_half(full, OTHER_X, TOP_H, panes[hud->watch]);
        snprintf(buf, sizeof buf, "P%d", hud->watch + 1);
        ui_text(full, OTHER_X + 2, TOP_H + 2, buf, C_WHITE);
    }

    /* 오른쪽 아래: 초파리 뇌 표시 */
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
