/* 키보드를 치는 초파리(47x54). 오른쪽 위 칸에서 Tab으로 넘기면 나온다.
 * 초파리 뇌의 반응을 키 입력처럼 보여 준다. 반응이 기준(0.5)을 넘는 동안 그 키를 누르고 있다.
 *   왼쪽, 오른쪽 화살표 : 방향 전환      위 화살표 : 도주
 *   아래 화살표         : 후진           긴 키     : 먹기
 * 보여 주기용 그림이다. 초파리가 실제로 키 입력을 보내는 것은 아니다. */
#include "ui/ui_internal.h"
#include "brain.h"

#define FW      47
#define FH      54
#define HALF    24
#define BUST_Y  9          /* 아래 그림이 놓이는 첫 줄 */
#define BUST_H  22

/* 머리와 몸통의 왼쪽 절반. 오른쪽은 뒤집어 만든다.
 *   r 눈   s 눈의 그늘   b 몸   n 밝은 쪽   d 줄무늬   p 주둥이 */
static const char *const bust[BUST_H] = {
    "..................n.....",
    "...................n....",
    "..............rrrr..nbbb",
    "............rrrrrrrbbbbb",
    "...........rrrrrrrrrbbbb",
    "...........rrrrrrrrrbbbb",
    "...........rrrrrrrrrbbbb",
    "...........rrrrrrrrrbbbb",
    "...........srrrrrrrsbbbb",
    "............ssrrrrssbbbb",
    "..............ssss.dbbbb",
    "...............nbbbbbdpp",
    "..............nbbbbbbbpp",
    ".............nbbdbbbbbbb",
    ".............nbbdbbbbdbb",
    ".............nbbdbbbbdbb",
    ".............nbbdbbbbdbb",
    ".............nbbdbbbbdbb",
    "..............nbbbbbbdbb",
    "..............nbbbbbbbbb",
    "...............bbbbbbbbb",
    "................sbbbbbbb",
};

typedef struct { int x, y, w; } key_box_t;

static const key_box_t K_UP    = { 20, 36,  7 };
static const key_box_t K_LEFT  = { 11, 41,  8 };
static const key_box_t K_DOWN  = { 20, 41,  7 };
static const key_box_t K_RIGHT = { 28, 41,  8 };
static const key_box_t K_SPACE = { 11, 46, 25 };

/* 키 하나. 눌리면 한 줄 내려가고 노랗게 된다. arrow: 0 없음, 1 위, 2 아래, 3 왼쪽, 4 오른쪽 */
static void draw_key(canvas_t *c, int ox, int oy, key_box_t k, int down, int arrow)
{
    int x = ox + k.x, y = oy + k.y + (down ? 1 : 0);
    ui_rect(c, x, y, k.w, 3, down ? C_YELLOW : C_GRAY3);
    if (!down) ui_rect(c, x, y + 3, k.w, 1, C_GRAY2);          /* 키의 옆면 */
    int cx = x + k.w / 2;
    if (arrow == 1) { ui_px(c, cx, y, C_BLACK); ui_rect(c, cx - 1, y + 1, 3, 1, C_BLACK); }
    if (arrow == 2) { ui_rect(c, cx - 1, y + 1, 3, 1, C_BLACK); ui_px(c, cx, y + 2, C_BLACK); }
    if (arrow == 3) { ui_px(c, cx - 1, y + 1, C_BLACK); ui_rect(c, cx, y, 1, 3, C_BLACK); }
    if (arrow == 4) { ui_px(c, cx + 1, y + 1, C_BLACK); ui_rect(c, cx, y, 1, 3, C_BLACK); }
}

/* 다리 하나: 어깨에서 무릎을 거쳐 발끝까지. side는 왼쪽 -1, 오른쪽 +1 */
static void draw_leg(canvas_t *c, int sx, int sy, int tx, int ty, int side)
{
    int kx = (sx + tx) / 2 + side * 4, ky = (sy + ty) / 2 - 3;
    ui_line(c, sx, sy, kx, ky, C_BROWN);
    ui_line(c, kx, ky, tx, ty, C_BROWN);
    ui_px(c, tx, ty, C_BLACK);
}

