/* 예제 스테이지: 떨어지는 돌 피하기
 *
 * 새 스테이지를 만들 때 이 폴더를 통째로 복사해서 시작하면 된다.
 * 이 파일이 보여 주는 것:
 *   1. 유저별 상태와 초파리 상태를 나눠 두는 법 (side_t)
 *   2. 키 입력과 마우스 입력 받기
 *   3. 초파리에게 자극을 주고 반응을 읽기 (brain_fired와 쿨다운)
 *   4. 스테이지 전용 그림 불러오기
 *   5. 사건이 난 틱에 이펙트 부르기
 *   6. 난이도 숫자를 config.ini에서 읽기
 *   7. 씨앗으로 문제를 만들어 초파리와 유저에게 똑같이 주기
 *   8. 초파리 칸에 뇌가 받은 자극과 반응을 그리기 */
#include "stage.h"
#include "brain.h"

#include <stdio.h>

#define MAX_ROCKS 64
#define ROCK_W    30           /* 돌의 폭 */
#define BODY_W    16           /* 캐릭터의 폭 */
#define FLOOR_Y   (PANE_H - 14)

/* 한 명 몫의 상태. 초파리도 같은 모양을 쓴다. */
typedef struct {
    float x;                   /* 캐릭터의 가운데 x */
    int   rock;                /* 지금 떨어지는 돌의 번호 */
    float rock_y;              /* 그 돌의 y */
    int   last_move;           /* 마지막으로 움직인 틱 */
    int   hit_until;           /* 이 틱까지 맞은 모습 */
    int   near_until;          /* 이 틱까지 "아슬!" 글자 */
    float target_x;            /* 마우스가 가리킨 x */
    int   has_target;          /* 1이면 target_x로 가는 중 */
    float score;
} side_t;

static side_t pl[STAGE_MAX_PLAYERS];   /* 유저별 상태 */
static side_t fly;                     /* 초파리 상태 */
static int    rock_x[MAX_ROCKS];       /* 문제: 돌이 떨어지는 자리. 모두에게 같다 */
static float  rock_v[MAX_ROCKS];       /* 문제: 그 돌이 한 틱에 떨어지는 픽셀 수 */
static float  rock_from[MAX_ROCKS];    /* 문제: 그 돌이 나타나는 높이. 낮게 나타날수록 빨리 닿는다 */
static int    n_players, tick_no;
static sprite_id_t spr_rock;

/* 초파리 칸의 뇌 표시. stage_tick에서 적고 그리기는 읽기만 한다. */
static int    stim_side;               /* 이번 틱에 준 다가옴 자극의 쪽. -1 왼쪽, 1 오른쪽, 0 없음 */
static float  stim_level;              /* 그 자극의 세기 */
static int    react_tick = -100;       /* 달아남 반응으로 뛰려 한 틱 */
static int    react_moved;             /* 1이면 뛰었다("!"), 0이면 벽에 막혔다("?") */

/* config.ini에서 읽는 숫자들 */
static float rock_speed_min, rock_speed_max, rock_start_max, step, loom_gain, fly_hop, score_per_dodge, wall_gain, wall_range;
static float rock_ramp, near_margin, near_bonus;
static int   move_gap, hop_cooldown, ramp_rocks;

static const stage_info_t info = {
    .title    = "돌 피하기",
    .rule     = "돌을 피해 초파리를 이기세요\n아슬하게 피하면 점수 더",
    .controls = "A D 또는 마우스",
};

const stage_info_t *stage_info(void) { return &info; }

static float clampf(float v, float lo, float hi) { return v < lo ? lo : (v > hi ? hi : v); }

void stage_start(const stage_ctx_t *ctx)
{
    n_players = ctx->n_players;

    rock_speed_min  = cfg_get("rock_speed_min", 4.0f);
    rock_speed_max  = cfg_get("rock_speed_max", 7.5f);
    rock_start_max  = cfg_get("rock_start_max", 30.0f);
    step            = cfg_get("step", 8.0f);
    move_gap        = (int)cfg_get("move_gap_ticks", 2.0f);
    loom_gain       = cfg_get("loom_gain", 1.3f);
    fly_hop         = cfg_get("fly_hop", 16.0f);
    hop_cooldown    = (int)cfg_get("hop_cooldown_ticks", 8.0f);
    score_per_dodge = cfg_get("score_per_dodge", 2.0f);
    wall_gain       = cfg_get("wall_gain", 0.8f);
    wall_range      = cfg_get("wall_range", 20.0f);
    rock_ramp       = cfg_get("rock_ramp", 0.4f);
    ramp_rocks      = (int)cfg_get("ramp_rocks", 30.0f);
    near_margin     = cfg_get("near_margin", 6.0f);
    near_bonus      = cfg_get("near_bonus", 2.0f);
    if (ramp_rocks < 1) ramp_rocks = 1;

    /* 문제는 씨앗에서만 만든다. 미리 다 만들어 두면 진행 속도가 달라도 같은 문제를 받는다. */
    stage_rng_t rng;
    stage_rng_seed(&rng, ctx->seed);
    for (int i = 0; i < MAX_ROCKS; i++) {
        /* 뒤의 돌일수록 빠르고 낮게 나타난다. ramp_rocks번째 돌부터는 (1 + rock_ramp)배로 같다. */
        float hard = 1.0f + rock_ramp * (i < ramp_rocks ? i : ramp_rocks) / ramp_rocks;
        rock_x[i] = stage_rng_range(&rng, ROCK_W / 2, PANE_W - ROCK_W / 2);
        rock_v[i] = (rock_speed_min + (rock_speed_max - rock_speed_min) * stage_rng_float(&rng)) * hard;
        rock_from[i] = rock_start_max * stage_rng_float(&rng) * hard;
    }

    for (int p = 0; p < STAGE_MAX_PLAYERS; p++)
        pl[p] = (side_t){ .x = PANE_W / 2.0f, .rock_y = rock_from[0], .last_move = -100 };
    fly = (side_t){ .x = PANE_W / 2.0f, .rock_y = rock_from[0], .last_move = -100 };

    spr_rock = ui_sprite_load("rock");
}

