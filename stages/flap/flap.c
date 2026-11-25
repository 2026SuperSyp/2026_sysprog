/* 플래핑 플라이 (담당: 김고은)
 *
 * 스페이스(또는 ↑)를 누를 때마다 날갯짓한다. 위의 거미줄과 아래의 파리지옥 사이 틈으로 지나가면 점수.
 * 부딪히면 잠깐 쉬었다가 다시 난다.
 *
 * 초파리는 규칙으로 날지 않는다. 위험이 다가오는 쪽을 뇌에 "다가옴" 자극으로 주고,
 * 뇌의 달아남 반응(BR_ESCAPE, 실제로는 거대 섬유 뉴런)이 나오면 날갯짓을 할지 정한다.
 *  - 바닥이나 아래 기둥 가장자리가 다가오면 왼쪽 다가옴(BR_LOOM_L)으로 준다.
 *  - 천장이나 위 기둥 가장자리가 다가오면 오른쪽 다가옴(BR_LOOM_R)으로 준다.
 *  - 뇌가 달아나려 하고, 오른쪽으로 돌려는 쪽(왼쪽 위험의 반대)이 크면 위로 날갯짓한다.
 *    왼쪽으로 돌려는 쪽이 크면(위가 위험) 날갯짓하지 않고 떨어지게 둔다.
 * 위/아래를 왼쪽/오른쪽 자극으로 바꾼 것은 우리가 정한 통역이다. 달아날지, 어느 쪽에서
 * 멀어질지는 뇌가 정한다. */
#include "stage.h"
#include "brain.h"

#include <stdio.h>

#define MAX_PIPES  64
#define HUD_H      12
#define CEIL_Y     (HUD_H + 1)         /* 천장 */
#define GROUND_Y   (PANE_H - 8)        /* 바닥 */
#define BIRD_X     26
#define BIRD_HW    5                   /* 몸의 가로 반 */
#define BIRD_HH    4                   /* 몸의 세로 반 */
#define PIPE_W     10

typedef struct {
    float x;
    int   gap_y;                       /* 틈의 가운데 */
    int   gap_h;                       /* 틈의 높이 */
    int   alive;
} pipe_t;

/* 한 명 몫의 상태. 초파리도 같은 모양을 쓴다. */
typedef struct {
    float y, vy;
    int   dead_until;                  /* 이 틱까지 쉬는 중(부딪힘) */
    int   invul_until;                 /* 이 틱까지 부딪혀도 괜찮다 */
    int   last_flap;
    int   n_pass, n_crash;
    float score;
    unsigned char passed[MAX_PIPES];
    float danger_below, danger_above;  /* 초파리 칸에 보여 줄, 이번 틱에 준 자극 */
} side_t;

static side_t pl[STAGE_MAX_PLAYERS];
static side_t fly;
static pipe_t pipes[MAX_PIPES];
static int    gap_seq[MAX_PIPES];      /* 씨앗에서 미리 만든 틈 위치(0~1) * 1000 */
static int    n_pipes, next_pipe, n_players, tick_no, total_ticks;

static float scroll_speed, gap_start, gap_end, gravity, flap_speed, max_fall;
static float score_per_pipe, crash_penalty, look_ahead, danger_range, loom_gain, turn_margin;
static int   pipe_every, flap_gap, respawn_ticks, invul_ticks, fly_flap_cooldown;

static const stage_info_t info = {
    .title    = "플래핑 플라이",
    .rule     = "스페이스로 날갯짓\n거미줄과 파리지옥 사이로 통과!",
    .controls = "스페이스 또는 W",
};

const stage_info_t *stage_info(void) { return &info; }

static float clampf(float v, float lo, float hi) { return v < lo ? lo : (v > hi ? hi : v); }

static sprite_id_t spr_person;         /* 날개 단 사람 (assets/person_wing.spr) */
static sprite_id_t spr_trap, spr_spider; /* 아래 장애물의 파리지옥 머리, 위 거미줄 끝의 거미 */

