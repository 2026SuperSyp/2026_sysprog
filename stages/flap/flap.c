/* 플래핑 플라이 (뼈대)
 *
 * 기둥이 오른쪽에서 다가온다. 기둥의 틈으로 지나가면 점수를 얻고, 부딪히면 그 기둥의 점수가 없다.
 * 가만히 있으면 떨어지고, 날갯짓 한 번에 위로 튀어 오른다.
 * 유저는 스페이스로 날갯짓을 한다. 초파리는 "아래에서 다가오는 것"(바닥, 아래 기둥)이 가까울수록 센
 * 다가옴 자극을 양쪽으로 받고, 뇌의 도주 반응(BR_ESCAPE)이 켜지면 날갯짓을 한 번 한다.
 *
 * 이 파일은 뼈대다. 규칙 카드부터 판정까지 돌고 make bot과 make test-stage를 통과하지만, 다듬지는 않았다.
 * 구조(유저별 상태, 문제를 씨앗에서 만들기, 자극과 반응, 이펙트, config.ini)는 그대로 쓰고 아래를 채우면 된다.
 *
 * 할 일
 *   1. 그림: 지금은 기둥과 캐릭터를 네모로 그린다. assets/ 에 .spr 그림을 넣고 ui_sprite_load로 불러 바꾼다.
 *   2. 초파리의 조종: fly_think가 주는 자극을 다듬는다. 지금은 "가야 할 높이(다음 틈의 가운데)보다 얼마나
 *      아래에 있는가"만 본다. 떨어지는 빠르기나 위쪽 기둥을 같이 보면 더 잘 난다.
 *      진짜 뇌는 가짜 뇌보다 예민하고(양쪽에 0.15씩이면 날갯짓) 반응이 2~3틱 늦다. 두 뇌에서 모두 날게
 *      loom_gain과 loom_range를 맞춘다(make run STAGE=flap BRAIN=real, make brain-table BRAIN=real).
 *   3. 난이도: config.ini의 숫자(중력, 날갯짓의 세기, 기둥의 빠르기와 간격, 틈의 크기, 점수)를 맞춘다.
 *      잘하면 100점 근처가 되게 한다.
 *   4. 부딪혔을 때: 지금은 틈의 가운데로 옮겨 주고 그 기둥의 점수만 없앤다. 잠깐 멈추게 하거나 점수를 깎는 것도 된다.
 *      스테이지는 일찍 끝낼 수 없다(docs/guide.md의 "한 판의 끝과 생명").
 *   5. 올리기 전에 make bot STAGE=flap 과 make test-stage STAGE=flap BRAIN=real 이 "통과"인지 본다. */
#include "stage.h"
#include "brain.h"

#include <stdio.h>

#define MAX_PILLARS 64
#define BODY_X      24             /* 캐릭터의 가운데 x. 움직이지 않는다 */
#define BODY_R      4              /* 캐릭터의 반 크기 */
#define PILLAR_W    12
#define FLOOR_Y     (PANE_H - 6)

/* 한 명 몫의 상태. 초파리도 같은 모양을 쓴다. 기둥의 위치는 모두에게 같으므로 여기에 두지 않는다 */
typedef struct {
    float y, vy;                   /* 높이(아래로 갈수록 크다)와 빠르기 */
    int   passed;                  /* 지나갔거나 부딪힌 기둥의 수(다음에 볼 기둥의 번호) */
    int   last_flap;               /* 마지막으로 날갯짓한 틱 */
    int   hit_until;               /* 이 틱까지 부딪힌 모습 */
    float score;
} side_t;

static side_t pl[STAGE_MAX_PLAYERS];
static side_t fly;
static int    gap_y[MAX_PILLARS];  /* 문제: 기둥마다 틈의 가운데 높이. 모두에게 같다 */
static int    n_players, tick_no;

/* config.ini에서 읽는 숫자들 */
static float gravity, flap_v, pillar_speed, pillar_spacing, gap_half, score_per_pillar, loom_gain, loom_range;
static int   first_pillar_x, flap_gap, fly_flap_cooldown;

static const stage_info_t info = {
    .title    = "플래핑 플라이",
    .rule     = "기둥 사이로 날아가세요\n부딪히면 점수가 없어요",
    .controls = "스페이스",
};

const stage_info_t *stage_info(void) { return &info; }

void stage_start(const stage_ctx_t *ctx)
{
    n_players = ctx->n_players;

    gravity           = cfg_get("gravity", 0.35f);
    flap_v            = cfg_get("flap_v", 3.2f);
    pillar_speed      = cfg_get("pillar_speed", 2.0f);
    pillar_spacing    = cfg_get("pillar_spacing", 60.0f);
    gap_half          = cfg_get("gap_half", 20.0f);
    first_pillar_x    = (int)cfg_get("first_pillar_x", 120.0f);
    flap_gap          = (int)cfg_get("flap_gap_ticks", 2.0f);
    score_per_pillar  = cfg_get("score_per_pillar", 8.0f);
    loom_gain         = cfg_get("loom_gain", 1.0f);
    loom_range        = cfg_get("loom_range", 24.0f);
    fly_flap_cooldown = (int)cfg_get("fly_flap_cooldown_ticks", 5.0f);
    if (pillar_spacing < PILLAR_W + 2 * BODY_R + 2) pillar_spacing = PILLAR_W + 2 * BODY_R + 2;

    /* 문제는 씨앗에서만 만든다. 틈이 천장이나 바닥에 붙지 않게 범위를 둔다 */
    stage_rng_t rng;
    stage_rng_seed(&rng, ctx->seed);
    int lo = (int)gap_half + 8, hi = FLOOR_Y - (int)gap_half - 8;
    for (int i = 0; i < MAX_PILLARS; i++) gap_y[i] = stage_rng_range(&rng, lo, hi);

    for (int p = 0; p < STAGE_MAX_PLAYERS; p++)
        pl[p] = (side_t){ .y = PANE_H / 2.0f, .last_flap = -100 };
    fly = (side_t){ .y = PANE_H / 2.0f, .last_flap = -100 };
}

