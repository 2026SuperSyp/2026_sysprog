
/* Spider web escape: fixed 2D obstacle field.
 * W/A/S/D: player movement in world coordinates.
 * Camera follows only when the character climbs upward.
 * Fly: automatic forward movement + brain-driven turning.
 */
#include "stage.h"
#include "brain.h"

#define BODY_H 9

typedef struct {
    int x, y, w, h;
} web_t;

typedef struct {
    float x, y, camera, best;
    int lives, stun_until, safe_until, last_move;
    int finished, finish_tick;
} actor_t;

/* Fixed map data: y=0 is the top of the world. */
static const web_t webs[] = {

    /* 맵 양쪽 끝을 막는 세로 거미줄 벽 */
    {0, 0, 8, 420},
    {86, 0, 8, 420},



    { 8, 340, 34, 10}, {66, 325, 23, 14},
    {30, 288, 45,  9},
    { 0, 257, 27, 15}, {51, 251, 39, 12},
    {17, 215, 23, 18}, {56, 205, 22, 20},
    { 0, 172, 50,  8}, {72, 159, 22, 14},
    {32, 139, 26, 14},
    { 5, 105, 31, 18}, {60, 99, 32, 13},
    {34,  71, 45, 10},
    { 0,  42, 25, 10}, {60, 37, 25, 10}
};

#define N_WEBS ((int)(sizeof(webs) / sizeof(webs[0])))

static actor_t players[STAGE_MAX_PLAYERS], fly;
static int n_players, ticks, tick_no;
static int lives_max, step_gap, stun_ticks, grace_ticks;

static float world_h, spawn_y, goal_y;
static float body_w, move_px, fly_speed;
static float sense_range, level_min, level_max;
static float steer_px, deadzone;

/* Fly steering: how many ticks ahead its path is checked, how close to
 * the chosen column it stops steering, and the column it is heading for
 * (negative: none yet).
 */
#define LOOK_TICKS 45
#define STEER_BAND 3.0f
static float fly_goal;

static const stage_info_t info = {
    .title = "거미줄 탈출",
    .rule = "거미줄을 피해 올라가요\n위쪽 출구에 도착하기",
    .controls = "W A S D 이동"
};

const stage_info_t *stage_info(void) {
    return &info;
}

static float limit(float v, float lo, float hi) {
    return v < lo ? lo : (v > hi ? hi : v);
}

static float absv(float v) {
    return v < 0 ? -v : v;
}

/* Rectangle collision between actor and fixed web objects. */
static int hits_web(float x, float y) {
    float l = x - body_w * 0.5f;
    float r = x + body_w * 0.5f;
    float t = y - BODY_H * 0.5f;
    float b = y + BODY_H * 0.5f;

    if (l < 2 || r > PANE_W - 2)
        return 1;

    for (int i = 0; i < N_WEBS; i++) {
        const web_t *w = &webs[i];

        if (l < w->x + w->w &&
            r > w->x &&
            t < w->y + w->h &&
            b > w->y)
            return 1;
    }
    return 0;
}

static void update_progress(actor_t *a) {
    float p = limit(
        (spawn_y - a->y) / (spawn_y - goal_y),
        0.0f, 1.0f
    );

    if (p > a->best)
        a->best = p;

    if (a->y <= goal_y &&
        a->x >= 31.0f &&
        a->x <= 63.0f &&
        !a->finished) {
        a->finished = 1;
        a->finish_tick = tick_no;
    }
}

