/* 보스전 그리기. 화면은 위에서부터 상태 줄(초파리 체력, 남은 시간), 3D 화면, 내 상태 줄이다.
 *
 * 벽: 세로줄마다 광선의 방향을 정하고, 격자의 칸 경계를 하나씩 건너가며 처음 만나는 벽을 찾는다.
 *     벽까지의 거리는 눈에서 화면에 수직으로 잰다(비스듬히 재면 벽이 둥글게 휜다). 줄의 높이는 거리에 반비례한다.
 *     세로줄마다 그 거리를 적어 두고(zbuf), 뒤에 그리는 것들이 벽에 가려지는지 판단하는 데 쓴다.
 * 초파리와 참가자: 눈을 기준으로 한 좌표로 옮겨 화면의 가로 위치와 크기를 정하고, 먼 것부터 그린다. */
#include "boss/boss_view.h"
#include "ui/ui_internal.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

#define VY1     14                   /* 3D 화면의 위쪽 끝(배율 1일 때) */
#define VH1     104                  /* 3D 화면의 높이(배율 1일 때) */
#define PLANE   0.66f                /* 시야의 넓이(약 66도) */
#define TWO_PI  6.2831853f

static uint8_t wall[BOSS_MAP][BOSS_MAP];
static int     have_map;
static float   zbuf[SCREEN_W * PRESENT_MAX_SCALE];
/* 지금 그리는 3D 화면의 위쪽 끝, 너비, 높이(픽셀). 배율 1이면 14, 240, 104이고 set_view로 바꾼다 */
static int     VY = VY1, VW = SCREEN_W, VH = VH1;

static void set_view(int scale)
{
    VY = VY1 * scale;
    VW = SCREEN_W * scale;
    VH = VH1 * scale;
}
static const color_t pcol[BOSS_PLAYERS] = { C_CYAN, C_GREEN, C_PINK };

/* 벽의 배치는 계산부가 정한다. 한 번만 받아 온다 */
static void load_map(void)
{
    static boss_state_t tmp;
    boss_cfg_t c;
    boss_cfg_default(&c);
    boss_init(&tmp, &c, 0, 1, NULL);
    for (int y = 0; y < BOSS_MAP; y++)
        for (int x = 0; x < BOSS_MAP; x++) wall[y][x] = (uint8_t)boss_solid(&tmp, x, y);
    have_map = 1;
}

static int solid(int cx, int cy)
{
    return cx < 0 || cy < 0 || cx >= BOSS_MAP || cy >= BOSS_MAP || wall[cy][cx];
}

static void px_view(canvas_t *c, int x, int y, color_t col)
{
    if (x >= 0 && x < VW && y >= VY && y < VY + VH) ui_px(c, x, y, col);
}

/* ---- 벽 ---------------------------------------------------------------------------- */
static color_t wall_color(int pillar, int side, float d, int seam)
{
    static const color_t outer[2][3]  = { { C_GRAY3, C_GRAY2, C_GRAY2 }, { C_GRAY2, C_GRAY1, C_GRAY1 } };
    static const color_t column[2][3] = { { CX_TAN, CX_CLAY, C_BROWN }, { CX_CLAY, C_BROWN, CX_MAROON } };
    int far = d < 3.5f ? 0 : (d < 8.0f ? 1 : 2);
    if (seam && far < 2) far++;                      /* 칸의 이음매는 한 단계 어둡게 */
    return pillar ? column[side][far] : outer[side][far];
}

