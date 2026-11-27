
/*
 * SUPER SYP PARTY - Spider Web Escape
 *
 * Player: W/A/S/D movement.
 * Fly: route planning + brain-controlled turning
 * and climbing speed.
 *
 * Three deterministic maps:
 * 0 = original map
 * 1 = mirrored map
 * 2 = alternate mirrored obstacles
 *
 * Existing movement, brain, camera, collision,
 * progress and scoring rules are preserved.
 */

#include "stage.h"
#include "brain.h"

#define BODY_H 9
#define WORLD_HEIGHT 420
#define LOOK_TICKS 45
#define STEER_BAND 3.0f
#define ESCAPE_COOLDOWN 8

typedef struct {
    int x, y, w, h;
} web_t;

typedef struct {
    float x, y;
    float camera;
    float best;

    int lives;
    int stun_until;
    int safe_until;
    int last_move;

    int finished;
    int finish_tick;

    /* Player only: was ahead of the fly last tick. */
    int ahead;
} actor_t;

/* Original map, unchanged. */
static const web_t base_webs[] = {
    { 0,   0,  8, 420},
    {86,   0,  8, 420},

    { 8, 340, 34, 10},
    {66, 325, 23, 14},
    {30, 288, 45,  9},
    { 0, 257, 27, 15},
    {51, 251, 39, 12},
    {17, 215, 23, 18},
    {56, 205, 22, 20},
    { 0, 172, 50,  8},
    {72, 159, 22, 14},
    {32, 139, 26, 14},
    { 5, 105, 31, 18},
    {60,  99, 32, 13},
    {34,  71, 45, 10},
    { 0,  42, 25, 10},
    {60,  37, 25, 10}
};

#define N_WEBS \
    ((int)(sizeof(base_webs) / sizeof(base_webs[0])))

/* Selected map for this match. */
static web_t webs[N_WEBS];

/*
 * Map A: original.
 * Map B: horizontally mirrored.
 * Map C: alternating interior obstacles mirrored.
 *
 * Boundary walls remain in place.
 */
static void select_map(int variant)
{
    for (int i = 0; i < N_WEBS; i++) {
        webs[i] = base_webs[i];

        int mirror =
            (variant == 1) ||
            (variant == 2 &&
             i >= 2 &&
             (i % 2) != 0);

        if (mirror) {
            webs[i].x =
                PANE_W - base_webs[i].x
                - base_webs[i].w;
        }
    }
}

static actor_t players[STAGE_MAX_PLAYERS];
static actor_t fly;

static int n_players;
static int ticks;
static int tick_no;

static int lives_max;
static int step_gap;
static int stun_ticks;
static int grace_ticks;

static float world_h;
static float spawn_y;
static float goal_y;

static float body_w;
static float move_px;
static float fly_speed;

static float sense_range;
static float level_min;
static float level_max;
static float steer_px;
static float deadzone;

static float finish_base;
static float time_bonus;
static float climb_max;
static float hit_penalty;

static float fly_goal;
static int fly_escape_last_tick;

/*
 * Climbing comes from the walk reaction.
 * The open way up is given as an object on the right.
 */
static float see_level;

/*
 * Brain display for the fly pane.
 * Written in fly_tick, only read while drawing.
 */
static int stim_side;          /* -1 left, +1 right, 0 none */
static float stim_level;       /* loom or object level given this tick */
static float walk_now;         /* walk reaction read this tick */
static int escape_act_tick;    /* escape fired and the fly jumped */
static int escape_miss_tick;   /* escape fired but a web blocked it */

#define REACT_SHOW_TICKS 6

static const stage_info_t info = {
    .title = "거미줄 탈출",
    .rule = "거미줄을 피해 위로 탈출\n닿으면 감점, 빠를수록 고득점",
    .controls = "W A S D 이동"
};

const stage_info_t *stage_info(void)
{
    return &info;
}

/* ---------- Helpers ---------- */

static float limit(float v, float lo, float hi)
{
    if (v < lo) return lo;
    if (v > hi) return hi;
    return v;
}

static float absv(float v)
{
    return v < 0.0f ? -v : v;
}

/* ---------- Collision ---------- */