/* Substeps prevent tunneling through thin obstacles. */
static void step_actor(
    actor_t *a, int pane, float dx, float dy
) {
    if (a->lives <= 0 ||
        a->finished ||
        tick_no < a->stun_until)
        return;

    float m = absv(dx) > absv(dy) ? absv(dx) : absv(dy);
    int n = (int)m + 1;
    if (n < 1) n = 1;

    for (int k = 0; k < n; k++) {
        float nx = limit(
            a->x + dx / (float)n,
            body_w * 0.5f + 2,
            PANE_W - body_w * 0.5f - 2
        );
        float ny = limit(
            a->y + dy / (float)n,
            goal_y, spawn_y
        );

        if (hits_web(nx, ny)) {
            /* For a short while after a hit the web only blocks.
             * Otherwise a held key walks into the same web again
             * the moment the stun ends and takes every life in
             * about half a second.
             */
            if (tick_no >= a->safe_until) {
                a->lives--;
                a->stun_until = tick_no + stun_ticks;
                a->safe_until = a->stun_until + grace_ticks;
                fx_flash(pane, C_BAD, 70);
                fx_shake(pane, 2, 140);
            }
            break;
        }

        a->x = nx;
        a->y = ny;
    }

    update_progress(a);
}

/* Camera stays fixed until character reaches upper area. */
static void camera_update(actor_t *a) {
    float top = a->y - a->camera;

    if (top < 29.0f) {
        a->camera = limit(
            a->y - 29.0f,
            0.0f,
            world_h - PANE_H
        );
    }
}

void stage_start(const stage_ctx_t *ctx) {
    n_players = ctx->n_players;
    if (n_players < 1) n_players = 1;
    if (n_players > STAGE_MAX_PLAYERS)
        n_players = STAGE_MAX_PLAYERS;

    ticks = ctx->n_ticks > 0 ? ctx->n_ticks : 400;
    tick_no = 0;

    world_h = 420.0f;
    spawn_y = world_h - 24.0f;
    goal_y = 22.0f;

    body_w = limit(
        cfg_get("body_w", 10.0f), 4.0f, 14.0f
    );
    move_px = limit(
        cfg_get("step", 7.0f), 1.0f, 12.0f
    );
    fly_speed = limit(
        cfg_get("fly_speed", 1.1f), 0.0f, 4.0f
    );

    step_gap = (int)cfg_get("move_gap_ticks", 1.0f);
    if (step_gap < 1) step_gap = 1;

    stun_ticks = (int)cfg_get("stun_ticks", 6.0f);
    if (stun_ticks < 0) stun_ticks = 0;

    grace_ticks = (int)cfg_get("grace_ticks", 14.0f);
    if (grace_ticks < 0) grace_ticks = 0;

    lives_max = (int)cfg_get("lives", 3.0f);
    if (lives_max < 1) lives_max = 1;
    if (lives_max > 9) lives_max = 9;

    sense_range = limit(
        cfg_get("sense_range", 48.0f), 10.0f, 90.0f
    );
    level_min = limit(
        cfg_get("level_min", 0.3f), 0.0f, 1.0f
    );
    level_max = limit(
        cfg_get("level_max", 0.6f), level_min, 1.0f
    );
    steer_px = limit(
        cfg_get("steer_px", 2.5f), 0.0f, 8.0f
    );
    deadzone = limit(
        cfg_get("turn_deadzone", 0.05f), 0.0f, 1.0f
    );

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
    fly = (actor_t){
        .x = 47.0f,
        .y = spawn_y,
        .camera = cam,
        .lives = lives_max,
        .last_move = -100
    };
}

void stage_input(int player, int key, double t) {
    (void)t;

    if (player < 0 || player >= n_players)
        return;

    actor_t *a = &players[player];

    if (tick_no - a->last_move < step_gap)
        return;

    float dx = 0, dy = 0;

    if (key == SK_LEFT) dx = -move_px;
    else if (key == SK_RIGHT) dx = move_px;
    else if (key == SK_UP) dy = -move_px;
    else if (key == SK_DOWN) dy = move_px;
    else return;

    step_actor(a, player, dx, dy);
    a->last_move = tick_no;
}

/* Walk the fly toward column x the way fly_tick moves it (sideways
 * steer_px per tick until it is inside the steering band, climbing
 * fly_speed per tick) and count the ticks before it would touch a web.
 * LOOK_TICKS means the whole path is clear.
 *
 * The earlier version only sampled points straight above each column.
 * It picked columns whose sideways path was still blocked by the web
 * the fly was passing, so the fly lost every life at the first web.
 */
