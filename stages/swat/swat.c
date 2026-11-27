
/* 파리채 피하기
 *
 * 바닥의 그림자(예고) -> 파리채 접근 -> 타격 -> 판정.
 * 같은 씨앗으로 모든 플레이어와 초파리가 같은 공격을 받는다.
 *
 * 수정 사항:
 * 1. 화면에 그리는 파리채와 실제 타격 판정의 틱 동기화
 * 2. 벽에 막히거나 파리채 쪽으로 가까워지는 초파리 점프 제한
 * 3. 플레이어 번호와 결과 배열의 범위 검사
 *
 * 초파리의 점프 여부와 방향은 실제 뇌 출력에 따른다.
 */

#include "stage.h"
#include "brain.h"

#include <stdio.h>

#define MAX_SWATS 40
#define FLOOR_Y   (PANE_H - 14)
#define BODY_H    12
#define HEAD_H    14
#define POP_Y     (FLOOR_Y - 26)   /* 결과 글자의 윗줄 */

/* 파리채가 닿은 틱에 캐릭터 위에 띄우는 결과 글자의 종류 */
enum { POP_NONE, POP_DODGE, POP_NEAR, POP_HIT };

typedef struct {
    float x;
    int last_move;
    int stun_until;
    int lives;
    int dodges;     /* 피한 파리채 수 */
    int pop_tick;   /* 결과 글자를 띄운 틱 */
    int pop_kind;   /* POP_* */
    float score;
} side_t;

typedef struct {
    int start;
    int x;
    int appr;   /* 이 파리채가 내려오는 틱 수. 뒤에 나올수록 짧다 */
} swat_t;

static side_t pl[STAGE_MAX_PLAYERS];
static side_t fly;
static swat_t sw[MAX_SWATS];

static int n_sw, n_players, tick_no, max_lives;

static int warn_t, appr_t, slam_t, lift_t;
static int stun_t, move_gap, hop_cooldown;
static int pop_t, mark_t;

static float head_w, body_w, step;
static float hit_penalty, score_per_dodge;
static float near_px, near_bonus;
static float loom_gain, warn_level, fly_hop;
static float wall_gain, wall_range, sense_margin;

/* 초파리 칸의 뇌 표시. stage_tick에서 적고 그리기는 읽기만 한다 */
static float fly_stim_l, fly_stim_r;   /* 이번 틱에 준 다가옴 자극 */
static int fly_mark_tick;              /* 달아남이 켜진 틱 */
static int fly_mark_blocked;           /* 1이면 켜졌는데 막혀서 못 뛰었다 */

static const stage_info_t info = {
    .title = "파리채 피하기",
    .rule = "그림자 자리를 피하세요\n바로 옆에서 피하면 보너스",
    .controls = "A D 좌우 이동",
};

const stage_info_t *stage_info(void)
{
    return &info;
}

static float clampf(float v, float lo, float hi)
{
    return v < lo ? lo : (v > hi ? hi : v);
}

static float absf(float v)
{
    return v < 0.0f ? -v : v;
}

static void move_side(side_t *s, float dx)
{
    s->x = clampf(
        s->x + dx,
        body_w / 2.0f,
        PANE_W - body_w / 2.0f
    );
}