static void reset_side(side_t *s)
{
    *s = (side_t){ .y = (CEIL_Y + GROUND_Y) / 2.0f, .last_flap = -100 };
}

void stage_start(const stage_ctx_t *ctx)
{
    n_players   = ctx->n_players;
    total_ticks = ctx->n_ticks;

    scroll_speed      = cfg_get("scroll_speed", 1.3f);
    pipe_every        = (int)cfg_get("pipe_every_ticks", 34.0f);
    next_pipe         = (int)cfg_get("first_pipe_ticks", 20.0f);
    gap_start         = cfg_get("gap_start", 46.0f);
    gap_end           = cfg_get("gap_end", 34.0f);
    gravity           = cfg_get("gravity", 0.22f);
    flap_speed        = cfg_get("flap_speed", 2.5f);
    max_fall          = cfg_get("max_fall", 3.2f);
    flap_gap          = (int)cfg_get("flap_gap_ticks", 2.0f);
    respawn_ticks     = (int)cfg_get("respawn_ticks", 16.0f);
    invul_ticks       = (int)cfg_get("invul_ticks", 24.0f);
    score_per_pipe    = cfg_get("score_per_pipe", 10.0f);
    crash_penalty     = cfg_get("crash_penalty", 5.0f);
    look_ahead        = cfg_get("look_ahead", 30.0f);
    danger_range      = cfg_get("danger_range", 16.0f);
    loom_gain         = cfg_get("loom_gain", 1.0f);
    fly_flap_cooldown = (int)cfg_get("fly_flap_cooldown", 3.0f);
    turn_margin       = cfg_get("turn_margin", 0.1f);

    /* 문제는 씨앗에서만 만든다. 틈이 너무 크게 오르내리지 않게 이전 틈 근처에서 고른다. */
    stage_rng_t rng;
    stage_rng_seed(&rng, ctx->seed);
    int prev = 500;
    for (int i = 0; i < MAX_PIPES; i++) {
        int lo = prev - 350 < 0 ? 0 : prev - 350;
        int hi = prev + 350 > 1000 ? 1000 : prev + 350;
        gap_seq[i] = stage_rng_range(&rng, lo, hi);
        prev = gap_seq[i];
    }

    for (int p = 0; p < STAGE_MAX_PLAYERS; p++) reset_side(&pl[p]);
    reset_side(&fly);
    spr_person = ui_sprite_load("person_wing");
    spr_trap   = ui_sprite_load("flytrap");
    spr_spider = ui_sprite_load("spider");
}

static int is_dead(const side_t *s)  { return tick_no < s->dead_until; }
static int is_invul(const side_t *s) { return tick_no < s->invul_until; }

static void flap(side_t *s, int gap)
{
    if (is_dead(s) || tick_no - s->last_flap < gap) return;
    s->vy = -flap_speed;
    s->last_flap = tick_no;
}

void stage_input(int player, int key, double t)
{
    (void)t;
    if (key == SK_ACT || key == SK_UP) flap(&pl[player], flap_gap);
}

/* 몸 앞에 있는 가장 가까운 기둥. 없으면 -1 */
static int pipe_ahead(float range)
{
    for (int i = 0; i < n_pipes; i++) {
        if (!pipes[i].alive) continue;
        if (pipes[i].x + PIPE_W < BIRD_X - BIRD_HW) continue;           /* 이미 지나감 */
        if (pipes[i].x - (BIRD_X + BIRD_HW) > range) return -1;          /* 아직 멀다 */
        return i;
    }
    return -1;
}

/* 남은 거리를 자극 세기로. 가까울수록 1에 가깝다. 이미 넘었으면 1 */
static float danger(float dist)
{
    if (dist >= danger_range) return 0.0f;
    if (dist <= 0) return 1.0f;
    return clampf(loom_gain * (1.0f - dist / danger_range), 0.0f, 1.0f);
}