static void draw_walls(canvas_t *c, float px, float py, float dx, float dy)
{
    float plx = -dy * PLANE, ply = dx * PLANE;
    ui_rect(c, 0, VY, VW, VH / 2, C_BG);                         /* 천장 */
    ui_rect(c, 0, VY + VH / 2, VW, VH - VH / 2, CX_SLATE);       /* 바닥 */
    for (int x = 0; x < VW; x++) {
        float cam = 2.0f * (float)x / (float)VW - 1.0f;
        float rx = dx + plx * cam, ry = dy + ply * cam;
        int mx = (int)px, my = (int)py, sx, sy, side = 0;
        float ddx = fabsf(rx) < 1e-6f ? 1e6f : fabsf(1.0f / rx), ddy = fabsf(ry) < 1e-6f ? 1e6f : fabsf(1.0f / ry);
        float sdx, sdy;
        if (rx < 0) { sx = -1; sdx = (px - (float)mx) * ddx; } else { sx = 1; sdx = ((float)mx + 1.0f - px) * ddx; }
        if (ry < 0) { sy = -1; sdy = (py - (float)my) * ddy; } else { sy = 1; sdy = ((float)my + 1.0f - py) * ddy; }
        for (int k = 0; k < 2 * BOSS_MAP; k++) {
            if (sdx < sdy) { sdx += ddx; mx += sx; side = 0; }
            else           { sdy += ddy; my += sy; side = 1; }
            if (solid(mx, my)) break;
        }
        float d = side == 0 ? sdx - ddx : sdy - ddy;
        if (d < 0.05f) d = 0.05f;
        zbuf[x] = d;
        float at = side == 0 ? py + d * ry : px + d * rx;        /* 벽의 어디에 닿았는가(이음매를 긋는 데 쓴다) */
        at -= floorf(at);
        int h = (int)((float)VH / d), y0 = VY + VH / 2 - h / 2, y1 = y0 + h;
        if (y0 < VY) y0 = VY;
        if (y1 > VY + VH) y1 = VY + VH;
        int pillar = mx > 0 && my > 0 && mx < BOSS_MAP - 1 && my < BOSS_MAP - 1;
        ui_rect(c, x, y0, 1, y1 - y0, wall_color(pillar, side, d, at < 0.04f || at > 0.96f));
    }
}

/* ---- 세워 그리는 것들 ---------------------------------------------------------------- */
typedef struct { float x, y, depth, side; int kind, idx; } thing_t;   /* kind 0: 초파리, 1: 참가자, 2: 끈끈이 */

/* 초파리의 한 점. u, v는 -1~1(가운데가 0, v는 아래가 +). 색이 없으면 C_NONE.
 * face: 0 정면(눈 둘), 1 뒤(날개가 등을 덮음), 2 옆(turn이 -1이면 왼쪽을 본다) */
static color_t fly_pixel(float u, float v, int face, int turn, color_t body, int stuck)
{
    float au = fabsf(u);
    if (stuck && v > 0.62f && au < 0.7f && (u * u) / 0.49f + (v - 0.8f) * (v - 0.8f) / 0.04f < 1.0f) return C_ORANGE;
    if (face == 2) {                                  /* 옆모습: 머리가 한쪽 끝에 있다 */
        float hu = u - 0.55f * (float)turn, bu = u + 0.1f * (float)turn;
        if (hu * hu / 0.06f + (v + 0.1f) * (v + 0.1f) / 0.06f < 1.0f) {
            float eu = hu - 0.06f * (float)turn;
            return eu * eu + (v + 0.14f) * (v + 0.14f) < 0.02f ? C_FLY_EYE : C_BROWN;
        }
        if (bu * bu / 0.36f + (v - 0.12f) * (v - 0.12f) / 0.09f < 1.0f) return body;
        float wu = u + 0.25f * (float)turn;
        if (wu * wu / 0.3f + (v + 0.22f) * (v + 0.22f) / 0.03f < 1.0f) return C_WING;
        if (v > 0.35f && v < 0.75f && fabsf(fmodf(au * 5.0f, 1.0f) - 0.5f) < 0.08f && au < 0.55f) return C_BLACK;
        return C_NONE;
    }
    /* 정면과 뒤: 머리, 몸통, 날개, 다리 */
    if (face == 0) {
        float e = (au - 0.17f) * (au - 0.17f) + (v + 0.45f) * (v + 0.45f);
        if (e < 0.017f) return C_FLY_EYE;
        if (u * u + (v + 0.42f) * (v + 0.42f) < 0.068f) return C_BROWN;
    }
    if (u * u / 0.14f + (v - 0.15f) * (v - 0.15f) / 0.25f < 1.0f) {
        if (face == 1 && u * u / 0.09f + (v - 0.1f) * (v - 0.1f) / 0.3f < 1.0f)
            return (((int)(u * 40.0f) + (int)(v * 40.0f)) & 1) ? C_WING : body;       /* 등을 덮은 날개 */
        if (v > 0.2f && fmodf(v * 6.0f, 1.0f) < 0.18f) return C_BLACK;              /* 배의 줄무늬 */
        return body;
    }
    if (face == 1 && u * u + (v + 0.42f) * (v + 0.42f) < 0.05f) return C_BROWN;
    if ((au - 0.55f) * (au - 0.55f) / 0.18f + (v + 0.15f) * (v + 0.15f) / 0.05f < 1.0f) return C_WING;
    if (au > 0.3f && au < 0.8f && fabsf(v - (0.3f + au * 0.45f)) < 0.035f) return C_BLACK;   /* 다리 */
    if (au > 0.3f && au < 0.7f && fabsf(v - (0.05f + au * 0.5f)) < 0.03f) return C_BLACK;
    return C_NONE;
}