static void move_side(side_t *s, float dx)
{
    s->x = clampf(s->x + dx, BODY_W / 2.0f, PANE_W - BODY_W / 2.0f);
}

void stage_input(int player, int key, double t)
{
    side_t *s = &pl[player];
    s->has_target = 0;                                 /* 키를 쓰면 마우스로 가던 것을 멈춘다 */
    /* 이동은 누를 때마다 한 걸음. 걸음 사이에 최소 간격을 둔다. */
    if (tick_no - s->last_move < move_gap) return;
    if (key == SK_LEFT)  { move_side(s, -step); s->last_move = tick_no; }
    if (key == SK_RIGHT) { move_side(s, +step); s->last_move = tick_no; }
}

void stage_pointer(int player, int x, int y, int pressed)
{
    /* 가리킨 곳만 적어 둔다. 움직이는 것은 stage_tick에서 키와 같은 빠르기로 한다.
     * 여기서 바로 움직이면 한 틱에 마우스 사건이 여러 번 올 때 키보다 빨라진다. */
    pl[player].target_x = x;
    pl[player].has_target = 1;
}

/* 마우스가 가리킨 곳으로 한 걸음 다가간다. 걸음과 간격은 키와 같다. */
static void follow_pointer(side_t *s)
{
    if (!s->has_target || tick_no - s->last_move < move_gap) return;
    if (s->target_x < s->x - step / 2)      { move_side(s, -step); s->last_move = tick_no; }
    else if (s->target_x > s->x + step / 2) { move_side(s, +step); s->last_move = tick_no; }
}

/* 돌을 한 틱 떨어뜨린다. pane은 이펙트를 걸 칸 번호다. */
static void fall(side_t *s, int pane)
{
    s->rock_y += rock_v[s->rock % MAX_ROCKS];
    if (s->rock_y < FLOOR_Y) return;

    float dx = rock_x[s->rock % MAX_ROCKS] - s->x;
    if (dx < 0) dx = -dx;
    if (dx < (ROCK_W + BODY_W) / 2.0f) {
        s->hit_until = tick_no + 6;                    /* 맞았다 */
        fx_shake(pane, 2, 150);
        fx_flash(pane, C_BAD, 50);
    } else {
        s->score += score_per_dodge;                   /* 피했다 */
        fx_shockwave(pane, rock_x[s->rock % MAX_ROCKS], FLOOR_Y);
        if (dx < (ROCK_W + BODY_W) / 2.0f + near_margin) {
            s->score += near_bonus;                    /* 아슬하게 피했다 */
            s->near_until = tick_no + 10;
            fx_flash(pane, C_GOOD, 50);
        }
    }
    s->rock++;
    s->rock_y = rock_from[s->rock % MAX_ROCKS];
}

/* 초파리: 다가오는 것을 자극으로 주고, 뇌의 반응대로 움직인다.
 * 다가오는 것은 두 가지다. 돌, 그리고 가까운 벽. 벽을 넣지 않으면 초파리는
 * 돌을 피해 한쪽 벽까지 가서 거기에 붙어 버린다. */