void stage_start(const stage_ctx_t *ctx)
{
    n_players = ctx->n_players;

    if (n_players < 1)
        n_players = 1;

    if (n_players > STAGE_MAX_PLAYERS)
        n_players = STAGE_MAX_PLAYERS;

    tick_no = 0;

    warn_t = (int)cfg_get("warn_ticks", 6.0f);
    appr_t = (int)cfg_get("approach_ticks", 10.0f);
    slam_t = (int)cfg_get("slam_ticks", 4.0f);
    lift_t = (int)cfg_get("lift_ticks", 3.0f);
    stun_t = (int)cfg_get("stun_ticks", 6.0f);

    max_lives = (int)cfg_get("lives", 3.0f);
    if (max_lives < 1) max_lives = 1;
    if (max_lives > 9) max_lives = 9;

    move_gap =
        (int)cfg_get("move_gap_ticks", 2.0f);

    hop_cooldown =
        (int)cfg_get("hop_cooldown_ticks", 16.0f);

    head_w = cfg_get("head_w", 28.0f);
    body_w = cfg_get("body_w", 14.0f);
    step = cfg_get("step", 8.0f);

    hit_penalty =
        cfg_get("hit_penalty", 6.0f);

    score_per_dodge =
        cfg_get("score_per_dodge", 6.5f);

    near_px = cfg_get("near_px", 8.0f);
    near_bonus = cfg_get("near_bonus", 2.0f);

    pop_t = (int)cfg_get("pop_ticks", 10.0f);
    mark_t = (int)cfg_get("mark_ticks", 6.0f);

    loom_gain =
        cfg_get("loom_gain", 1.3f);

    warn_level =
        cfg_get("warn_level", 0.5f);

    fly_hop =
        cfg_get("fly_hop", 12.0f);

    wall_gain =
        cfg_get("wall_gain", 0.5f);

    wall_range =
        cfg_get("wall_range", 14.0f);

    sense_margin =
        cfg_get("sense_margin", 4.0f);

    int first_delay =
        (int)cfg_get("first_delay_ticks", 20.0f);

    int rest_min =
        (int)cfg_get("rest_min_ticks", 6.0f);

    int rest_max =
        (int)cfg_get("rest_max_ticks", 12.0f);

    int appr_end =
        (int)cfg_get("approach_end_ticks", 6.0f);

    if (warn_t < 1) warn_t = 1;
    if (appr_t < 1) appr_t = 1;
    if (appr_end < 1) appr_end = 1;
    if (appr_end > appr_t) appr_end = appr_t;
    if (near_px < 0.0f) near_px = 0.0f;
    if (near_bonus < 0.0f) near_bonus = 0.0f;
    if (slam_t < 1) slam_t = 1;
    if (lift_t < 0) lift_t = 0;
    if (stun_t < 0) stun_t = 0;
    if (move_gap < 0) move_gap = 0;
    if (hop_cooldown < 0) hop_cooldown = 0;
    if (first_delay < 0) first_delay = 0;
    if (rest_min < 0) rest_min = 0;
    if (rest_max < rest_min) rest_max = rest_min;

    body_w = clampf(
        body_w, 2.0f, PANE_W / 2.0f
    );

    head_w = clampf(
        head_w, 2.0f, PANE_W - 14.0f
    );

    step = clampf(
        step, 1.0f, PANE_W / 2.0f
    );

    wall_range = clampf(
        wall_range, 1.0f, PANE_W / 2.0f
    );

    if (sense_margin < 0.0f)
        sense_margin = 0.0f;

    if (hit_penalty < 0.0f)
        hit_penalty = 0.0f;

    /* Generate attacks using the shared seed. */
    stage_rng_t rng;
    stage_rng_seed(&rng, ctx->seed);

    int n_ticks = ctx->n_ticks > 0 ? ctx->n_ticks : 1;

    int t = first_delay;
    n_sw = 0;

    while (n_sw < MAX_SWATS) {
        /* 판이 흐를수록 내려오는 틱 수가 appr_end 쪽으로 준다 */
        int appr = appr_t -
            (appr_t - appr_end) * t / n_ticks;

        if (t + warn_t + appr + slam_t >= ctx->n_ticks)
            break;

        sw[n_sw].start = t;
        sw[n_sw].appr = appr;
        sw[n_sw].x = stage_rng_range(
            &rng, 6, PANE_W - 7
        );

        n_sw++;

        t += warn_t + appr + slam_t + lift_t +
            stage_rng_range(
                &rng, rest_min, rest_max
            );
    }

    for (int p = 0; p < STAGE_MAX_PLAYERS; p++) {
        pl[p] = (side_t){
            .x = PANE_W / 2.0f,
            .last_move = -100,
            .pop_tick = -1000,
            .lives = max_lives
        };
    }

    fly = (side_t){
        .x = PANE_W / 2.0f,
        .last_move = -100,
        .pop_tick = -1000,
        .lives = max_lives
    };

    fly_mark_tick = -1000;
}

void stage_input(int player, int key, double t)
{
    (void)t;

    if (player < 0 || player >= n_players)
        return;

    side_t *s = &pl[player];

    if (s->lives <= 0)
        return;

    if (tick_no < s->stun_until)
        return;

    if (tick_no - s->last_move < move_gap)
        return;

    if (key == SK_LEFT) {
        move_side(s, -step);
        s->last_move = tick_no;
    }

    if (key == SK_RIGHT) {
        move_side(s, step);
        s->last_move = tick_no;
    }
}

/* Current warning, approach or slam. */
static int active_swat(void)
{
    for (int k = 0; k < n_sw; k++) {
        int t0 = sw[k].start;

        if (tick_no >= t0 &&
            tick_no <
                t0 + warn_t + sw[k].appr + slam_t) {
            return k;
        }
    }

    return -1;
}