static void draw_fly(canvas_t *c, const boss_snap_t *b, const thing_t *t, float eye_x, float eye_y)
{
    float scale = (float)VH / t->depth;                           /* 한 칸이 화면에서 몇 픽셀인가 */
    int half = (int)(0.6f * scale), cx = (int)((float)VW / 2.0f * (1.0f + t->side / t->depth));
    int floor_y = VY + VH / 2 + (int)(0.5f * scale), cy = floor_y - (int)(0.55f * scale);
    if (half < 2) half = 2;
    if (b->fly.air) {                                             /* 떠 있다: 바닥에 그림자를 남기고 위로 올린다 */
        for (int x = cx - half * 2 / 3; x <= cx + half * 2 / 3; x++)
            if (x >= 0 && x < VW && t->depth < zbuf[x]) ui_rect(c, x, floor_y - 1 > VY ? floor_y - 1 : VY, 1, VH / 52, C_BLACK);
        cy -= (int)(0.55f * scale);
    }
    /* 초파리가 나를 보고 있는가, 등을 돌렸는가, 옆을 보는가 */
    float fa = (float)b->fly.ang * TWO_PI / 65536.0f, hx = cosf(fa), hy = sinf(fa);
    float tx = eye_x - t->x, ty = eye_y - t->y, len = sqrtf(tx * tx + ty * ty);
    float front = len > 1e-4f ? (hx * tx + hy * ty) / len : 1.0f, cross = len > 1e-4f ? (hx * ty - hy * tx) / len : 0.0f;
    int face = front > 0.5f ? 0 : (front < -0.5f ? 1 : 2), turn = cross > 0.0f ? -1 : 1;
    color_t body = b->fly.stun ? C_GRAY2 : C_FLY_BODY;
    if ((b->fly.windup || b->fly.charging) && (b->tick / 2) % 2) body = C_RED;          /* 돌진하려 한다 */
    for (int x = cx - half; x <= cx + half; x++) {
        if (x < 0 || x >= VW || t->depth >= zbuf[x]) continue;
        for (int y = cy - half; y <= cy + half; y++) {
            color_t col = fly_pixel((float)(x - cx) / (float)half, (float)(y - cy) / (float)half, face, turn, body, b->fly.stun > 0);
            if (col != C_NONE) px_view(c, x, y, col);
        }
    }
}

static void draw_player(canvas_t *c, const boss_snap_t *b, const thing_t *t)
{
    float scale = (float)VH / t->depth;
    int cx = (int)((float)VW / 2.0f * (1.0f + t->side / t->depth)), floor_y = VY + VH / 2 + (int)(0.5f * scale);
    int w = (int)(0.4f * scale), h = (int)(0.8f * scale), head = (int)(0.13f * scale);
    color_t col = pcol[t->idx % BOSS_PLAYERS];
    if (w < 2) w = 2;
    if (!b->p[t->idx].alive) { h = (int)(0.1f * scale) + 1; col = C_GRAY2; head = 0; }   /* 쓰러졌다 */
    for (int x = cx - w / 2; x <= cx + w / 2; x++) {
        if (x < 0 || x >= VW || t->depth >= zbuf[x]) continue;
        for (int y = floor_y - h; y < floor_y; y++) px_view(c, x, y, col);
        for (int y = floor_y - h - 2 * head; y < floor_y - h; y++)
            if ((x - cx) * (x - cx) + (y - (floor_y - h - head)) * (y - (floor_y - h - head)) <= head * head) px_view(c, x, y, CX_TAN);
    }
}