static int fly_clear_ticks(float x) {
    float fx = fly.x, fy = fly.y;

    for (int k = 0; k < LOOK_TICKS; k++) {
        float off = x - fx;

        if (absv(off) > STEER_BAND) {
            fx += off > 0 ? steer_px : -steer_px;

            if (hits_web(fx, fy))
                return k;
        }

        fy = limit(fy - fly_speed, goal_y, spawn_y);

        if (hits_web(fx, fy))
            return k;

        /* The top row only counts inside the exit. */
        if (fy <= goal_y && (fx < 31.0f || fx > 63.0f))
            return k;
    }

    return LOOK_TICKS;
}

/* Find a free column in front of the fly.
 * The fly brain still determines actual turning.
 */
static float fly_target(void) {
    /* Keep the chosen column while its path stays clear. Changing
     * sides every tick would make the fly act on the turn the brain
     * produced for the previous side.
     */
    if (fly_goal >= 0.0f &&
        fly_clear_ticks(fly_goal) == LOOK_TICKS)
        return fly_goal;

    float best_x = fly.x;
    float best_cost = 1.0e9f;

    for (int col = 9; col <= 85; col += 2) {
        float x = (float)col;
        float cost =
            (float)(LOOK_TICKS - fly_clear_ticks(x)) * 10.0f +
            absv(x - fly.x) * 0.06f +
            absv(x - 47.0f) * 0.01f;

        if (cost < best_cost) {
            best_cost = cost;
            best_x = x;
        }
    }

    fly_goal = best_x;
    return best_x;
}

static void fly_tick(void) {
    if (fly.lives <= 0 ||
        fly.finished ||
        tick_no < fly.stun_until)
        return;

    float target = fly_target();
    float off = target - fly.x;

    if (absv(off) > STEER_BAND) {
        float intensity = limit(
            absv(off) / sense_range,
            0.0f, 1.0f
        );

        float level =
            level_min +
            (level_max - level_min) * intensity;

        brain_stimulate(
            off < 0 ? BR_LOOM_R : BR_LOOM_L,
            level
        );

        float turn =
            brain_rate(BR_TURN_R) -
            brain_rate(BR_TURN_L);

        if (absv(turn) > deadzone) {
            step_actor(
                &fly, PANE_FLY,
                turn > 0 ? steer_px : -steer_px,
                0
            );
            fly.last_move = tick_no;
        }
    }

    step_actor(&fly, PANE_FLY, 0, -fly_speed);
}

void stage_tick(double dt) {
    (void)dt;

    fly_tick();

    for (int p = 0; p < n_players; p++)
        camera_update(&players[p]);

    camera_update(&fly);
    tick_no++;
}

/* Render crossed spiderweb threads inside each obstacle. */
static void draw_web(
    canvas_t *c, const web_t *w, int cam
) {
    int y0 = w->y - cam;
    int y1 = y0 + w->h;

    if (y1 <= 0 || y0 >= PANE_H)
        return;

    for (int sy = y0; sy < y1; sy++) {
        if (sy < 0 || sy >= PANE_H)
            continue;

        int wy = sy + cam;

        for (int x = w->x; x < w->x + w->w; x++) {
            if (x < 0 || x >= PANE_W)
                continue;

            int a = (x + wy) % 8;
            int b = ((x - wy) % 8 + 8) % 8;

            int vertical = (x % 12) == 0;
            int horizontal = (wy % 12) == 0;

            if ((a == 0 && b == 0) ||
                (vertical && horizontal)) {
                ui_px(c, x, sy, C_WHITE);
            }
            else if (a == 0 || b == 0 ||
                     vertical || horizontal) {
                ui_px(c, x, sy, C_WEB);
            }
            else if (((x / 5 + wy / 5) & 1) == 0) {
                ui_px(c, x, sy, C_GRAY1);
            }
        }
    }

    ui_rect(c, w->x, y0, w->w, 1, C_GRAY2);
    ui_rect(c, w->x, y1 - 1, w->w, 1, C_GRAY2);
}