static int hits_web(float x, float y)
{
    float l = x - body_w * 0.5f;
    float r = x + body_w * 0.5f;
    float t = y - BODY_H * 0.5f;
    float b = y + BODY_H * 0.5f;

    if (l < 2.0f || r > PANE_W - 2.0f)
        return 1;

    for (int i = 0; i < N_WEBS; i++) {
        const web_t *w = &webs[i];

        if (l < w->x + w->w &&
            r > w->x &&
            t < w->y + w->h &&
            b > w->y) {
            return 1;
        }
    }

    return 0;
}

/* ---------- Progress ---------- */

static void update_progress(actor_t *a)
{
    float p = limit(
        (spawn_y - a->y) / (spawn_y - goal_y),
        0.0f, 1.0f
    );

    if (!a->finished &&
        a->y <= goal_y &&
        a->x >= 31.0f &&
        a->x <= 63.0f) {

        a->finished = 1;
        a->finish_tick = tick_no;
    }

    if (a->finished)
        p = 1.0f;
    else if (p > 0.99f)
        p = 0.99f;

    if (p > a->best)
        a->best = p;
}

/* ---------- Movement ---------- */

static void step_actor(
    actor_t *a,
    int pane,
    float dx,
    float dy
)
{
    if (a->lives <= 0 ||
        a->finished ||
        tick_no < a->stun_until) {
        return;
    }

    float m = absv(dx) > absv(dy)
        ? absv(dx) : absv(dy);

    int n = (int)m + 1;

    if (n < 1)
        n = 1;

    for (int k = 0; k < n; k++) {
        float nx = limit(
            a->x + dx / (float)n,
            body_w * 0.5f + 2.0f,
            PANE_W - body_w * 0.5f - 2.0f
        );

        float ny = limit(
            a->y + dy / (float)n,
            goal_y,
            spawn_y
        );

        if (hits_web(nx, ny)) {
            /*
             * The web blocks movement even during
             * the post-hit grace period.
             */
            if (tick_no >= a->safe_until) {
                a->lives--;

                a->stun_until =
                    tick_no + stun_ticks;

                a->safe_until =
                    a->stun_until + grace_ticks;

                fx_flash(pane, C_BAD, 70);
                fx_shake(pane, 2, 140);

                /* Ring where the body touched the web. */
                fx_shockwave(
                    pane,
                    (int)nx,
                    (int)(ny - a->camera)
                );
            }

            break;
        }

        a->x = nx;
        a->y = ny;
    }

    update_progress(a);
}

/* ---------- Camera ---------- */

static void camera_update(actor_t *a)
{
    float sy = a->y - a->camera;

    const float upper = 29.0f;
    const float lower = (float)PANE_H - 16.0f;

    if (sy < upper)
        a->camera = a->y - upper;
    else if (sy > lower)
        a->camera = a->y - lower;

    a->camera = limit(
        a->camera,
        0.0f,
        world_h - PANE_H
    );
}

/* ---------- Initialization ---------- */