static void draw_trap(canvas_t *c, const thing_t *t)
{
    float scale = (float)VH / t->depth;
    int cx = (int)((float)VW / 2.0f * (1.0f + t->side / t->depth)), cy = VY + VH / 2 + (int)(0.1f * scale);
    int r = (int)(0.12f * scale) + 1;
    for (int x = cx - r; x <= cx + r; x++) {
        if (x < 0 || x >= VW || t->depth >= zbuf[x]) continue;
        for (int y = cy - r; y <= cy + r; y++)
            if ((x - cx) * (x - cx) + (y - cy) * (y - cy) <= r * r) px_view(c, x, y, C_ORANGE);
    }
}

static void draw_things(canvas_t *c, const boss_snap_t *b, int eye, float px, float py, float dx, float dy)
{
    thing_t list[1 + BOSS_PLAYERS + BOSS_PROJ];
    float plx = -dy * PLANE, ply = dx * PLANE, inv = 1.0f / (plx * dy - dx * ply);
    int n = 0;
    list[n++] = (thing_t){ (float)b->fly.x / BOSS_FIX, (float)b->fly.y / BOSS_FIX, 0, 0, 0, 0 };
    for (int i = 0; i < b->n_players && i < BOSS_PLAYERS; i++)
        if (i != eye) list[n++] = (thing_t){ (float)b->p[i].x / BOSS_FIX, (float)b->p[i].y / BOSS_FIX, 0, 0, 1, i };
    for (int k = 0; k < b->n_proj && k < BOSS_PROJ; k++)
        list[n++] = (thing_t){ (float)b->proj[k].x / BOSS_FIX, (float)b->proj[k].y / BOSS_FIX, 0, 0, 2, k };
    for (int i = 0; i < n; i++) {                    /* 눈 기준 좌표: side는 오른쪽으로, depth는 앞으로 */
        float rx = list[i].x - px, ry = list[i].y - py;
        list[i].side = inv * (dy * rx - dx * ry);
        list[i].depth = inv * (-ply * rx + plx * ry);
    }
    for (int i = 1; i < n; i++)                      /* 먼 것부터 */
        for (int j = i; j > 0 && list[j].depth > list[j - 1].depth; j--) {
            thing_t tmp = list[j]; list[j] = list[j - 1]; list[j - 1] = tmp;
        }
    for (int i = 0; i < n; i++) {
        if (list[i].depth < 0.2f) continue;          /* 등 뒤에 있다 */
        if (list[i].kind == 0) draw_fly(c, b, &list[i], px, py);
        else if (list[i].kind == 1) draw_player(c, b, &list[i]);
        else draw_trap(c, &list[i]);
    }
}

/* ---- 손에 든 무기, 작은 지도, 상태 줄 -------------------------------------------------- */
static void draw_weapon(canvas_t *c, const boss_snap_t *b, int me)
{
    int item = b->p[me].item, used = b->p[me].cool >= 6;         /* 방금 썼다 */
    int bx = VW / 2 + 34, by = VY + VH - (used ? 44 : 30);
    if (item == BOSS_SWATTER) {
        ui_rect(c, bx + 8, by + 18, 3, 30, C_BROWN);
        ui_rect(c, bx, by, 20, 20, C_GRAY3);
        for (int k = 3; k < 20; k += 4) { ui_rect(c, bx + k, by, 1, 20, C_GRAY1); ui_rect(c, bx, by + k, 20, 1, C_GRAY1); }
    } else if (item == BOSS_SPRAY) {
        ui_rect(c, bx + 4, by + 8, 12, 30, C_CYAN);
        ui_rect(c, bx + 7, by + 2, 6, 6, C_WHITE);
        if (used) for (int k = 0; k < 14; k++) ui_px(c, bx + 6 - k * 3, by + 4 - k, (k & 1) ? C_WHITE : C_CYAN);
    } else {
        for (int y = -7; y <= 7; y++)
            for (int x = -9; x <= 9; x++)
                if (x * x * 49 + y * y * 81 <= 49 * 81) px_view(c, bx + 10 + x, by + 14 + y, C_ORANGE);
    }
}

