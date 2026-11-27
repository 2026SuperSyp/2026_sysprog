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
#define MARK_TICKS 6                   /* 초파리 머리 위의 "!", "?"를 보여 주는 틱 수 */
#define GAIN_TICKS 10                  /* 통과할 때 "+N"을 보여 주는 틱 수 */
#define STIM_H     12                  /* 자극 세기 1.0일 때 초파리 옆 막대의 높이 */

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
    int   streak;                      /* 부딪히지 않고 이어서 통과한 기둥 수 */
    int   pass_tick;                   /* 마지막으로 점수를 얻은 틱("+N"을 띄운다) */
    float last_gain;                   /* 그때 얻은 점수 */
    float score;
    unsigned char passed[MAX_PIPES];
    float danger_below, danger_above;  /* 초파리 칸에 보여 줄, 이번 틱에 잰 위험 */
} side_t;

static side_t pl[STAGE_MAX_PLAYERS];
static side_t fly;
static pipe_t pipes[MAX_PIPES];
static int    gap_seq[MAX_PIPES];      /* 씨앗에서 미리 만든 틈 위치(0~1) * 1000 */
static int    n_pipes, next_pipe, n_players, tick_no, total_ticks;
static int    n_score_pipes;           /* 판이 끝나기 전에 몸 뒤로 지나가서 점수가 나는 기둥 수 */

/* 초파리 칸의 뇌 표시. stage_tick에서 적고 그리기는 읽기만 한다. */
static float fly_stim_l, fly_stim_r;   /* 이번 틱에 준 왼쪽(아래 위험), 오른쪽(위 위험) 다가옴 */
static float fly_escape;               /* 뇌의 달아남 반응 */
static int   fly_act_tick = -100;      /* 뇌 때문에 날갯짓한 틱 */
static int   fly_hold_tick = -100;     /* 달아남이 켜졌는데 날갯짓하지 않은 틱 */

static float scroll_speed, gap_start, gap_end, gravity, flap_speed, max_fall;
static float score_per_pipe, combo_step, crash_penalty, look_ahead, danger_range, loom_gain, turn_margin;
static float jump_start, jump_end;
static int   pipe_every, flap_gap, respawn_ticks, invul_ticks, fly_flap_cooldown, combo_max;

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
    *s = (side_t){ .y = (CEIL_Y + GROUND_Y) / 2.0f, .last_flap = -100, .pass_tick = -100 };
}

/* i번째 기둥의 난이도(0~1). 첫 기둥이 0이고 점수가 나는 마지막 기둥이 1이다. */
static float pipe_level(int i)
{
    return n_score_pipes > 1 ? clampf((float)i / (n_score_pipes - 1), 0.0f, 1.0f) : 1.0f;
}