void stage_start(const stage_ctx_t *ctx)
{
    n_players = ctx->n_players;

    if (n_players < 1)
        n_players = 1;

    if (n_players > STAGE_MAX_PLAYERS)
        n_players = STAGE_MAX_PLAYERS;

    ticks = ctx->n_ticks > 0
        ? ctx->n_ticks : 400;

    tick_no = 0;

    /*
     * Same seed selects the same map.
     * This keeps all participants on the same map.
     */
    stage_rng_t map_rng;
    stage_rng_seed(&map_rng, ctx->seed);

    int chosen_map =
        stage_rng_range(&map_rng, 0, 2);

    /*
     * Optional test override:
     * map_variant = 0, 1, or 2
     */
    int forced_map =
        (int)cfg_get("map_variant", -1.0f);

    if (forced_map >= 0 && forced_map <= 2)
        chosen_map = forced_map;

    select_map(chosen_map);

    world_h = (float)WORLD_HEIGHT;
    spawn_y = world_h - 24.0f;
    goal_y = 22.0f;

    body_w = limit(
        cfg_get("body_w", 10.0f),
        4.0f, 14.0f
    );

    move_px = limit(
        cfg_get("step", 7.0f),
        1.0f, 12.0f
    );

    fly_speed = limit(
        cfg_get("fly_speed", 1.8f),
        0.0f, 4.0f
    );

    step_gap = (int)cfg_get(
        "move_gap_ticks", 1.0f
    );

    if (step_gap < 1)
        step_gap = 1;

    stun_ticks = (int)cfg_get(
        "stun_ticks", 6.0f
    );

    if (stun_ticks < 0)
        stun_ticks = 0;

    grace_ticks = (int)cfg_get(
        "grace_ticks", 14.0f
    );

    if (grace_ticks < 0)
        grace_ticks = 0;

    lives_max = (int)cfg_get(
        "lives", 3.0f
    );

    if (lives_max < 1)
        lives_max = 1;

    if (lives_max > 9)
        lives_max = 9;

    sense_range = limit(
        cfg_get("sense_range", 48.0f),
        10.0f, 90.0f
    );

    level_min = limit(
        cfg_get("level_min", 0.3f),
        0.0f, 1.0f
    );

    level_max = limit(
        cfg_get("level_max", 0.6f),
        level_min, 1.0f
    );

    steer_px = limit(
        cfg_get("steer_px", 3.4f),
        0.0f, 8.0f
    );

    deadzone = limit(
        cfg_get("turn_deadzone", 0.05f),
        0.0f, 1.0f
    );

    see_level = limit(
        cfg_get("see_level", 1.0f),
        0.0f, 1.0f
    );

    finish_base = limit(
        cfg_get("finish_base", 75.0f),
        0.0f, 100.0f
    );

    time_bonus = limit(
        cfg_get("time_bonus", 30.0f),
        0.0f, 100.0f
    );

    climb_max = limit(
        cfg_get("climb_max", 70.0f),
        0.0f, 100.0f
    );

    hit_penalty = limit(
        cfg_get("hit_penalty", 5.0f),
        0.0f, 50.0f
    );

    stim_side = 0;
    stim_level = 0.0f;
    walk_now = 0.0f;
    escape_act_tick = -100;
    escape_miss_tick = -100;

    float cam = world_h - PANE_H;

    for (int p = 0; p < STAGE_MAX_PLAYERS; p++) {
        players[p] = (actor_t){
            .x = 47.0f,
            .y = spawn_y,
            .camera = cam,
            .lives = lives_max,
            .last_move = -100
        };
    }

    fly_goal = -1.0f;
    fly_escape_last_tick = -100;

    fly = (actor_t){
        .x = 47.0f,
        .y = spawn_y,
        .camera = cam,
        .lives = lives_max,
        .last_move = -100
    };
}

/* ---------- Player Input ---------- */

void stage_input(int player, int key, double t)
{
    (void)t;

    if (player < 0 || player >= n_players)
        return;

    actor_t *a = &players[player];

    if (tick_no - a->last_move < step_gap)
        return;

    float dx = 0.0f;
    float dy = 0.0f;

    if (key == SK_LEFT)
        dx = -move_px;
    else if (key == SK_RIGHT)
        dx = move_px;
    else if (key == SK_UP)
        dy = -move_px;
    else if (key == SK_DOWN)
        dy = move_px;
    else
        return;

    step_actor(a, player, dx, dy);
    a->last_move = tick_no;
}

/* ---------- Fly Route Prediction ---------- */

/*
 * Counts the climbing ticks that stay clear on the way
 * to column x. The fly does not climb while it steps
 * sideways, so those ticks are not counted.
 */
static int fly_clear_ticks(float x)
{
    float fx = fly.x;
    float fy = fly.y;
    int k = 0;

    for (int n = 0;
         n < LOOK_TICKS * 2 && k < LOOK_TICKS;
         n++) {
        float off = x - fx;

        if (absv(off) > STEER_BAND) {
            fx += off > 0.0f
                ? steer_px : -steer_px;

            if (hits_web(fx, fy))
                return k;

            continue;
        }

        fy = limit(
            fy - fly_speed,
            goal_y,
            spawn_y
        );

        if (hits_web(fx, fy))
            return k;

        if (fy <= goal_y &&
            (fx < 31.0f || fx > 63.0f)) {
            return k;
        }

        k++;
    }

    return k;
}

