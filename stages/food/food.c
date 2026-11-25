/* 음식 골라 먹기 (뼈대)
 *
 * 음식이 하나씩 앞에 놓였다가 치워진다. 단것만 먹으면 점수를 얻고, 쓴것이나 섞인 것을 먹으면 점수를 잃는다.
 * 유저는 스페이스로 먹는다. 초파리는 음식의 맛을 자극으로 받고, 뇌의 먹기 반응(BR_FEED)이 켜지면 먹는다.
 *
 * 이 파일은 뼈대다. 규칙 카드부터 판정까지 돌고 make bot과 make test-stage를 통과하지만, 다듬지는 않았다.
 * 구조(유저별 상태, 문제를 씨앗에서 만들기, 자극과 반응, 이펙트, config.ini)는 그대로 쓰고 아래를 채우면 된다.
 *
 * 할 일
 *   1. 그림: 지금은 음식과 캐릭터를 네모로 그린다. assets/ 에 .spr 그림을 넣고 ui_sprite_load로 불러 바꾼다
 *      (형식은 stages/example/assets/rock.spr, 불러오는 법은 stages/example/example.c).
 *   2. 음식 종류: kinds[] 표에 줄을 더하면 된다. 줄마다 뇌에 줄 자극(단맛, 쓴맛, 짠맛, 물)과 먹어도 되는지를 적는다.
 *      진짜 뇌에서 달라지는 점(docs/guide.md의 "가짜 뇌와 진짜 뇌의 차이")을 보고 정한다:
 *      물은 진짜 뇌에서 먹게 만들지 못한다. 짠맛은 단맛이 0.5 이하일 때만 먹기를 줄인다. 쓴맛은 늘 통한다.
 *   3. 난이도: config.ini의 숫자(음식이 놓여 있는 시간, 종류의 비율, 점수)를 맞춘다. 잘하면 100점 근처가 되게 한다.
 *   4. 초파리가 헷갈리는 음식(단맛이 약한 것, 쓴맛이 조금 섞인 것)을 넣으면 사람이 이길 틈이 생긴다.
 *      지금 값으로는 초파리가 거의 틀리지 않는다.
 *   5. 올리기 전에 make bot STAGE=food 와 make test-stage STAGE=food BRAIN=real 이 "통과"인지 본다. */
#include "stage.h"
#include "brain.h"

#include <stdio.h>

#define MAX_FOODS 64
#define PLATE_Y   62            /* 음식이 놓이는 높이 */
#define BODY_Y    (PANE_H - 26) /* 캐릭터의 높이 */

/* 음식의 종류. 뇌에 줄 자극의 세기(0~1)와, 먹으면 점수를 얻는지 잃는지. 세기는 config.ini에서 읽어 채운다 */
enum { SWEET, BITTER, WATER, MIXED, N_KINDS };
typedef struct {
    const char *name;
    float sugar, bitter, salt, water;
    int   good;                 /* 1이면 먹어야 하는 음식 */
    color_t col;
} kind_t;
static kind_t kinds[N_KINDS] = {
    [SWEET]  = { "단것",   0, 0, 0, 0, 1, C_PINK },
    [BITTER] = { "쓴것",   0, 0, 0, 0, 0, C_GREEN },
    [WATER]  = { "물",     0, 0, 0, 0, 0, C_BLUE },
    [MIXED]  = { "섞인것", 0, 0, 0, 0, 0, C_ORANGE },
};

/* 한 명 몫의 상태. 초파리도 같은 모양을 쓴다. 음식이 나오는 때는 모두에게 같으므로 여기에 두지 않는다 */
typedef struct {
    int   acted;                /* 지금 음식을 이미 먹었으면 그 음식의 번호 + 1 */
    int   eat_until;            /* 이 틱까지 먹는 모습 */
    int   hit_until;            /* 이 틱까지 잘못 먹은 모습 */
    float score;
} side_t;

static side_t pl[STAGE_MAX_PLAYERS];
static side_t fly;
static int    food_kind[MAX_FOODS];     /* 문제: 차례로 나오는 음식의 종류. 모두에게 같다 */
static int    n_players, tick_no;

/* config.ini에서 읽는 숫자들 */
static int   food_ticks, gap_ticks, fly_wait_ticks;
static float score_good, penalty_bad;

static const stage_info_t info = {
    .title    = "음식 골라 먹기",
    .rule     = "단것만 골라 먹으세요\n쓴것을 먹으면 감점",
    .controls = "스페이스",
};

const stage_info_t *stage_info(void) { return &info; }