static void draw_minimap(canvas_t *c, const boss_snap_t *b, int me)
{
    const int cell = 2, ox = VW - BOSS_MAP * cell - 2, oy = VY + 2;
    ui_rect(c, ox - 1, oy - 1, BOSS_MAP * cell + 2, BOSS_MAP * cell + 2, C_BLACK);
    for (int y = 0; y < BOSS_MAP; y++)
        for (int x = 0; x < BOSS_MAP; x++)
            if (wall[y][x]) ui_rect(c, ox + x * cell, oy + y * cell, cell, cell, C_GRAY2);
    ui_rect(c, ox + b->fly.x * cell / BOSS_FIX - 1, oy + b->fly.y * cell / BOSS_FIX - 1, 3, 3,
            b->fly.stun ? C_ORANGE : (b->fly.air ? C_YELLOW : C_RED));
    for (int i = 0; i < b->n_players && i < BOSS_PLAYERS; i++) {
        int x = ox + b->p[i].x * cell / BOSS_FIX, y = oy + b->p[i].y * cell / BOSS_FIX;
        float a = (float)b->p[i].dir * TWO_PI / BOSS_DIRS;
        if (!b->p[i].alive) continue;
        ui_rect(c, x - 1, y - 1, 2, 2, i == me ? C_WHITE : pcol[i]);
        ui_px(c, x + (int)(cosf(a) * 3.0f), y + (int)(sinf(a) * 3.0f), i == me ? C_WHITE : pcol[i]);
    }
}

/* ---- 키 안내 ---------------------------------------------------------------------- */
/* 글자열 하나를 한글 줄의 높이에 맞춰 놓는다. 영문만 있는 조각은 한글이 섞인 조각 안의 영문과 같은 높이로
 * 내려서 놓는다. 그린 폭을 돌려준다 */
static int put_text(canvas_t *c, int x, int y, const char *s, color_t col)
{
    ui_text(c, x, ui_text_h(s) > UI_FONT_H ? y : y + UI_KFONT_LATIN_DY, s, col);
    return ui_text_w(s);
}

typedef struct { const char *key, *what; } key_hint_t;

/* 키는 노랗게, 설명은 흰색으로 한 줄에 이어 놓는다. key가 NULL인 항목에서 끝난다 */
static void put_hints(canvas_t *c, int x, int y, const key_hint_t *h)
{
    for (; h->key; h++) {
        x += put_text(c, x, y, h->key, C_YELLOW) + 4;
        x += put_text(c, x, y, h->what, C_TEXT) + 10;
    }
}

/* 말풍선 상자. core/scenes.c의 말풍선과 같은 모양이다(모서리를 한 픽셀 깎은 검은 상자에 흰 테두리,
 * 아래로 내려오는 꼬리). boss/는 core/를 쓰지 않으므로 여기서 따로 그린다 */
static void speech_box(canvas_t *c, int x, int y, int w, int h, int tail_x)
{
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
    for (int k = 0; k < 5; k++) {
        ui_line(c, tail_x + k / 2, y + h - 1 + k, tail_x + 4 - (k + 1) / 2, y + h - 1 + k, C_BLACK);
        ui_px(c, tail_x + k / 2, y + h - 1 + k, C_WHITE);
        ui_px(c, tail_x + 4 - (k + 1) / 2, y + h - 1 + k, C_WHITE);
    }
}

/* 시프봇의 키 안내. 화면 전체를 쓴다. 보스전이 시작하기 전(서버가 잠깐 기다려 준다)과 H를 눌렀을 때 띄운다.
 * 진행자 시프봇이 말풍선으로 키를 전부 알려 주고, 옆에 한마디(살금살금 다가가라)를 덧붙인다.
 * 조작이 바뀌면 여기와 draw_legend, 상태 줄의 숫자, docs/guide.md의 "보스전 조작"을 같이 고친다 */