static float fly_target(void)
{
    if (fly_goal >= 0.0f &&
        fly_clear_ticks(fly_goal) ==
            LOOK_TICKS) {
        return fly_goal;
    }

    float best_x = fly.x;
    float best_cost = 1.0e9f;

    for (int col = 9; col <= 85; col += 2) {
        float x = (float)col;

        float cost =
            (float)(
                LOOK_TICKS - fly_clear_ticks(x)
            ) * 10.0f
            + absv(x - fly.x) * 0.06f
            + absv(x - 47.0f) * 0.01f;

        if (cost < best_cost) {
            best_cost = cost;
            best_x = x;
        }
    }

    fly_goal = best_x;
    return best_x;
}

/* ---------- Fly Brain and Movement ---------- */

static void fly_tick(void)
{
    stim_side = 0;
    stim_level = 0.0f;

    /*
     * Brain outputs reflect prior stimulation.
     */
    float wl = brain_rate(BR_WALK_L);
    float wr = brain_rate(BR_WALK_R);

    walk_now = limit(wl > wr ? wl : wr, 0.0f, 1.0f);

    if (fly.lives <= 0 ||
        fly.finished ||
        tick_no < fly.stun_until) {
        return;
    }

    float target = fly_target();
    float off = target - fly.x;

    float turn =
        brain_rate(BR_TURN_R) -
        brain_rate(BR_TURN_L);

    int escape = brain_fired(BR_ESCAPE);

    if (absv(off) <= STEER_BAND) {
        /*
         * The way up is open: show it as an object on
         * the right only. Both sides at once wake only the
         * left walk in the real brain, and a left object
         * also raises the right turn there (DNa01), which
         * steps the fly right into a web when it next has
         * to go left. The way up is straight ahead here,
         * so the side does not matter for the route.
         */
        brain_stimulate(BR_OBJECT_R, see_level);

        stim_side = 1;
        stim_level = see_level;

        /*
         * Climbing speed is the walk reaction. The fly
         * only climbs here, never while stepping sideways,
         * so it follows the path fly_clear_ticks checked.
         */
        step_actor(
            &fly,
            PANE_FLY,
            0.0f,
            -fly_speed * walk_now
        );
    }
    else {
        float intensity = limit(
            absv(off) / sense_range,
            0.0f, 1.0f
        );

        float level =
            level_min +
            (level_max - level_min) * intensity;

        brain_stimulate(
            off < 0.0f
                ? BR_LOOM_R
                : BR_LOOM_L,
            level
        );

        stim_side = off < 0.0f ? 1 : -1;
        stim_level = level;

        if (absv(turn) > deadzone) {
            float dx =
                turn > 0.0f
                ? steer_px : -steer_px;

            step_actor(
                &fly,
                PANE_FLY,
                dx,
                0.0f
            );

            fly.last_move = tick_no;
        }

        /*
         * Additional escape step only if
         * the brain fires and cooldown has ended.
         */
        int escape_ready =
            escape &&
            tick_no - fly_escape_last_tick >=
                ESCAPE_COOLDOWN;

        if (escape_ready &&
            fly.lives > 0 &&
            !fly.finished &&
            tick_no >= fly.stun_until) {

            float dx =
                off > 0.0f
                ? steer_px : -steer_px;

            if (!hits_web(
                    fly.x + dx,
                    fly.y
                )) {
                step_actor(
                    &fly,
                    PANE_FLY,
                    dx,
                    0.0f
                );

                fly_escape_last_tick = tick_no;
                fly.last_move = tick_no;
                escape_act_tick = tick_no;
            }
            else {
                escape_miss_tick = tick_no;
            }
        }
        else if (escape_ready && !fly.finished) {
            /* Stunned by the turn step this tick. */
            escape_miss_tick = tick_no;
        }
    }
}

/* ---------- Stage Tick ---------- */

void stage_tick(double dt)
{
    (void)dt;

    fly_tick();

    /* Flash my pane the moment the fly passes me. */
    for (int p = 0; p < n_players; p++) {
        actor_t *a = &players[p];

        if (a->ahead && a->best < fly.best)
            fx_flash(p, C_WARN, 60);

        a->ahead = a->best > fly.best;
    }

    for (int p = 0; p < n_players; p++)
        camera_update(&players[p]);

    camera_update(&fly);

    tick_no++;
}