void stage_start(const stage_ctx_t *ctx)
{
    n_players = ctx->n_players;

    food_ticks     = (int)cfg_get("food_ticks", 30.0f);
    gap_ticks      = (int)cfg_get("gap_ticks", 10.0f);
    fly_wait_ticks = (int)cfg_get("fly_wait_ticks", 5.0f);
    score_good     = cfg_get("score_good", 20.0f);
    penalty_bad    = cfg_get("penalty_bad", 10.0f);
    kinds[SWEET].sugar   = cfg_get("sweet_sugar", 1.0f);
    kinds[BITTER].bitter = cfg_get("bitter_bitter", 1.0f);
    kinds[WATER].water   = cfg_get("water_water", 1.0f);
    kinds[MIXED].sugar   = cfg_get("mixed_sugar", 1.0f);
    kinds[MIXED].bitter  = cfg_get("mixed_bitter", 0.8f);
    if (food_ticks < 1) food_ticks = 1;
    if (gap_ticks < 0) gap_ticks = 0;

    /* 문제는 씨앗에서만 만든다. 단것이 절반쯤 나오게 한다(sweet_percent) */
    int sweet_percent = (int)cfg_get("sweet_percent", 50.0f);
    stage_rng_t rng;
    stage_rng_seed(&rng, ctx->seed);
    for (int i = 0; i < MAX_FOODS; i++) {
        if (stage_rng_range(&rng, 1, 100) <= sweet_percent) food_kind[i] = SWEET;
        else food_kind[i] = stage_rng_range(&rng, BITTER, N_KINDS - 1);
    }
}

/* 지금 놓여 있는 음식의 번호. 음식 사이의 빈 시간이면 -1. *since에는 놓인 뒤 지난 틱 수를 준다 */
static int food_now(int *since)
{
    int period = food_ticks + gap_ticks, at = tick_no % period;
    if (since) *since = at;
    return at < food_ticks ? (tick_no / period) % MAX_FOODS : -1;
}

/* 지금 음식을 먹는다. pane은 이펙트를 걸 칸 번호다. 한 음식은 한 번만 먹는다 */
static void eat(side_t *s, int pane)
{
    int food = food_now(NULL);
    if (food < 0 || s->acted == food + 1) return;
    s->acted = food + 1;
    if (kinds[food_kind[food]].good) {
        s->score += score_good;
        s->eat_until = tick_no + 6;
        fx_shockwave(pane, PANE_W / 2, PLATE_Y);
    } else {
        s->score -= penalty_bad;
        if (s->score < 0.0f) s->score = 0.0f;         /* 점수는 0 아래로 내려가지 않는다 */
        s->hit_until = tick_no + 8;
        fx_shake(pane, 2, 150);
        fx_flash(pane, C_BAD, 50);
    }
}

void stage_input(int player, int key, double t)
{
    if (key == SK_ACT) eat(&pl[player], player);
}

/* 초파리: 놓여 있는 음식의 맛을 자극으로 주고, 뇌의 먹기 반응이 켜지면 먹는다.
 * 앞 음식의 반응이 아직 남아 있을 수 있으므로, 음식이 놓이고 fly_wait_ticks가 지난 뒤부터 먹는다 */
static void fly_think(void)
{
    int since, food = food_now(&since);
    if (food < 0) return;
    const kind_t *k = &kinds[food_kind[food]];
    if (k->sugar > 0.0f)  brain_stimulate(BR_SUGAR, k->sugar);
    if (k->bitter > 0.0f) brain_stimulate(BR_BITTER, k->bitter);
    if (k->salt > 0.0f)   brain_stimulate(BR_SALT, k->salt);
    if (k->water > 0.0f)  brain_stimulate(BR_WATER, k->water);
    if (since >= fly_wait_ticks && brain_fired(BR_FEED)) eat(&fly, PANE_FLY);
}

void stage_tick(double dt)
{
    fly_think();
    tick_no++;
}

static void draw_side(canvas_t *c, const side_t *s, int is_fly)
{
    int since, food = food_now(&since);
    ui_rect(c, PANE_W / 2 - 22, PLATE_Y + 12, 44, 3, C_GRAY2);                 /* 접시 */
    if (food >= 0 && s->acted != food + 1) {
        const kind_t *k = &kinds[food_kind[food]];
        ui_rect(c, PANE_W / 2 - 9, PLATE_Y - 6, 18, 18, k->col);               /* 음식. 할 일 1: 그림으로 바꾼다 */
        ui_text(c, (PANE_W - ui_text_w(k->name)) / 2, PLATE_Y - 22, k->name, C_TEXT);
        ui_bar(c, PANE_W / 2 - 20, PLATE_Y + 18, 40, 1.0f - (float)since / (float)food_ticks, C_WARN);   /* 남은 시간 */
    }

    int hit = tick_no < s->hit_until, eating = tick_no < s->eat_until;
    if (is_fly) {
        sprite_id_t id = hit ? SPR_FLY_HIT : (eating ? SPR_FLY_EAT : SPR_FLY_IDLE);
        ui_sprite(c, id, PANE_W / 2 - ui_sprite_w(id) / 2, BODY_Y, 0);
    } else {
        ui_rect(c, PANE_W / 2 - 8, BODY_Y, 16, 14, hit ? C_BAD : (eating ? C_YELLOW : C_GOOD));   /* 할 일 1 */
        ui_rect(c, PANE_W / 2 - 4, BODY_Y + 4, 2, 2, C_BLACK);
        ui_rect(c, PANE_W / 2 + 2, BODY_Y + 4, 2, 2, C_BLACK);
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