/* 초파리: 위, 아래에서 다가오는 위험을 자극으로 주고, 뇌의 반응대로 날갯짓한다. */
static void fly_think(void)
{
    fly.danger_below = fly.danger_above = 0.0f;
    if (is_dead(&fly)) return;

    float below = GROUND_Y - (fly.y + BIRD_HH);                 /* 바닥까지 */
    float above = (fly.y - BIRD_HH) - CEIL_Y;                   /* 천장까지 */
    int i = pipe_ahead(look_ahead);
    if (i >= 0) {
        float lip_b = pipes[i].gap_y + pipes[i].gap_h / 2.0f - (fly.y + BIRD_HH);
        float lip_t = (fly.y - BIRD_HH) - (pipes[i].gap_y - pipes[i].gap_h / 2.0f);
        if (lip_b < below) below = lip_b;
        if (lip_t < above) above = lip_t;
    }
    fly.danger_below = danger(below);
    fly.danger_above = danger(above);

    /* 한 틱에 한쪽만 준다. 양쪽을 같이 주면 방향 반응이 꺼진다. */
    if (fly.danger_below > fly.danger_above)      brain_stimulate(BR_LOOM_L, fly.danger_below);
    else if (fly.danger_above > fly.danger_below) brain_stimulate(BR_LOOM_R, fly.danger_above);

    if (brain_fired(BR_ESCAPE)) {
        float turn = brain_rate(BR_TURN_R) - brain_rate(BR_TURN_L);
        if (turn > turn_margin) flap(&fly, fly_flap_cooldown);   /* 아래 위험에서 멀어지기: 위로 */
    }
}

static void crash(side_t *s, int pane)
{
    s->n_crash++;
    s->score -= crash_penalty;
    if (s->score < 0) s->score = 0;
    s->dead_until = tick_no + respawn_ticks;
    s->vy = 0;
    fx_shake(pane, 2, 200);
    fx_flash(pane, C_BAD, 80);
    fx_shockwave(pane, BIRD_X, (int)s->y);
}

static void move_side(side_t *s, int pane)
{
    if (tick_no + 1 == s->dead_until) {                         /* 쉬는 마지막 틱: 가운데에서 다시 난다 */
        s->y = (CEIL_Y + GROUND_Y) / 2.0f;
        s->vy = -flap_speed;
        s->invul_until = s->dead_until + invul_ticks;
        return;
    }
    if (is_dead(s)) return;
    s->vy = clampf(s->vy + gravity, -flap_speed, max_fall);
    s->y += s->vy;
    if (s->y - BIRD_HH < CEIL_Y) { s->y = CEIL_Y + BIRD_HH; s->vy = 0; }

    if (s->y + BIRD_HH >= GROUND_Y) {
        if (is_invul(s)) { s->y = GROUND_Y - BIRD_HH; s->vy = -flap_speed; }
        else             { s->y = GROUND_Y - BIRD_HH; crash(s, pane); return; }
    }
    for (int i = 0; i < n_pipes; i++) {
        const pipe_t *p = &pipes[i];
        if (!p->alive) continue;
        if (p->x > BIRD_X + BIRD_HW || p->x + PIPE_W < BIRD_X - BIRD_HW) continue;
        int top = p->gap_y - p->gap_h / 2, bot = p->gap_y + p->gap_h / 2;
        if ((s->y - BIRD_HH < top || s->y + BIRD_HH > bot) && !is_invul(s)) {
            crash(s, pane);
            return;
        }
    }
}

/* 지나간 기둥을 센다. 쉬는 중이나 무적일 때 지나간 것은 점수가 없다. */
static void count_pass(side_t *s, int pane)
{
    for (int i = 0; i < n_pipes; i++) {
        if (s->passed[i] || pipes[i].x + PIPE_W >= BIRD_X - BIRD_HW) continue;
        s->passed[i] = 1;
        if (is_dead(s) || is_invul(s)) continue;
        s->n_pass++;
        s->score += score_per_pipe;
        fx_trail(pane, BIRD_X, (int)s->y);
    }
}

