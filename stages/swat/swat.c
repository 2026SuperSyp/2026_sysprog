
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

typedef struct {
    float x;
    int last_move;
    int stun_until;
    int hit_now;
    int lives;
    float score;
} side_t;

typedef struct {
    int start;
    int x;
} swat_t;

static side_t pl[STAGE_MAX_PLAYERS];
static side_t fly;
static swat_t sw[MAX_SWATS];

static int n_sw, n_players, tick_no, max_lives;

static int warn_t, appr_t, slam_t, lift_t;
static int stun_t, move_gap, hop_cooldown;

static float head_w, body_w, step;
static float hit_penalty, score_per_dodge;
static float loom_gain, warn_level, fly_hop;
static float wall_gain, wall_range, sense_margin;

static const stage_info_t info = {
    .title = "파리채 피하기",
    .rule = "그림자 자리를 피하세요\n초파리보다 많이 피하기",
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
        (int)cfg_get("hop_cooldown_ticks", 8.0f);

    head_w = cfg_get("head_w", 28.0f);
    body_w = cfg_get("body_w", 14.0f);
    step = cfg_get("step", 8.0f);

    hit_penalty =
        cfg_get("hit_penalty", 6.0f);

    score_per_dodge =
        cfg_get("score_per_dodge", 8.5f);

    loom_gain =
        cfg_get("loom_gain", 1.3f);

    warn_level =
        cfg_get("warn_level", 0.5f);

    fly_hop =
        cfg_get("fly_hop", 22.0f);

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

    if (warn_t < 1) warn_t = 1;
    if (appr_t < 1) appr_t = 1;
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

    int len =
        warn_t + appr_t + slam_t + lift_t;

    int t = first_delay;
    n_sw = 0;

    while (n_sw < MAX_SWATS &&
           t + warn_t + appr_t + slam_t <
               ctx->n_ticks) {

        sw[n_sw].start = t;
        sw[n_sw].x = stage_rng_range(
            &rng, 6, PANE_W - 7
        );

        n_sw++;

        t += len + stage_rng_range(
            &rng, rest_min, rest_max
        );
    }

    for (int p = 0; p < STAGE_MAX_PLAYERS; p++) {
        pl[p] = (side_t){
            .x = PANE_W / 2.0f,
            .last_move = -100,
            .lives = max_lives
        };
    }

    fly = (side_t){
        .x = PANE_W / 2.0f,
        .last_move = -100,
        .lives = max_lives
    };
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
                t0 + warn_t + appr_t + slam_t) {
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
    if (fly.lives <= 0)
        return;

    float from_left = 0.0f;
    float from_right = 0.0f;

    int k = active_swat();
    int threatened = 0;

    if (k >= 0 &&
        tick_no <
            sw[k].start + warn_t + appr_t) {

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
                  (float)appr_t;

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
        brain_stimulate(
            BR_LOOM_L,
            clampf(from_left, 0.0f, 1.0f)
        );

        stimulated = 1;
    }
    else if (from_right > from_left) {
        brain_stimulate(
            BR_LOOM_R,
            clampf(from_right, 0.0f, 1.0f)
        );

        stimulated = 1;
    }

    if (stimulated &&
        tick_no >= fly.stun_until &&
        brain_fired(BR_ESCAPE) &&
        tick_no - fly.last_move >=
            hop_cooldown) {

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
            }
        }
    }
}

static void judge_hit(
    side_t *s, int pane, float swat_x
)
{
    s->hit_now = 0;

    if (s->lives <= 0)
        return;

    s->hit_now =
        absf(swat_x - s->x) <
            (head_w + body_w) / 2.0f;

    if (!s->hit_now)
        return;

    s->lives--;

    s->score = clampf(
        s->score - hit_penalty,
        0.0f, 1.0e9f
    );

    s->stun_until = tick_no + stun_t;

    fx_shake(pane, 2, 150);
    fx_flash(pane, C_BAD, 50);
}

static void judge_end(
    side_t *s, int pane, float swat_x
)
{
    if (s->lives > 0 &&
        !s->hit_now) {

        s->score += score_per_dodge;

        fx_shockwave(
            pane,
            (int)swat_x,
            FLOOR_Y + BODY_H
        );
    }

    s->hit_now = 0;
}

void stage_tick(double dt)
{
    (void)dt;

    fly_think();

    for (int k = 0; k < n_sw; k++) {
        int t_hit =
            sw[k].start + warn_t + appr_t;

        int t_end = t_hit + slam_t;

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

        if (tick_no == t_end) {
            judge_end(
                &fly, PANE_FLY,
                (float)sw[k].x
            );

            for (int p = 0;
                 p < n_players;
                 p++) {

                judge_end(
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
                warn_t + appr_t +
                slam_t + lift_t) {

            k = i;
            break;
        }
    }

    if (k < 0)
        return;

    int sx = sw[k].x;
    int t = shown_tick - sw[k].start;

    int y_floor = FLOOR_Y + BODY_H;
    int y_slam = y_floor - HEAD_H;
    int hw = (int)head_w;

    /* Warning shadow */
    if (t < warn_t + appr_t + slam_t) {
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
    if (t < appr_t) {
        y = -HEAD_H +
            (y_slam + HEAD_H) *
            t / appr_t;
    }
    else if (t < appr_t + slam_t) {
        y = y_slam;
    }
    else {
        y = y_slam -
            (y_slam + HEAD_H) *
            (t - appr_t - slam_t + 1) /
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