/* ---------- Spiderweb Rendering ---------- */

static void draw_web(
    canvas_t *c,
    const web_t *w,
    int cam
)
{
    int y0 = w->y - cam;
    int y1 = y0 + w->h;

    if (y1 <= 0 || y0 >= PANE_H)
        return;

    for (int sy = y0; sy < y1; sy++) {
        if (sy < 0 || sy >= PANE_H)
            continue;

        int wy = sy + cam;

        for (int x = w->x;
             x < w->x + w->w;
             x++) {

            if (x < 0 || x >= PANE_W)
                continue;

            int a = (x + wy) % 8;
            int b =
                ((x - wy) % 8 + 8) % 8;

            int vertical =
                (x % 12) == 0;

            int horizontal =
                (wy % 12) == 0;

            if ((a == 0 && b == 0) ||
                (vertical && horizontal)) {
                ui_px(
                    c, x, sy, C_WHITE
                );
            }
            else if (a == 0 ||
                     b == 0 ||
                     vertical ||
                     horizontal) {
                ui_px(
                    c, x, sy, C_WEB
                );
            }
            else if (
                ((x / 5 + wy / 5) & 1) == 0
            ) {
                ui_px(
                    c, x, sy, C_GRAY1
                );
            }
        }
    }

    ui_rect(
        c, w->x, y0,
        w->w, 1, C_GRAY2
    );

    ui_rect(
        c, w->x, y1 - 1,
        w->w, 1, C_GRAY2
    );
}

/* ---------- Actor Rendering ---------- */

static void draw_actor(
    canvas_t *c,
    const actor_t *a,
    int is_fly
)
{
    int sy = (int)(a->y - a->camera);

    int hit =
        tick_no < a->stun_until ||
        a->lives <= 0;

    /* Blink while webs cannot take a life. */
    int blink =
        !hit &&
        tick_no < a->safe_until &&
        (tick_no / 2) % 2 != 0;

    if (is_fly) {
        sprite_id_t id =
            hit ? SPR_FLY_HIT : SPR_FLY_IDLE;

        if (blink)
            return;

        ui_sprite(
            c,
            id,
            (int)a->x -
                ui_sprite_w(id) / 2,
            sy - ui_sprite_h(id) / 2,
            0
        );
    }
    else {
        int l = (int)(
            a->x - body_w / 2.0f
        );

        if (blink) {
            int t = sy - BODY_H / 2;
            int w = (int)body_w;

            ui_rect(c, l, t, w, 1, C_WHITE);
            ui_rect(c, l, t + BODY_H - 1, w, 1, C_WHITE);
            ui_rect(c, l, t, 1, BODY_H, C_WHITE);
            ui_rect(c, l + w - 1, t, 1, BODY_H, C_WHITE);
            return;
        }

        ui_rect(
            c,
            l,
            sy - BODY_H / 2,
            (int)body_w,
            BODY_H,
            hit ? C_BAD : C_GOOD
        );

        ui_px(
            c,
            (int)a->x - 2,
            sy - 1,
            C_BLACK
        );

        ui_px(
            c,
            (int)a->x + 2,
            sy - 1,
            C_BLACK
        );
    }
}

/* ---------- Fly Brain Display ---------- */

/*
 * Stimulus bar on the side the loom or the object
 * was given, walk reaction bar under the feet,
 * and "!" (jumped) or "?" (blocked) above the head
 * for a few ticks after the escape reaction.
 */
static void draw_fly_brain(canvas_t *c)
{
    int sy = (int)(fly.y - fly.camera);
    int fx = (int)fly.x;
    int hw = ui_sprite_w(SPR_FLY_IDLE) / 2;
    int hh = ui_sprite_h(SPR_FLY_IDLE) / 2;

    if (stim_side != 0) {
        int h = (int)(stim_level * 12.0f);

        if (h < 1)
            h = 1;

        int bx = stim_side > 0
            ? fx + hw + 1
            : fx - hw - 3;

        ui_rect(c, bx, sy - h / 2, 2, h, C_WARN);
    }

    ui_bar(c, fx - hw, sy + hh + 1, hw * 2, walk_now, C_CYAN);

    int since_act = tick_no - escape_act_tick;
    int since_miss = tick_no - escape_miss_tick;

    const char *mark = 0;
    color_t col = C_WARN;

    if (since_act <= REACT_SHOW_TICKS &&
        since_act <= since_miss) {
        mark = "!";
    }
    else if (since_miss <= REACT_SHOW_TICKS) {
        mark = "?";
        col = C_GRAY3;
    }

    if (mark) {
        ui_text(
            c,
            fx - ui_text_w(mark) / 2,
            sy - hh - UI_FONT_H - 1,
            mark,
            col
        );
    }
}