static void fly_think(void)
{
    float dx = rock_x[fly.rock % MAX_ROCKS] - fly.x;
    float near = fly.rock_y / (float)FLOOR_Y;          /* 0: 멀다, 1: 코앞 */
    float adx = dx < 0 ? -dx : dx;
    float from_left = 0.0f, from_right = 0.0f;         /* 왼쪽, 오른쪽에서 다가오는 정도 */

    if (adx < (ROCK_W + BODY_W) / 2.0f + 6) {
        if (dx < 0 || (dx == 0 && fly.rock % 2 == 0)) from_left = loom_gain * near;
        else                                           from_right = loom_gain * near;
    }
    float gap_l = fly.x - BODY_W / 2.0f;               /* 왼쪽 벽까지의 거리 */
    float gap_r = PANE_W - BODY_W / 2.0f - fly.x;
    if (gap_l < wall_range) from_left  += wall_gain * (1.0f - gap_l / wall_range);
    if (gap_r < wall_range) from_right += wall_gain * (1.0f - gap_r / wall_range);

    /* 한 틱에 한쪽만 준다. 양쪽을 같이 주면 방향 전환이 꺼진다. 준 것은 그리기용으로 적어 둔다. */
    stim_side = 0;
    if (from_left > from_right)      { brain_stimulate(BR_LOOM_L, from_left);  stim_side = -1; stim_level = from_left; }
    else if (from_right > from_left) { brain_stimulate(BR_LOOM_R, from_right); stim_side = 1;  stim_level = from_right; }

    /* brain_fired는 반응하는 동안 계속 1이다. 한 번씩만 뛰게 하려고 쿨다운을 둔다. */
    if (brain_fired(BR_ESCAPE) && tick_no - fly.last_move >= hop_cooldown) {
        float turn = brain_rate(BR_TURN_R) - brain_rate(BR_TURN_L);
        /* 방향은 도주보다 늦게 오를 수 있다. 차이가 작으면 이번 틱에는 정하지 않는다. */
        if (turn > 0.1f || turn < -0.1f) {
            float old_x = fly.x;
            move_side(&fly, turn > 0 ? fly_hop : -fly_hop);
            /* 벽에 막혀 못 움직였으면 뛴 것으로 치지 않는다. 잔상도 쿨다운도 없다. */
            react_tick = tick_no;
            react_moved = fly.x != old_x;
            if (react_moved) {
                fx_trail(PANE_FLY, (int)old_x, FLOOR_Y + 4);
                fly.last_move = tick_no;
            }
        }
    }
}

void stage_tick(double dt)
{
    fly_think();
    fall(&fly, PANE_FLY);
    for (int p = 0; p < n_players; p++) {
        follow_pointer(&pl[p]);
        fall(&pl[p], p);
    }
    tick_no++;
}

static void draw_side(canvas_t *c, const side_t *s, int is_fly)
{
    ui_rect(c, 0, FLOOR_Y + 12, PANE_W, 2, C_GRAY2);                           /* 바닥 */

    int rx = rock_x[s->rock % MAX_ROCKS];
    ui_rect(c, rx - ROCK_W / 2, (int)s->rock_y - 6, ROCK_W, 6, C_GRAY3);       /* 돌 */
    ui_sprite(c, spr_rock, rx - ui_sprite_w(spr_rock) / 2, (int)s->rock_y - 6, 0);

    int hit = tick_no < s->hit_until;
    if (is_fly) {
        sprite_id_t id = hit ? SPR_FLY_HIT : (tick_no - s->last_move < 4 ? SPR_FLY_JUMP : SPR_FLY_IDLE);
        ui_sprite(c, id, (int)s->x - ui_sprite_w(id) / 2, FLOOR_Y, 0);

        /* 뇌 표시: 이번 틱에 준 자극은 그 쪽 옆에 막대로(높이 = 세기 x 12),
         * 달아남 반응은 6틱 동안 머리 위에 "!"(뛰었다)나 "?"(벽에 막혔다)로 */
        if (stim_side != 0) {
            int h = (int)(clampf(stim_level, 0.0f, 1.0f) * 12);
            if (h < 1) h = 1;
            int bx = stim_side < 0 ? (int)s->x - 12 : (int)s->x + 10;          /* 벽에 붙어 있으면 칸 안으로 당긴다 */
            ui_rect(c, (int)clampf(bx, 0, PANE_W - 2), FLOOR_Y + 12 - h, 2, h, C_WARN);
        }
        if (tick_no - react_tick <= 6)
            ui_text(c, (int)s->x - 3, FLOOR_Y - 10, react_moved ? "!" : "?", react_moved ? C_WARN : C_GRAY3);
    } else {
        ui_rect(c, (int)s->x - BODY_W / 2, FLOOR_Y, BODY_W, 12, hit ? C_BAD : C_GOOD);
        ui_rect(c, (int)s->x - 4, FLOOR_Y + 3, 2, 2, C_BLACK);
        ui_rect(c, (int)s->x + 2, FLOOR_Y + 3, 2, 2, C_BLACK);
    }

    if (tick_no < s->near_until) {                                             /* 아슬하게 피했다 */
        int w = ui_text_w("아슬!");
        ui_text(c, (int)clampf(s->x - w / 2, 0, PANE_W - w), FLOOR_Y - 26, "아슬!", C_GOOD);
    }
}

void stage_draw(int player, canvas_t *fly_canvas, canvas_t *me)
{
    /* 그리기만 한다. 여기서 상태를 바꾸지 않는다. fly_canvas에는 모두에게 같은 그림을 그린다. */
    draw_side(fly_canvas, &fly, 1);
    draw_side(me, &pl[player], 0);
}

void stage_result(stage_result_t *out)
{
    for (int p = 0; p < n_players; p++)
        out->score[p] = pl[p].score;
    out->fly_score = fly.score;
}