/* i번 기둥의 왼쪽 끝 x. 기둥은 틱마다 왼쪽으로 온다. 누가 무엇을 하든 같은 틱에는 같은 자리에 있다 */
static float pillar_x(int i)
{
    return (float)first_pillar_x + (float)i * pillar_spacing - (float)tick_no * pillar_speed;
}

static void flap(side_t *s, int pane, int cooldown)
{
    if (tick_no - s->last_flap < cooldown) return;
    s->vy = -flap_v;
    s->last_flap = tick_no;
    fx_trail(pane, BODY_X, (int)s->y + BODY_R);
}

void stage_input(int player, int key, double t)
{
    if (key == SK_ACT || key == SK_UP) flap(&pl[player], player, flap_gap);
}

/* 한 틱 움직이고, 다음 기둥과 부딪혔는지 지나갔는지 본다. pane은 이펙트를 걸 칸 번호다 */
static void move(side_t *s, int pane)
{
    s->vy += gravity;
    s->y += s->vy;
    if (s->y < BODY_R) { s->y = BODY_R; s->vy = 0.0f; }                   /* 천장 */
    if (s->y > FLOOR_Y - BODY_R) { s->y = FLOOR_Y - BODY_R; s->vy = 0.0f; }   /* 바닥에서는 멈춰 선다 */

    int i = s->passed;
    float x = pillar_x(i);
    int gy = gap_y[i % MAX_PILLARS];
    if (x <= BODY_X + BODY_R && x + PILLAR_W >= BODY_X - BODY_R) {         /* 기둥과 겹치는 동안 */
        if (s->y - BODY_R < (float)gy - gap_half || s->y + BODY_R > (float)gy + gap_half) {
            s->hit_until = tick_no + 8;                                    /* 부딪혔다. 할 일 4 */
            s->y = (float)gy;
            s->vy = 0.0f;
            s->passed++;                                                   /* 이 기둥은 점수 없이 넘어간다 */
            fx_shake(pane, 2, 150);
            fx_flash(pane, C_BAD, 50);
        }
    } else if (x + PILLAR_W < BODY_X - BODY_R) {                           /* 다 지나갔다 */
        s->score += score_per_pillar;
        s->passed++;
        fx_shockwave(pane, BODY_X, (int)s->y);
    }
}

/* 초파리: 가야 할 높이보다 아래에 있을수록 센 다가옴 자극을 양쪽으로 준다(바닥과 아래 기둥이 다가온다).
 * 뇌의 도주 반응이 켜지면 날갯짓을 한 번 한다. brain_fired는 반응하는 동안 계속 1이므로 쿨다운을 둔다 */
static void fly_think(void)
{
    float target = (float)gap_y[fly.passed % MAX_PILLARS];                 /* 다음 틈의 가운데 */
    float below = fly.y - target + gap_half * 0.5f;                        /* 틈의 아래쪽 4분의 1 지점을 넘으면 0보다 크다 */
    if (below > 0.0f) {
        float level = loom_gain * below / loom_range;
        if (level > 1.0f) level = 1.0f;
        brain_stimulate(BR_LOOM_L, level);
        brain_stimulate(BR_LOOM_R, level);
    }
    if (brain_fired(BR_ESCAPE)) flap(&fly, PANE_FLY, fly_flap_cooldown);
}

void stage_tick(double dt)
{
    fly_think();
    move(&fly, PANE_FLY);
    for (int p = 0; p < n_players; p++)
        move(&pl[p], p);
    tick_no++;
}

static void draw_side(canvas_t *c, const side_t *s, int is_fly)
{
    ui_rect(c, 0, FLOOR_Y, PANE_W, 2, C_GRAY2);                            /* 바닥 */
    for (int i = s->passed > 0 ? s->passed - 1 : 0; i < s->passed + 3; i++) {      /* 화면에 걸친 기둥들 */
        int x = (int)pillar_x(i), gy = gap_y[i % MAX_PILLARS];
        if (x > PANE_W || x + PILLAR_W < 0) continue;
        ui_rect(c, x, 0, PILLAR_W, gy - (int)gap_half, C_GREEN);          /* 할 일 1: 그림으로 바꾼다 */
        ui_rect(c, x, gy + (int)gap_half, PILLAR_W, FLOOR_Y - gy - (int)gap_half, C_GREEN);
    }

    int hit = tick_no < s->hit_until, flapping = tick_no - s->last_flap < 4;
    if (is_fly) {
        sprite_id_t id = hit ? SPR_FLY_HIT : (flapping ? SPR_FLY_JUMP : SPR_FLY_IDLE);
        ui_sprite(c, id, BODY_X - ui_sprite_w(id) / 2, (int)s->y - ui_sprite_h(id) / 2, 0);
    } else {
        ui_rect(c, BODY_X - BODY_R, (int)s->y - BODY_R, 2 * BODY_R, 2 * BODY_R, hit ? C_BAD : C_GOOD);   /* 할 일 1 */
        if (flapping) ui_rect(c, BODY_X - BODY_R - 3, (int)s->y - 1, 3, 2, C_WING);
    }
    char line[24];
    snprintf(line, sizeof line, "%d", (int)(s->score + 0.5f));
    ui_text(c, 3, 3, line, C_TEXT);
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