static void draw_guide(canvas_t *c, int intro, int age_ms)
{
    static sprite_id_t cat = SPR_MISSING;
    static const key_hint_t rows[][4] = {
        { { "W S", "앞뒤" }, { "A D", "돌기" }, { "SHIFT+W", "살금살금" }, { NULL, NULL } },
        { { "1", "파리채" }, { "2", "스프레이" }, { "3", "끈끈이" }, { NULL, NULL } },
        { { "F", "다음 무기" }, { "SPACE", "무기 쓰기" }, { NULL, NULL } },
        { { "TAB", "쓰러진 뒤 다른 사람 보기" }, { NULL, NULL } },
        { { "Q", "나가기" }, { "H", "이 안내 다시 보기" }, { NULL, NULL } },
    };
    const int n = (int)(sizeof rows / sizeof rows[0]), bx = 6, by = 18, bw = 228, bh = 10 + n * 13;
    const char *right = intro ? "곧 시작해요" : "H 닫기";
    if (cat == SPR_MISSING) cat = ui_sprite_load("sipbot_guide");

    ui_rect(c, 0, 0, SCREEN_W, SCREEN_H, C_BG);
    put_text(c, 8, 3, "보스전 조작", C_YELLOW);
    put_text(c, SCREEN_W - 8 - ui_text_w(right), 3, right, C_CYAN);

    speech_box(c, bx, by, bw, bh, 34);
    for (int i = 0; i < n; i++) put_hints(c, bx + 8, by + 5 + i * 13, rows[i]);

    /* 시프봇은 말풍선 아래 왼쪽에 서고, 그 옆에 가장 중요한 한마디를 둔다 */
    int cy = SCREEN_H - ui_sprite_h(cat), frames = ui_sprite_frames(cat);
    ui_sprite(c, cat, 10, cy, frames > 0 ? (age_ms < 0 ? 0 : age_ms / 300) % frames : 0);
    put_text(c, 72, by + bh + 8, "초파리는 다가가면 달아나요", C_TEXT);
    put_text(c, 72, by + bh + 22, "SHIFT로 살금살금 다가가요", C_CYAN);
}

/* 싸우는 동안 3D 화면의 왼쪽 아래에 늘 보이는 짧은 안내. 무기를 고르는 숫자 키는 아래 상태 줄에 적혀 있다 */
static void draw_legend(canvas_t *c)
{
    static const key_hint_t a[] = { { "WASD", "이동" }, { "SPACE", "쓰기" }, { NULL, NULL } };
    static const key_hint_t b[] = { { "SHIFT", "살금살금" }, { "H", "키 안내" }, { NULL, NULL } };
    ui_rect(c, 0, VY + VH - 29, 150, 29, C_BLACK);
    put_hints(c, 3, VY + VH - 27, a);
    put_hints(c, 3, VY + VH - 14, b);
}