void stage_tick(double dt)
{
    (void)dt;
    if (tick_no >= next_pipe && n_pipes < MAX_PIPES) {          /* 새 기둥. 갈수록 틈이 좁아진다 */
        float p = total_ticks > 0 ? (float)tick_no / total_ticks : 0.0f;
        int h = (int)(gap_start + (gap_end - gap_start) * p);
        int lo = CEIL_Y + 6 + h / 2, hi = GROUND_Y - 6 - h / 2;
        pipes[n_pipes] = (pipe_t){ .x = PANE_W + 2, .gap_h = h, .alive = 1,
                                   .gap_y = lo + (hi - lo) * gap_seq[n_pipes] / 1000 };
        n_pipes++;
        next_pipe = tick_no + pipe_every;
    }

    fly_think();

    move_side(&fly, PANE_FLY);
    for (int p = 0; p < n_players; p++) move_side(&pl[p], p);

    for (int i = 0; i < n_pipes; i++) {
        if (!pipes[i].alive) continue;
        pipes[i].x -= scroll_speed;
        if (pipes[i].x + PIPE_W < -2) pipes[i].alive = 0;
    }
    count_pass(&fly, PANE_FLY);
    for (int p = 0; p < n_players; p++) count_pass(&pl[p], p);

    tick_no++;
}

/* ---- 그리기 ---- */

/* 작은 둥근 거미줄 한 칸(가운데 cx, cy, 반지름 r). 바큇살 8개와 둥근 줄 2개 */
static void draw_orb(canvas_t *c, int cx, int cy, int r)
{
    static const int dx[8] = { 0, 1, 1, 1, 0, -1, -1, -1 }, dy[8] = { -1, -1, 0, 1, 1, 1, 0, -1 };
    for (int k = 0; k < 8; k++) {
        int d = (dx[k] && dy[k]) ? r * 7 / 10 : r;                    /* 대각선은 조금 짧게(둥글게) */
        ui_line(c, cx, cy, cx + dx[k] * d, cy + dy[k] * d, C_GRAY3);
    }
    for (int q = 1; q <= 2; q++) {
        int rr = r * q / 3 + 1;
        for (int k = 0; k < 8; k++) {
            int j = (k + 1) % 8;
            int d1 = (dx[k] && dy[k]) ? rr * 7 / 10 : rr, d2 = (dx[j] && dy[j]) ? rr * 7 / 10 : rr;
            ui_line(c, cx + dx[k] * d1, cy + dy[k] * d1, cx + dx[j] * d2, cy + dy[j] * d2, C_WEB);
        }
    }
}

/* 위 장애물: 천장에서 늘어진 거미줄. 작은 둥근 거미줄을 세로로 이어 붙였고(남는 위쪽은 한 가닥 줄),
 * 맨 아래 끝에 거미가 매달려 있다. 부딪힘은 기둥 전체다. */
static void draw_web_column(canvas_t *c, int x, int y0, int y1)
{
    if (y1 <= y0) return;
    int cx = x + PIPE_W / 2, r = PIPE_W / 2;
    int sw = ui_sprite_w(spr_spider), sh = ui_sprite_h(spr_spider);
    int bottom = y1 - sh + 2;                                          /* 거미 자리를 남긴다 */
    int cy = bottom - r - 1, top_orb = bottom;
    for (; cy - r >= y0; cy -= PIPE_W) { draw_orb(c, cx, cy, r); top_orb = cy - r; }
    ui_line(c, cx, y0, cx, top_orb, C_GRAY3);                          /* 천장에서 내려온 한 가닥 */
    if (y1 - y0 >= sh) ui_sprite(c, spr_spider, cx - sw / 2, y1 - sh, tick_no / 6);
}