void stage_start(const stage_ctx_t *ctx)
{
    n_players   = ctx->n_players;
    total_ticks = ctx->n_ticks;

    scroll_speed      = cfg_get("scroll_speed", 1.3f);
    pipe_every        = (int)cfg_get("pipe_every_ticks", 34.0f);
    next_pipe         = (int)cfg_get("first_pipe_ticks", 20.0f);
    gap_start         = cfg_get("gap_start", 46.0f);
    gap_end           = cfg_get("gap_end", 26.0f);
    gravity           = cfg_get("gravity", 0.22f);
    flap_speed        = cfg_get("flap_speed", 2.5f);
    max_fall          = cfg_get("max_fall", 3.2f);
    flap_gap          = (int)cfg_get("flap_gap_ticks", 2.0f);
    respawn_ticks     = (int)cfg_get("respawn_ticks", 16.0f);
    invul_ticks       = (int)cfg_get("invul_ticks", 24.0f);
    score_per_pipe    = cfg_get("score_per_pipe", 7.0f);
    combo_step        = cfg_get("combo_step", 1.0f);
    combo_max         = (int)cfg_get("combo_max", 4.0f);
    crash_penalty     = cfg_get("crash_penalty", 5.0f);
    jump_start        = cfg_get("jump_start", 250.0f);
    jump_end          = cfg_get("jump_end", 450.0f);
    look_ahead        = cfg_get("look_ahead", 30.0f);
    danger_range      = cfg_get("danger_range", 16.0f);
    loom_gain         = cfg_get("loom_gain", 1.0f);
    fly_flap_cooldown = (int)cfg_get("fly_flap_cooldown", 8.0f);
    turn_margin       = cfg_get("turn_margin", 0.1f);

    if (pipe_every < 1) pipe_every = 1;

    /* 기둥은 나온 뒤 pass_ticks 틱째에 몸 뒤로 완전히 지나간다(count_pass). 판이 끝나기
     * 그만큼 전까지 나온 기둥만 점수가 나므로, 난이도는 그 마지막 기둥에서 끝 값이 되게 한다. */
    int pass_ticks = (int)((PANE_W + 2 - (BIRD_X - BIRD_HW - PIPE_W)) / scroll_speed) + 1;
    n_score_pipes = total_ticks - pass_ticks >= next_pipe
                  ? (total_ticks - pass_ticks - next_pipe) / pipe_every + 1 : 1;

    /* 문제는 씨앗에서만 만든다. 틈이 너무 크게 오르내리지 않게 이전 틈 근처에서 고르고,
     * 뒤의 기둥일수록 오르내림의 폭을 키운다. */
    stage_rng_t rng;
    stage_rng_seed(&rng, ctx->seed);
    int prev = 500;
    for (int i = 0; i < MAX_PIPES; i++) {
        int jump = (int)(jump_start + (jump_end - jump_start) * pipe_level(i));
        int lo = prev - jump < 0 ? 0 : prev - jump;
        int hi = prev + jump > 1000 ? 1000 : prev + jump;
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

/* 날갯짓한다. 쉬는 중이거나 바로 전에 날갯짓했으면 하지 않고 0을 돌려준다. */
static int flap(side_t *s, int gap)
{
    if (is_dead(s) || tick_no - s->last_flap < gap) return 0;
    s->vy = -flap_speed;
    s->last_flap = tick_no;
    return 1;
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
    fly_stim_l = fly_stim_r = 0.0f;
    fly_escape = brain_rate(BR_ESCAPE);                         /* 보여 주려고 읽기만 한다 */
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
    if (fly.danger_below > fly.danger_above)      brain_stimulate(BR_LOOM_L, fly_stim_l = fly.danger_below);
    else if (fly.danger_above > fly.danger_below) brain_stimulate(BR_LOOM_R, fly_stim_r = fly.danger_above);

    if (brain_fired(BR_ESCAPE)) {
        float turn = brain_rate(BR_TURN_R) - brain_rate(BR_TURN_L);
        if (turn > turn_margin && flap(&fly, fly_flap_cooldown)) /* 아래 위험에서 멀어지기: 위로 */
            fly_act_tick = tick_no;
        else                                                     /* 위가 위험해서 참았거나, 방금 날갯짓해서 아직 못 하는 틱 */
            fly_hold_tick = tick_no;
    }
}

static void crash(side_t *s, int pane)
{
    s->n_crash++;
    s->streak = 0;
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

/* 지나간 기둥을 센다. 쉬는 중이나 무적일 때 지나간 것은 점수가 없다.
 * 부딪히지 않고 이어서 통과할수록 combo_step씩 더 얻는다(combo_max번까지 늘어난다). */
static void count_pass(side_t *s, int pane)
{
    for (int i = 0; i < n_pipes; i++) {
        if (s->passed[i] || pipes[i].x + PIPE_W >= BIRD_X - BIRD_HW) continue;
        s->passed[i] = 1;
        if (is_dead(s) || is_invul(s)) continue;
        s->n_pass++;
        s->streak++;
        int bonus = s->streak - 1 < combo_max ? s->streak - 1 : combo_max;
        s->last_gain = score_per_pipe + combo_step * bonus;
        s->score += s->last_gain;
        s->pass_tick = tick_no;
        fx_trail(pane, BIRD_X, (int)s->y);
    }
}

void stage_tick(double dt)
{
    (void)dt;
    if (tick_no >= next_pipe && n_pipes < MAX_PIPES) {          /* 새 기둥. 갈수록 틈이 좁아진다 */
        int h = (int)(gap_start + (gap_end - gap_start) * pipe_level(n_pipes));
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

/* 초파리 옆의 자극 막대. 아래에서 위로 자란다(바닥은 bottom 줄) */
static void draw_stim(canvas_t *c, int x, int bottom, float level)
{
    if (level <= 0.0f) return;
    int h = (int)(level * STIM_H + 0.5f);
    if (h < 1) h = 1;
    ui_rect(c, x, bottom - h + 1, 2, h, C_WARN);
}

/* 초파리 칸의 뇌 표시: 준 자극(옆 막대), 뇌의 달아남 반응(발밑 막대), 뇌 때문에 날갯짓했나(머리 위 "!", "?") */
static void draw_brain(canvas_t *c, const side_t *s)
{
    int y = (int)s->y, w = ui_sprite_w(SPR_FLY_IDLE), h = ui_sprite_h(SPR_FLY_IDLE);
    int left = BIRD_X - w / 2, top = y - h / 2;
    draw_stim(c, left - 3, top + h - 1, fly_stim_l);                  /* 왼쪽 다가옴(아래 위험) */
    draw_stim(c, left + w + 1, top + h - 1, fly_stim_r);              /* 오른쪽 다가옴(위 위험) */
    ui_bar(c, left, top + h + 1, w, fly_escape, C_CYAN);

    const char *mark = tick_no - 1 - fly_act_tick < MARK_TICKS ? "!"
                     : tick_no - 1 - fly_hold_tick < MARK_TICKS ? "?" : NULL;
    if (!mark) return;
    color_t col = mark[0] == '!' ? C_WARN : C_GRAY3;
    int mx = BIRD_X - ui_text_w(mark) / 2, my = top - UI_FONT_H - 1;
    if (my < CEIL_Y) { mx = left - 4 - ui_text_w(mark); my = y - UI_FONT_H / 2; }  /* 천장에 붙으면 왼쪽에 */
    ui_text(c, mx, my, mark, col);
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
    if (is_fly) draw_brain(c, s);
    if (is_dead(s)) ui_text(c, BIRD_X + 8, (int)s->y - 6, "쿵!", C_BAD);

    char buf[32];
    int age = tick_no - 1 - s->pass_tick;                              /* 통과한 뒤 지난 틱 */
    if (!is_dead(s) && age < GAIN_TICKS) {                             /* 얻은 점수가 몸 옆에서 떠오른다 */
        snprintf(buf, sizeof buf, "+%d", (int)s->last_gain);
        int gy = (int)s->y - 10 - age;
        ui_text(c, BIRD_X + 12, gy < CEIL_Y ? CEIL_Y : gy, buf, C_GOOD);
    }

    snprintf(buf, sizeof buf, "%d", (int)s->score);
    ui_text(c, PANE_W - ui_text_w(buf) - 2, 2, buf, C_TEXT);
    if (is_fly) {                                                      /* 이번 틱에 잰 아래, 위 위험 */
        ui_text(c, 2, 0, "↓", C_TEXT);
        ui_bar(c, 15, 4, 18, s->danger_below, C_WARN);
        ui_text(c, 36, 0, "↑", C_TEXT);
        ui_bar(c, 49, 4, 18, s->danger_above, C_WARN);
    } else if (s->streak >= 2) {                                       /* 이어서 통과한 수 */
        snprintf(buf, sizeof buf, "연속 %d", s->streak);
        ui_text(c, 2, 0, buf, C_GOOD);
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