static void draw_bars(canvas_t *c, const boss_snap_t *b, const boss_view_t *v, int eye)
{
    static const char *item_name[BOSS_ITEMS] = { "파리채", "스프레이", "끈끈이" };
    char line[64];
    ui_rect(c, 0, 0, SCREEN_W, VY, C_BLACK);
    ui_rect(c, 0, VY + VH, SCREEN_W, SCREEN_H - VY - VH, C_BLACK);
    ui_text(c, 2, 1, "초파리", C_TEXT);
    ui_rect(c, 42, 4, 122, 6, C_GRAY1);
    ui_rect(c, 43, 5, b->fly.hp_max > 0 ? 120 * b->fly.hp / b->fly.hp_max : 0, 4, C_RED);
    snprintf(line, sizeof line, "%d/%d", b->fly.hp, b->fly.hp_max);
    ui_text(c, 168, 3, line, C_TEXT);
    snprintf(line, sizeof line, "%d", b->ticks_left / 20);
    ui_text(c, SCREEN_W - 2 - ui_text_w(line), 3, line, b->ticks_left < 600 ? C_WARN : C_TEXT);

    int y = VY + VH + 2;
    if (eye < 0) { ui_text(c, 2, y, "모두 쓰러졌다", C_GRAY3); return; }
    if (eye != v->me) {
        snprintf(line, sizeof line, "관전 %.10s   Tab 다른 사람", v->name[eye] ? v->name[eye] : "");
        ui_text(c, 2, y, line, pcol[eye]);
        return;
    }
    /* 체력(빨간 칸), 그리고 무기 셋. 이름 앞의 숫자가 그 무기를 고르는 키다 */
    int hp = b->p[eye].hp < 5 ? b->p[eye].hp : 5;
    for (int k = 0; k < hp; k++) ui_rect(c, 2 + k * 8, y + 3, 6, 6, C_BAD);
    int x = 2 + (hp > 3 ? hp : 3) * 8 + 4;
    for (int k = 0; k < BOSS_ITEMS; k++) {
        const char key[2] = { (char)('1' + k), '\0' };               /* 무기 k를 고르는 키는 SK_1 + k다 */
        int on = k == b->p[eye].item;
        if (k == BOSS_SPRAY) snprintf(line, sizeof line, "%s %d", item_name[k], b->p[eye].ammo);
        else if (k == BOSS_TRAP) snprintf(line, sizeof line, "%s %d", item_name[k], b->p[eye].traps);
        else snprintf(line, sizeof line, "%s", item_name[k]);
        x += put_text(c, x, y, key, on ? C_WHITE : C_GRAY3) + 4;
        x += put_text(c, x, y, line, on ? C_YELLOW : C_GRAY2) + 6;
    }
}

/* 보스전이 끝났으면 가운데에 결과를 알린다. 서버는 끝난 상태를 잠깐 그대로 둔 뒤 랭킹으로 넘어간다 */
static void draw_outcome(canvas_t *c, const boss_snap_t *b)
{
    if (b->outcome == BOSS_GOING) return;
    int win = b->outcome == BOSS_PLAYERS_WIN, alive = 0;
    for (int i = 0; i < b->n_players && i < BOSS_PLAYERS; i++) alive += b->p[i].alive;
    const char *head = win ? "초파리를 잡았다!" : (alive ? "시간이 다 됐다" : "모두 쓰러졌다");
    const char *sub  = win ? "살아남은 사람은 점수를 더 받아요" : "초파리가 이겼어요";
    color_t col = win ? C_GOOD : C_BAD;
    int hw = ui_text_w(head), sw = ui_text_w(sub);
    /* 오른쪽 위의 작은 지도(세로 50픽셀)를 가리지 않게 화면의 아래쪽 절반에 놓는다 */
    int bw = (hw > sw ? hw : sw) + 24, bh = 44, bx = (VW - bw) / 2, by = VY + VH / 2 + 2;
    ui_rect(c, bx - 1, by - 1, bw + 2, bh + 2, col);
    ui_rect(c, bx, by, bw, bh, C_BLACK);
    ui_text(c, (VW - hw) / 2, by + 8, head, col);
    ui_text(c, (VW - sw) / 2, by + 26, sub, C_TEXT);
}

/* 3D 화면 위에 얹는 것: 손에 든 무기, 가운데 표시, 작은 지도, 짧은 키 안내, 알림, 상태 줄, 결과.
 * 시프봇의 키 안내를 띄울 때는 그것만 그린다. 배율 1의 좌표로 그린다 */