/*
 * Fly movement:
 *
 * The swatter provides visual looming stimuli.
 * The brain decides whether to escape and turn.
 *
 * Avoid jumps that cannot move away from a wall,
 * or that move closer to the swatter's impact point.
 */
static void fly_think(void)
{
    fly_stim_l = 0.0f;
    fly_stim_r = 0.0f;

    if (fly.lives <= 0)
        return;

    float from_left = 0.0f;
    float from_right = 0.0f;

    int k = active_swat();
    int threatened = 0;

    if (k >= 0 &&
        tick_no <
            sw[k].start + warn_t + sw[k].appr) {

        float dx =
            (float)sw[k].x - fly.x;

        if (absf(dx) <
            (head_w + body_w) / 2.0f +
                sense_margin) {

            int since =
                tick_no - sw[k].start;

            float p = since < warn_t
                ? warn_level *
                  (float)(since + 1) /
                  (float)warn_t
                : warn_level +
                  (1.0f - warn_level) *
                  (float)(since - warn_t + 1) /
                  (float)sw[k].appr;

            float level = clampf(
                loom_gain * p,
                0.0f, 1.0f
            );

            threatened = 1;

            if (dx < 0.0f ||
                (dx == 0.0f && k % 2 == 0)) {
                from_left = level;
            }
            else {
                from_right = level;
            }
        }
    }

    /* Wall sensing outside active attacks. */
    if (k < 0) {
        float gap_l =
            fly.x - body_w / 2.0f;

        float gap_r =
            PANE_W - body_w / 2.0f - fly.x;

        if (gap_l < wall_range) {
            from_left += wall_gain *
                (1.0f - gap_l / wall_range);
        }

        if (gap_r < wall_range) {
            from_right += wall_gain *
                (1.0f - gap_r / wall_range);
        }
    }

    /* One stimulus direction per tick. */
    int stimulated = 0;

    if (from_left > from_right) {
        fly_stim_l = clampf(from_left, 0.0f, 1.0f);

        brain_stimulate(BR_LOOM_L, fly_stim_l);

        stimulated = 1;
    }
    else if (from_right > from_left) {
        fly_stim_r = clampf(from_right, 0.0f, 1.0f);

        brain_stimulate(BR_LOOM_R, fly_stim_r);

        stimulated = 1;
    }

    int escape = stimulated &&
        brain_fired(BR_ESCAPE);

    if (escape &&
        tick_no >= fly.stun_until &&
        tick_no - fly.last_move >=
            hop_cooldown) {

        /* 실제로 뛰면 0이 된다. 남으면 머리 위에 "?"를 띄운다 */
        int blocked = 1;

        float turn =
            brain_rate(BR_TURN_R) -
            brain_rate(BR_TURN_L);

        if (absf(turn) > 0.1f) {
            float dx =
                turn > 0.0f
                ? fly_hop : -fly_hop;

            float nx = clampf(
                fly.x + dx,
                body_w / 2.0f,
                PANE_W - body_w / 2.0f
            );

            float actual = nx - fly.x;

            /* Reject wall-blocked hops. */
            int useful =
                absf(actual) >=
                    fly_hop * 0.35f;

            /* Reject hops toward the swatter. */
            if (threatened && k >= 0) {
                float before = absf(
                    fly.x - (float)sw[k].x
                );

                float after = absf(
                    nx - (float)sw[k].x
                );

                if (after <= before) {
                    useful = 0;
                }
            }

            if (useful) {
                fx_trail(
                    PANE_FLY,
                    (int)fly.x,
                    FLOOR_Y + 4
                );

                move_side(&fly, dx);
                fly.last_move = tick_no;
                blocked = 0;
            }
        }

        /* 뛰었으면 그 틱부터 "!"를 띄운다. "?"는 막혀 있는 동안 이어진다 */
        if (!blocked || tick_no - fly_mark_tick >= mark_t ||
            fly_mark_blocked) {
            fly_mark_tick = tick_no;
            fly_mark_blocked = blocked;
        }
    }
}

/* 파리채가 바닥에 닿은 틱에 맞았는지, 피했는지를 정한다.
 * 초파리와 참가자에게 같은 규칙을 쓴다. */