/* 아래 장애물: 땅에서 자란 파리지옥. 줄기 위에 입을 벌렸다 다물었다 하는 머리가 있다 */
static void draw_trap_column(canvas_t *c, int x, int y0, int y1, int idx)
{
    if (y1 <= y0) return;
    int hh = ui_sprite_h(spr_trap);
    ui_rect(c, x, y0 + hh / 2, PIPE_W, y1 - y0 - hh / 2, C_GREEN);    /* 줄기 */
    ui_rect(c, x + 4, y0 + hh / 2, 2, y1 - y0 - hh / 2, C_GOOD);
    for (int y = y0 + hh + 4; y < y1 - 3; y += 9) {                      /* 잎 */
        int left = ((y - y0) / 9) % 2;
        ui_rect(c, left ? x : x + 6, y, 4, 2, C_GOOD);
    }
    int shut = ((tick_no / 8) + idx) % 4 == 0;
    ui_sprite(c, spr_trap, x - 1, y0, shut);
}

static void draw_world(canvas_t *c)
{
    ui_rect(c, 0, CEIL_Y - 1, PANE_W, 1, C_GRAY2);
    for (int i = 0; i < n_pipes; i++) {
        if (!pipes[i].alive) continue;
        int x = (int)pipes[i].x;
        int top = pipes[i].gap_y - pipes[i].gap_h / 2, bot = pipes[i].gap_y + pipes[i].gap_h / 2;
        draw_web_column(c, x, CEIL_Y, top);
        draw_trap_column(c, x, bot, GROUND_Y, i);
    }
    ui_rect(c, 0, GROUND_Y, PANE_W, PANE_H - GROUND_Y, C_BROWN);
    for (int x = -((int)(tick_no * scroll_speed) % 10); x < PANE_W; x += 10)
        ui_rect(c, x, GROUND_Y + 2, 4, 2, C_ORANGE);
}

/* 사람: 음식 골라 먹기의 그 사람에 곤충 날개를 달았다(엎드려 오른쪽으로 난다). 그림의 (8, 6)이 부딪힘 판정의 가운데다 */
static void draw_player(canvas_t *c, const side_t *s)
{
    int frame = is_dead(s) ? 2 : (tick_no - s->last_flap < 4 ? 0 : 1);
    ui_sprite(c, spr_person, BIRD_X - 8, (int)s->y - 6, frame);
}

static void draw_side(canvas_t *c, const side_t *s, int is_fly)
{
    ui_clear(c, C_BG);
    draw_world(c);

    int blink = is_invul(s) && !is_dead(s) && (tick_no / 2) % 2;
    if (!blink) {
        if (is_fly) {
            sprite_id_t id = is_dead(s) ? SPR_FLY_HIT
                           : (tick_no - s->last_flap < 4 ? SPR_FLY_JUMP : SPR_FLY_IDLE);
            ui_sprite(c, id, BIRD_X - ui_sprite_w(id) / 2, (int)s->y - ui_sprite_h(id) / 2, tick_no / 2);
        } else {
            draw_player(c, s);
        }
    }
    if (is_dead(s)) ui_text(c, BIRD_X + 8, (int)s->y - 6, "쿵!", C_BAD);

    char buf[32];
    snprintf(buf, sizeof buf, "%d", (int)s->score);
    ui_text(c, PANE_W - ui_text_w(buf) - 2, 2, buf, C_TEXT);
    if (is_fly) {                                                      /* 이번 틱에 준 자극 */
        ui_text(c, 2, 0, "↓", C_TEXT);
        ui_bar(c, 15, 4, 18, s->danger_below, C_WARN);
        ui_text(c, 36, 0, "↑", C_TEXT);
        ui_bar(c, 49, 4, 18, s->danger_above, C_WARN);
    } else {
        snprintf(buf, sizeof buf, "통과 %d", s->n_pass);
        ui_text(c, 2, 0, buf, C_GRAY3);
    }
}

void stage_draw(int player, canvas_t *fly_canvas, canvas_t *me)
{
    draw_side(fly_canvas, &fly, 1);
    draw_side(me, &pl[player], 0);
}

void stage_result(stage_result_t *out)
{
    for (int p = 0; p < n_players; p++)
        out->score[p] = pl[p].score;
    out->fly_score = fly.score;
}