static void draw_actor(
    canvas_t *c, const actor_t *a, int is_fly
) {
    int sy = (int)(a->y - a->camera);
    int hit =
        tick_no < a->stun_until || a->lives <= 0;

    if (is_fly) {
        sprite_id_t id =
            hit ? SPR_FLY_HIT : SPR_FLY_IDLE;

        ui_sprite(
            c, id,
            (int)a->x - ui_sprite_w(id) / 2,
            sy - ui_sprite_h(id) / 2,
            0
        );
    }
    else {
        int l = (int)(a->x - body_w / 2);

        ui_rect(
            c, l, sy - BODY_H / 2,
            (int)body_w, BODY_H,
            hit ? C_BAD : C_GOOD
        );

        ui_px(c, (int)a->x - 2, sy - 1, C_BLACK);
        ui_px(c, (int)a->x + 2, sy - 1, C_BLACK);
    }
}

static void draw_scene(
    canvas_t *c, const actor_t *a, int is_fly
) {
    int cam = (int)a->camera;
    ui_clear(c, C_BG);

    /* Background decoration, without marking a route. */
    for (int i = 0; i < 20; i++) {
        int wy = i * 23 + 8;
        int sy = wy - cam;

        if (sy >= 0 && sy < PANE_H)
            ui_px(c, 5 + (i * 17) % 84, sy, C_GRAY1);
    }

    /* Stationary obstacles in world coordinates. */
    for (int i = 0; i < N_WEBS; i++)
        draw_web(c, &webs[i], cam);

    /* Goal is visible only near the map top. */
    int gy = (int)goal_y - cam;

    if (gy > -14 && gy < PANE_H + 10) {
        ui_rect(c, 30, gy - 9, 34, 5, C_GOOD);

        ui_text(
            c,
            (PANE_W - ui_text_w("GOAL")) / 2,
            gy - 20,
            "GOAL",
            C_WHITE
        );
    }

    draw_actor(c, a, is_fly);

    ui_bar(c, 4, 3, 40, a->best, C_GOOD);

    for (int i = 0; i < lives_max; i++) {
        ui_rect(
            c, 4 + 8 * i, PANE_H - 8, 6, 5,
            i < a->lives ? C_BAD : C_GRAY2
        );
    }

    if (a->finished) {
        ui_rect(
            c, 7, 39, PANE_W - 14, 24, C_GREEN
        );

        ui_text(
            c,
            (PANE_W - ui_text_w("FINISH!")) / 2,
            46,
            "FINISH!",
            C_WHITE
        );
    }
    else if (a->lives <= 0) {
        ui_text(
            c,
            (PANE_W - ui_text_w("탈락!")) / 2,
            43,
            "탈락!",
            C_BAD
        );
    }
}

void stage_draw(
    int player, canvas_t *fly_canvas, canvas_t *me
) {
    draw_scene(fly_canvas, &fly, 1);

    if (player >= 0 && player < n_players)
        draw_scene(me, &players[player], 0);
    else
        ui_clear(me, C_BG);
}

static float score(const actor_t *a) {
    if (a->finished) {
        float t = limit(
            (float)(ticks - a->finish_tick) /
            (float)ticks,
            0.0f, 1.0f
        );

        return 90.0f + 10.0f * t;
    }

    return a->best * 80.0f;
}


void stage_result(stage_result_t *out)
{
    /* 참가자와 초파리를 같은 규칙으로 채점한다: 출구에 닿으면 90~100점(빨리 닿을수록 높다),
     * 못 닿으면 올라간 만큼 0~80점. 출구에 닿은 쪽이 못 닿은 쪽을 항상 이기고, 못 닿은 사람도
     * 올라간 만큼은 점수를 받는다. 초파리는 늘 조금은 올라가므로 가만히 있던 참가자와 0:0으로
     * 비기지 않는다 */
    for (int p = 0; p < STAGE_MAX_PLAYERS; p++)
        out->score[p] = p < n_players ? score(&players[p]) : 0.0f;

    out->fly_score = score(&fly);
}