static void judge_hit(
    side_t *s, int pane, float swat_x
)
{
    if (s->lives <= 0)
        return;

    float dist = absf(swat_x - s->x);
    float reach = (head_w + body_w) / 2.0f;

    s->pop_tick = tick_no;

    if (dist >= reach) {
        s->dodges++;
        s->score += score_per_dodge;
        s->pop_kind = POP_DODGE;

        /* 맞는 자리 바로 옆에서 버티면 더 준다 */
        if (dist < reach + near_px) {
            s->score += near_bonus;
            s->pop_kind = POP_NEAR;
        }

        fx_shockwave(
            pane,
            (int)swat_x,
            FLOOR_Y + BODY_H
        );
        return;
    }

    s->pop_kind = POP_HIT;
    s->lives--;

    s->score = clampf(
        s->score - hit_penalty,
        0.0f, 1.0e9f
    );

    s->stun_until = tick_no + stun_t;

    fx_shake(pane, 2, 150);
    fx_flash(pane, C_BAD, 50);
}

void stage_tick(double dt)
{
    (void)dt;

    fly_think();

    for (int k = 0; k < n_sw; k++) {
        int t_hit =
            sw[k].start + warn_t + sw[k].appr;

        if (tick_no == t_hit) {
            judge_hit(
                &fly, PANE_FLY,
                (float)sw[k].x
            );

            for (int p = 0;
                 p < n_players;
                 p++) {

                judge_hit(
                    &pl[p], p,
                    (float)sw[k].x
                );
            }
        }
    }

    tick_no++;
}

/* -------------------------------------------------
 * Rendering: does not change game state.
 * ------------------------------------------------- */

static void draw_swatter(canvas_t *c)
{
    /*
     * stage_tick() processes the current tick
     * and then increments tick_no.
     *
     * Render the last processed tick to keep
     * the visible slam and hit event aligned.
     */
    int shown_tick = tick_no - 1;
    int k = -1;

    for (int i = 0; i < n_sw; i++) {
        if (shown_tick >= sw[i].start &&
            shown_tick <
                sw[i].start +
                warn_t + sw[i].appr +
                slam_t + lift_t) {

            k = i;
            break;
        }
    }

    if (k < 0)
        return;

    int sx = sw[k].x;
    int t = shown_tick - sw[k].start;
    int appr = sw[k].appr;

    int y_floor = FLOOR_Y + BODY_H;
    int y_slam = y_floor - HEAD_H;
    int hw = (int)head_w;

    /* Warning shadow */
    if (t < warn_t + appr + slam_t) {
        int on =
            t >= warn_t ||
            ((t / 2) & 1) == 0;

        ui_rect(
            c,
            sx - hw / 2,
            y_floor - 4,
            hw, 4,
            on ? C_BAD : C_WARN
        );
    }

    int y;

    if (t < warn_t)
        return;

    t -= warn_t;

    /*
     * During approach, remain above the floor.
     * The first fully landed frame is the
     * same processed tick as judge_hit().
     */
    if (t < appr) {
        y = -HEAD_H +
            (y_slam + HEAD_H) *
            t / appr;
    }
    else if (t < appr + slam_t) {
        y = y_slam;
    }
    else {
        y = y_slam -
            (y_slam + HEAD_H) *
            (t - appr - slam_t + 1) /
            (lift_t + 1);
    }

    /* Handle */
    if (y > 0) {
        ui_rect(
            c, sx - 1, 0,
            3, y, C_BROWN
        );
    }

    /* Swatter head */
    ui_rect(
        c,
        sx - hw / 2,
        y, hw, HEAD_H,
        C_GRAY3
    );

    /* Mesh holes */
    for (int gy = y + 2;
         gy < y + HEAD_H - 2;
         gy += 4) {

        for (int gx = sx - hw / 2 + 2;
             gx < sx + hw / 2 - 2;
             gx += 4) {

            ui_rect(
                c, gx, gy,
                2, 2, C_BG
            );
        }
    }
}

static void draw_player(
    canvas_t *c,
    int cx,
    int top,
    int hit
)
{
    ui_rect(
        c,
        cx - (int)body_w / 2,
        top,
        (int)body_w,
        BODY_H,
        hit ? C_BAD : C_GOOD
    );

    ui_rect(
        c,
        cx - 4, top + 3,
        2, 2, C_BLACK
    );

    ui_rect(
        c,
        cx + 2, top + 3,
        2, 2, C_BLACK
    );
}

/* 초파리의 뇌 표시: 준 자극은 그 쪽 옆의 세로 막대,
 * 달아남이 켜져서 뛰면 머리 위의 "!", 막혀서 못 뛰면 "?" */