static void draw_overlay(canvas_t *c, const boss_snap_t *b, const boss_view_t *v, int eye)
{
    if (b->outcome == BOSS_GOING && (v->help || b->tick == 0)) {     /* tick 0: 아직 시작하지 않았다 */
        draw_guide(c, b->tick == 0, v->age_ms);
        return;
    }
    if (eye < 0) {                                   /* 볼 사람이 없다: 지도만 크게 */
        ui_rect(c, 0, VY, VW, VH, C_BG);
        draw_minimap(c, b, -1);
        draw_bars(c, b, v, eye);
        draw_outcome(c, b);
        return;
    }
    if (eye == v->me) {
        draw_weapon(c, b, eye);
        ui_rect(c, VW / 2 - 3, VY + VH / 2, 7, 1, v->hit_ms >= 0 && v->hit_ms < 250 ? C_RED : C_WHITE);   /* 가운데 표시 */
        ui_rect(c, VW / 2, VY + VH / 2 - 3, 1, 7, v->hit_ms >= 0 && v->hit_ms < 250 ? C_RED : C_WHITE);
    }
    draw_minimap(c, b, eye);
    if (eye == v->me && b->outcome == BOSS_GOING) draw_legend(c);

    /* 알림: 내가 맞았다(테두리), 돌진이 나를 겨눈다, 날갯짓, 끈끈이에 붙었다 */
    if (v->hurt_ms >= 0 && v->hurt_ms < 300) {
        ui_rect(c, 0, VY, VW, 3, C_RED); ui_rect(c, 0, VY + VH - 3, VW, 3, C_RED);
        ui_rect(c, 0, VY, 3, VH, C_RED); ui_rect(c, VW - 3, VY, 3, VH, C_RED);
    }
    const char *note = NULL;
    color_t note_col = C_WARN;
    if (b->fly.windup && b->fly.target == eye) { note = "돌진이 온다!"; note_col = C_RED; }
    else if (v->wave_ms >= 0 && v->wave_ms < 500) note = "날갯짓!";
    else if (b->fly.stun) note = "붙었다! 지금이다";
    else if (b->fly.air) note = "달아났다! SHIFT로 살금살금";
    if (note && b->outcome == BOSS_GOING) {          /* 가운데에 놓되, 긴 글은 오른쪽 위의 작은 지도(x 189부터)를 피한다 */
        int w = ui_text_w(note), x = (VW - w) / 2;
        if (x + w > 186) x = 186 - w;
        ui_text(c, x, VY + 4, note, note_col);
    }
    draw_bars(c, b, v, eye);
    draw_outcome(c, b);
}

/* full이 240x134이면 전처럼 그린다. 배율 N으로 키운 화면(240N x 134N)이면 벽, 초파리, 참가자는 그 크기에
 * 맞춰 촘촘하게 그리고, 무기와 지도와 글자는 240x134로 그린 것을 N배로 키워 얹는다. */
void boss_view_draw(canvas_t *full, const boss_snap_t *b, const boss_view_t *v)
{
    static canvas_t *over;                           /* 배율이 2 이상일 때 얹을 것을 그리는 240x134 */
    int eye = -1, scale = ui_width(full) / SCREEN_W;
    if (scale < 1) scale = 1;
    if (scale > PRESENT_MAX_SCALE) scale = PRESENT_MAX_SCALE;
    if (!have_map) load_map();
    if (v->me >= 0 && v->me < b->n_players && b->p[v->me].alive) eye = v->me;
    int n = b->n_players < BOSS_PLAYERS ? b->n_players : BOSS_PLAYERS;
    for (int k = 0; eye < 0 && k < n; k++) {         /* 고른 사람부터 차례로, 살아 있는 사람을 찾는다 */
        int i = ((v->watch % n + n) % n + k) % n;
        if (b->p[i].alive) eye = i;
    }

    ui_clear(full, C_BLACK);
    if (eye >= 0) {
        float px = (float)b->p[eye].x / BOSS_FIX, py = (float)b->p[eye].y / BOSS_FIX;
        float a = (float)b->p[eye].dir * TWO_PI / BOSS_DIRS, dx = cosf(a), dy = sinf(a);
        set_view(scale);
        draw_walls(full, px, py, dx, dy);
        draw_things(full, b, eye, px, py, dx, dy);
        set_view(1);
    }
    if (scale == 1) { draw_overlay(full, b, v, eye); return; }
    if (!over) over = canvas_new(SCREEN_W, SCREEN_H);
    if (!over) return;
    ui_clear(over, C_NONE);
    draw_overlay(over, b, v, eye);
    canvas_blit_scaled(full, 0, 0, over, scale);
}