void flycam_draw(canvas_t *c, int ox, int oy, const float *rate, int frame)
{
    static const float zero[BRAIN_GROUP_COUNT];
    if (!rate) rate = zero;
    int left  = rate[BR_TURN_L]   >= 0.5f, right = rate[BR_TURN_R] >= 0.5f;
    int up    = rate[BR_ESCAPE]   >= 0.5f, down  = rate[BR_BACKWARD] >= 0.5f;
    int space = rate[BR_FEED]     >= 0.5f;
    int typing = left || right || up || down || space;
    int bob = typing ? (frame & 1) : ((frame / 8) & 1);         /* 칠 때는 빠르게, 쉴 때는 천천히 끄덕인다 */

    /* 벽과 책상 */
    ui_rect(c, ox, oy, FW, FH, C_BLACK);
    ui_rect(c, ox, oy + 32, FW, FH - 32, C_BG);

    /* 키보드: 가까운 쪽이 넓다 */
    for (int y = 34; y <= 51; y++) {
        int hw = 15 + (y - 34) * 5 / 17;
        ui_rect(c, ox + 23 - hw, oy + y, 2 * hw + 1, 1, y == 34 ? C_GRAY2 : C_GRAY1);
    }
    ui_rect(c, ox + 3, oy + 52, 41, 1, C_FLY_BODY);              /* 키보드의 앞면 */
    draw_key(c, ox, oy, K_UP, up, 1);
    draw_key(c, ox, oy, K_LEFT, left, 3);
    draw_key(c, ox, oy, K_DOWN, down, 2);
    draw_key(c, ox, oy, K_RIGHT, right, 4);
    draw_key(c, ox, oy, K_SPACE, space, 0);

    /* 날개: 몸 뒤에서 양옆으로. 도주할 때 파닥인다 */
    int flap = up ? (frame & 1) * 3 : 0;
    for (int i = 0; i < 6; i++) {
        color_t col = (i == 0 || i == 5) ? C_GRAY3 : C_WING;
        ui_line(c, ox + 14, oy + 22 + bob + i, ox + 2 + i / 2, oy + 25 + i - flap, col);
        ui_line(c, ox + 32, oy + 22 + bob + i, ox + 44 - i / 2, oy + 25 + i - flap, col);
    }

    /* 가운뎃다리: 책상을 짚고 있다 */
    ui_line(c, ox + 16, oy + 29 + bob, ox + 8,  oy + 33, C_BROWN);
    ui_line(c, ox + 30, oy + 29 + bob, ox + 38, oy + 33, C_BROWN);

    /* 머리와 몸통 */
    for (int y = 0; y < BUST_H; y++) {
        for (int x = 0; x < FW; x++) {
            char ch = bust[y][x < HALF ? x : FW - 1 - x];
            color_t col = C_NONE;
            switch (ch) {
            case 'r': col = C_FLY_EYE;  break;
            case 's': col = C_BROWN;    break;
            case 'b': col = C_FLY_BODY; break;
            case 'n': col = x < HALF ? C_BROWN : C_FLY_BODY; break;   /* 빛은 왼쪽 위에서 온다 */
            case 'd': col = C_GRAY1;    break;
            case 'p': col = C_WARN;     break;
            }
            ui_px(c, ox + x, oy + BUST_Y + bob + y, col);
        }
    }
    /* 눈의 반짝임 */
    for (int e = 0; e < 2; e++) {
        int ex = ox + (e ? 29 : 13), ey = oy + BUST_Y + bob + 4;
        ui_rect(c, ex, ey, 2, 2, C_PINK);
        ui_px(c, ex, ey, C_WHITE);
    }

    /* 앞다리: 누르는 키로 뻗는다. 왼다리는 왼쪽, 위, 긴 키. 오른다리는 오른쪽, 아래, 긴 키 */
    int lx = 16, ly = 38, rx = 30, ry = 38;
    if (left)        { lx = 14; ly = 43; }
    else if (up)     { lx = 22; ly = 38; }
    else if (space)  { lx = 17; ly = 48; }
    if (right)       { rx = 32; ry = 43; }
    else if (down)   { rx = 24; ry = 43; }
    else if (space)  { rx = 29; ry = 48; }
    draw_leg(c, ox + 16, oy + 26 + bob, ox + lx, oy + ly, -1);
    draw_leg(c, ox + 30, oy + 26 + bob, ox + rx, oy + ry, +1);

    ui_text(c, ox + 2, oy + 2, "FLY", C_WHITE);
}