static void draw_brain(canvas_t *c, int fx)
{
    int half = ui_sprite_w(SPR_FLY_IDLE) / 2;
    int floor_y = FLOOR_Y + BODY_H;

    float lv[2] = { fly_stim_l, fly_stim_r };
    int bx[2] = { fx - half - 3, fx + half + 1 };

    for (int i = 0; i < 2; i++) {
        if (lv[i] <= 0.0f)
            continue;

        int h = (int)(lv[i] * 12.0f + 0.5f);
        if (h < 1) h = 1;

        int x = bx[i];
        if (x < 0) x = 0;
        if (x > PANE_W - 2) x = PANE_W - 2;

        /* 같은 색의 그림자(높이 4)에 묻히지 않게 그 위에 세운다 */
        ui_rect(c, x, floor_y - 4 - h, 2, h, C_WARN);
    }

    int since = tick_no - 1 - fly_mark_tick;

    if (since >= 0 && since < mark_t) {
        const char *m = fly_mark_blocked ? "?" : "!";

        ui_text(
            c,
            fx - ui_text_w(m) / 2,
            floor_y - ui_sprite_h(SPR_FLY_IDLE) - 9,
            m,
            fly_mark_blocked ? C_GRAY3 : C_WARN
        );
    }
}

/* 파리채가 닿은 틱부터 잠깐 캐릭터 위에 결과 글자를 띄운다 */
static void draw_pop(canvas_t *c, const side_t *s)
{
    int since = tick_no - 1 - s->pop_tick;

    if (since < 0 || since >= pop_t ||
        s->pop_kind == POP_NONE)
        return;

    char buf[16];
    color_t col;

    if (s->pop_kind == POP_HIT) {
        snprintf(buf, sizeof buf, "-%g", (double)hit_penalty);
        col = C_BAD;
    }
    else if (s->pop_kind == POP_NEAR) {
        snprintf(buf, sizeof buf, "아슬!");
        col = C_YELLOW;
    }
    else {
        snprintf(buf, sizeof buf, "+%g", (double)score_per_dodge);
        col = C_GOOD;
    }

    int w = ui_text_w(buf);
    int x = (int)clampf(
        s->x - (float)w / 2.0f,
        1.0f, (float)(PANE_W - 1 - w)
    );

    ui_text(c, x, POP_Y, buf, col);
}

static void draw_side(
    canvas_t *c,
    const side_t *s,
    int is_fly
)
{
    /* Ground */
    ui_rect(
        c,
        0,
        FLOOR_Y + BODY_H,
        PANE_W, 2,
        C_GRAY2
    );

    /* 피한 수. 파리채가 앞을 지나가도록 먼저 그린다 */
    char buf[24];
    snprintf(buf, sizeof buf, "피함 %d", s->dodges);

    ui_text(
        c,
        PANE_W - 2 - ui_text_w(buf),
        2,
        buf,
        C_TEXT
    );

    draw_swatter(c);

    int hit =
        tick_no < s->stun_until ||
        s->lives <= 0;

    if (is_fly) {
        sprite_id_t id = hit
            ? SPR_FLY_HIT
            : (tick_no - s->last_move < 4
                ? SPR_FLY_JUMP
                : SPR_FLY_IDLE);

        ui_sprite(
            c,
            id,
            (int)s->x -
                ui_sprite_w(id) / 2,
            FLOOR_Y + BODY_H -
                ui_sprite_h(id),
            0
        );
    }
    else {
        draw_player(
            c,
            (int)s->x,
            FLOOR_Y,
            hit
        );
    }

    if (is_fly)
        draw_brain(c, (int)s->x);

    draw_pop(c, s);

    /* Remaining lives */
    for (int i = 0;
         i < max_lives;
         i++) {

        ui_rect(
            c,
            4 + i * 8,
            4,
            6, 6,
            i < s->lives
                ? C_BAD : C_GRAY2
        );
    }

    if (s->lives <= 0) {
        const char *msg = "탈락!";

        ui_text(
            c,
            (PANE_W -
                ui_text_w(msg)) / 2,
            44,
            msg,
            C_BAD
        );
    }
}

void stage_draw(
    int player,
    canvas_t *fly_canvas,
    canvas_t *me
)
{
    ui_clear(fly_canvas, C_BG);
    ui_clear(me, C_BG);

    draw_side(
        fly_canvas, &fly, 1
    );

    if (player >= 0 &&
        player < n_players) {

        draw_side(
            me, &pl[player], 0
        );
    }
}

void stage_result(stage_result_t *out)
{
    for (int p = 0;
         p < STAGE_MAX_PLAYERS;
         p++) {

        out->score[p] =
            p < n_players
                ? pl[p].score
                : 0.0f;
    }

    out->fly_score = fly.score;
}