/* ---------- Scene Rendering ---------- */

static void draw_scene(
    canvas_t *c,
    const actor_t *a,
    int is_fly
)
{
    int cam = (int)a->camera;

    ui_clear(c, C_BG);

    for (int i = 0; i < 20; i++) {
        int wy = i * 23 + 8;
        int sy = wy - cam;

        if (sy >= 0 && sy < PANE_H) {
            ui_px(
                c,
                5 + (i * 17) % 84,
                sy,
                C_GRAY1
            );
        }
    }

    for (int i = 0; i < N_WEBS; i++)
        draw_web(c, &webs[i], cam);

    int gy = (int)goal_y - cam;

    if (gy > -14 &&
        gy < PANE_H + 10) {

        ui_rect(
            c,
            30,
            gy - 9,
            34,
            5,
            C_GOOD
        );

        ui_text(
            c,
            (PANE_W -
                ui_text_w("GOAL")) / 2,
            gy - 20,
            "GOAL",
            C_WHITE
        );
    }

    draw_actor(c, a, is_fly);

    if (is_fly)
        draw_fly_brain(c);

    /*
     * Climbing progress, bottom right next to the lives.
     * At the top it ran into the GOAL text.
     */
    int bar_x = PANE_W - 48;
    int bar_y = PANE_H - 7;

    ui_bar(
        c,
        bar_x, bar_y, 40,
        a->best,
        C_GOOD
    );

    /* Where the fly is on my progress bar. */
    if (!is_fly) {
        ui_rect(
            c,
            bar_x + (int)(fly.best * 40.0f) - 1,
            bar_y - 2,
            2, 8,
            C_WING
        );
    }

    /* Remaining lives */
    for (int i = 0;
         i < lives_max;
         i++) {

        ui_rect(
            c,
            4 + 8 * i,
            PANE_H - 8,
            6, 5,
            i < a->lives
                ? C_BAD : C_GRAY2
        );
    }

    if (a->finished) {
        ui_rect(
            c,
            7,
            39,
            PANE_W - 14,
            24,
            C_GREEN
        );

        ui_text(
            c,
            (PANE_W -
                ui_text_w("FINISH!")) / 2,
            46,
            "FINISH!",
            C_WHITE
        );
    }
    else if (a->lives <= 0) {
        ui_text(
            c,
            (PANE_W -
                ui_text_w("탈락!")) / 2,
            43,
            "탈락!",
            C_BAD
        );
    }
}

/* ---------- Stage Drawing ---------- */

void stage_draw(
    int player,
    canvas_t *fly_canvas,
    canvas_t *me
)
{
    draw_scene(
        fly_canvas,
        &fly,
        1
    );

    if (player >= 0 &&
        player < n_players) {
        draw_scene(
            me,
            &players[player],
            0
        );
    }
    else {
        ui_clear(me, C_BG);
    }
}

/* ---------- Scoring ---------- */

static float score(const actor_t *a)
{
    float s;

    if (a->finished) {
        float t = limit(
            (float)(ticks - a->finish_tick) /
                (float)ticks,
            0.0f,
            1.0f
        );

        s = finish_base + time_bonus * t;
    }
    else {
        s = a->best * climb_max;
    }

    /* Every web hit costs points, so clean climbs win. */
    s -= hit_penalty *
        (float)(lives_max - a->lives);

    return s > 0.0f ? s : 0.0f;
}

void stage_result(stage_result_t *out)
{
    for (int p = 0;
         p < STAGE_MAX_PLAYERS;
         p++) {

        out->score[p] =
            p < n_players
                ? score(&players[p])
                : 0.0f;
    }

    out->fly_score = score(&fly);
}
