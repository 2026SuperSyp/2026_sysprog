/* 보트 탈출: 초파리에게서 노를 저어 달아나는 협동 스테이지
 *
 * 참가자 모두가 보트 한 척에 탄다. 초파리는 보트를 먹이로 보고 쫓아온다.
 * 노는 차례대로 한 번씩 젓는다. k번째 차례는 참가자 (k % 인원)이 젓고, 노는 왼쪽(k가 짝수), 오른쪽(홀수)이
 * 번갈아 온다. 2명이면 0번은 늘 왼쪽 노(A), 1번은 늘 오른쪽 노(D)다. 혼자이면 A와 D를 번갈아 누른다.
 * 왼쪽 노를 저으면 보트는 앞으로 가며 오른쪽으로 꺾인다. 번갈아 저어야 곧게 간다.
 * 스페이스는 젓지 않고 차례를 넘긴다(같은 쪽을 두 번 저어 방향을 틀 때 쓴다).
 *
 * 뇌가 정하는 것 세 가지
 *  - 쫓기: 보트가 보이는 쪽에 물체 자극을 준다. 걷기와 방향 반응대로 초파리가 온다.
 *  - 물기: 초파리가 보트에 닿으면 단맛 자극을 준다. 먹기 반응이 feed_level 이상이면 물린 것이고 판이 끝난다.
 *          반응은 몇 틱 늦게 오므로 닿은 직후에 한 번 더 저으면 빠져나올 수 있다.
 *          쓴 열매(F, 팀에 bitter_count개)를 쓰면 bitter_ticks 동안 쓴맛을 같이 준다. 뇌가 먹기를 끄면 초파리는 물지 않고 물러난다.
 * 급류: 강의 한 구간(rapid_at부터 rapid_len)은 물살이 빠르다. 보트가 저절로 앞으로 흘러가서 초파리와 벌어지지만,
 * 한쪽 강둑으로도 밀린다(어느 쪽인지는 씨앗이 정한다). 밀리는 반대쪽 노를 한 번 더 저어야 강둑을 피한다.
 * 차례의 제한 시간도 짧아진다. 초파리는 날아오므로 물살을 타지 않는다.
 *
 *  - 놀라기: 차례에 W를 누르면 젓는 대신 노로 물을 뒤로 튀긴다(splash_ticks 동안 다가옴 자극). 뇌의 달아남 반응이
 *          켜지면 초파리가 뒤로 튄다. 왼쪽 노로 튀기면 왼쪽에서 다가오는 것이라 초파리는 오른쪽으로 꺾인다.
 *
 * 다른 스테이지와 다른 점 (팀장 확인 필요)
 *  - 상태가 참가자마다 따로가 아니라 하나다. 점수도 모두 같다. include/stage.h의 "참가자의 결과는 다른
 *    참가자의 입력에 영향받지 않는다"를 일부러 지키지 않는다. 그래서 make bot의 "혼자 돌린 판과 같은 점수"
 *    검사는 config.ini가 아니라 tools/bot.sh의 협동 스테이지 목록으로 건너뛴다.
 *  - 초파리의 걷기는 보스전과 같은 집단을 쓴다: 물체 자극(BR_OBJECT_L/R) -> 걷기(BR_WALK_L/R), 방향(BR_STEER_L/R).
 *    냄새 집단은 두 뇌 모두 반응이 없어서 쓰지 않았다. */
#include "stage.h"
#include "brain.h"

#include <math.h>
#include <stdio.h>

#define MAX_ROCKS  16
#define BOAT_ROW   60            /* 내 칸에서 보트를 그리는 줄 */
#define FLY_ROW    84            /* 초파리 칸에서 초파리를 그리는 줄 */
#define BANK_W     4             /* 강둑의 폭 */
#define HUD_H      17            /* 위쪽 안내 띠의 높이 */
#define PHEW_GAP   20            /* 빠져나옴 번쩍임 사이의 최소 틱 수. 초파리 칸에 먹기 반응을 남기는 틱 수 */
#define PI_F       3.14159265f

enum { MSG_NONE, MSG_GOOD, MSG_EARLY, MSG_PASS, MSG_TIMEOUT, MSG_ROCK, MSG_BANK, MSG_SPLASH, MSG_BITTER, MSG_SPIT, MSG_STARTLE, MSG_RAPID };

/* 보트와 초파리. 방향은 0이 상류(화면 위쪽)이고 오른쪽으로 돌수록 커진다. y는 상류로 갈수록 커진다. */
static float bx, by, bh, bv;
static float fx, fy, fh;
static int   fly_side;           /* 마지막으로 물체 자극을 준 쪽. 0 왼쪽, 1 오른쪽 */
static int   fly_side_at = -1000, side_hold_ticks;       /* 쪽을 바꾼 틱, 바꾼 뒤 유지하는 틱 수 */
static int   fly_moving;
static int   turn_no, turn_start, last_stroke, last_side;
static int   last_wrong[STAGE_MAX_PLAYERS];  /* 참가자마다 차례가 아닌데 눌러 감속된 틱 */
static int   last_early = -1000;             /* 엇박으로 감속된 틱 */
static int   caught, caught_tick, hit_rock = -1;
static int   touching, touch_ticks;          /* 초파리가 보트에 닿아 있다, 닿은 뒤 지난 틱 */
static float feed_now;                       /* 이번 틱의 먹기 반응(그리기용) */
static float touch_feed, phew_feed;          /* 이번에 닿아 있는 동안 가장 컸던 먹기 반응, 빠져나왔을 때의 그 값 */
static int   phew_at = -1000, phew_flash_at = -1000, touch_at = -1000;   /* 물리지 않고 빠져나온 틱, 그때 번쩍인 틱, 마지막으로 닿기 시작한 틱 */
static int   splash_until, splash_side, splash_ready_at;   /* 물을 튀기는 중인 마지막 틱, 튀긴 노, 다시 튀길 수 있는 틱 */
static int   bitter_left, bitter_until;      /* 남은 쓴 열매, 쓴맛이 남아 있는 마지막 틱 */
static int   fly_hop_at = -1000, fly_spit_at = -1000;      /* 초파리가 놀라 튄 틱, 뱉고 물러난 틱 */
static int   msg, msg_until;
/* 뇌 표시(그리기용): 이번 틱에 준 자극의 세기(좌우가 있는 물체와 다가옴, 좌우가 없는 맛),
 * 반응이 행동이 된 틱, 반응은 켜졌는데 막혀 행동하지 못한 틱 */
static float stim_l, stim_r, stim_sugar, stim_bitter;
static int   react_at = -1000, block_at = -1000;
static int   n_players, n_ticks, tick_no;
static float rock_x[MAX_ROCKS], rock_y[MAX_ROCKS];
static int   n_rocks;

static float stroke_power, stroke_turn, drag_keep, early_keep, wrong_keep, start_gap, gap_far;
static float safe_base, safe_far, fly_miss_max;
static float fly_speed, fly_turn, pursue_turn, see_level, center_band, rock_radius, rock_keep, bank_keep;
static int   cooldown_ticks, wrong_gap_ticks, turn_limit_ticks;
static float contact_dist, sugar_level, feed_level, bitter_level, bitter_push;
static float splash_range, splash_level, escape_push, escape_turn;
static int   bitter_count, bitter_ticks, spit_ticks, splash_ticks, splash_cooldown, hop_cooldown;
static float rapid_at, rapid_len, rapid_flow, rapid_drift;
static int   rapid_limit_ticks, rapid_dir, was_in_rapid;   /* rapid_dir: 밀리는 쪽. -1 왼쪽 강둑, 1 오른쪽 강둑 */

static const stage_info_t info = {
    .title    = "보트 탈출",
    .rule     = "차례대로 노를 저어 도망쳐요\n닿으면 물려요 스페이스 넘김",
    .controls = "A D 젓기 W 물튀김 F 열매",
};

const stage_info_t *stage_info(void) { return &info; }

static float clampf(float v, float lo, float hi) { return v < lo ? lo : (v > hi ? hi : v); }
static float gap_now(void) { return sqrtf((bx - fx) * (bx - fx) + (by - fy) * (by - fy)); }

static void say(int m) { msg = m; msg_until = tick_no + 14; }

static void fx_all(void (*f)(int, int, int), int a, int b)
{
    for (int p = 0; p < n_players; p++) f(p, a, b);
}

void stage_start(const stage_ctx_t *ctx)
{
    n_players = ctx->n_players;
    n_ticks   = ctx->n_ticks;

    stroke_power     = cfg_get("stroke_power", 0.45f);
    stroke_turn      = cfg_get("stroke_turn", 0.25f);
    drag_keep        = cfg_get("drag_keep", 0.942f);
    cooldown_ticks   = (int)cfg_get("cooldown_ticks", 6.0f);
    early_keep       = cfg_get("early_keep", 0.8f);
    wrong_keep       = cfg_get("wrong_keep", 0.9f);
    wrong_gap_ticks  = (int)cfg_get("wrong_gap_ticks", 4.0f);
    turn_limit_ticks = (int)cfg_get("turn_limit_ticks", 24.0f);
    start_gap        = cfg_get("start_gap", 34.0f);
    gap_far          = cfg_get("gap_far", 70.0f);
    safe_base        = cfg_get("safe_base", 80.0f);
    safe_far         = cfg_get("safe_far", 20.0f);
    fly_miss_max     = cfg_get("fly_miss_max", 79.0f);
    fly_speed        = cfg_get("fly_speed", 0.78f);
    fly_turn         = cfg_get("fly_turn", 0.05f);
    pursue_turn      = cfg_get("pursue_turn", 0.5f);
    see_level        = cfg_get("see_level", 1.0f);
    center_band      = cfg_get("center_band", 0.15f);
    rock_radius      = cfg_get("rock_radius", 5.0f);
    rock_keep        = cfg_get("rock_keep", 0.6f);
    bank_keep        = cfg_get("bank_keep", 0.7f);
    contact_dist     = cfg_get("contact_dist", 7.0f);
    sugar_level      = cfg_get("sugar_level", 1.0f);
    feed_level       = cfg_get("feed_level", 0.5f);
    bitter_count     = (int)cfg_get("bitter_count", 2.0f);
    bitter_ticks     = (int)cfg_get("bitter_ticks", 60.0f);
    bitter_level     = cfg_get("bitter_level", 0.8f);
    bitter_push      = cfg_get("bitter_push", 22.0f);
    spit_ticks       = (int)cfg_get("spit_ticks", 6.0f);
    splash_ticks     = (int)cfg_get("splash_ticks", 5.0f);
    splash_cooldown  = (int)cfg_get("splash_cooldown_ticks", 30.0f);
    splash_range     = cfg_get("splash_range", 34.0f);
    splash_level     = cfg_get("splash_level", 1.0f);
    escape_push      = cfg_get("escape_push", 16.0f);
    escape_turn      = cfg_get("escape_turn", 0.6f);
    hop_cooldown     = (int)cfg_get("hop_cooldown_ticks", 12.0f);
    side_hold_ticks  = (int)cfg_get("side_hold_ticks", 12.0f);
    rapid_at         = cfg_get("rapid_at", 150.0f);
    rapid_len        = cfg_get("rapid_len", 90.0f);
    rapid_flow       = cfg_get("rapid_flow", 0.12f);
    rapid_drift      = cfg_get("rapid_drift", 0.14f);
    rapid_limit_ticks = (int)cfg_get("rapid_limit_ticks", 16.0f);
    if (rapid_limit_ticks < 1) rapid_limit_ticks = 1;
    float rock_first = cfg_get("rock_first", 70.0f);
    float rock_every = cfg_get("rock_every", 75.0f);
    if (turn_limit_ticks < 1) turn_limit_ticks = 1;
    if (gap_far < 1.0f) gap_far = 1.0f;
    if (fly_miss_max > safe_base - 1.0f) fly_miss_max = safe_base - 1.0f;   /* 달아난 팀이 초파리보다 낮아지지 않게 */
    if (rock_every < 10.0f) rock_every = 10.0f;

    /* 문제(바위 자리)는 씨앗에서만 만든다. */
    stage_rng_t rng;
    stage_rng_seed(&rng, ctx->seed);
    n_rocks = MAX_ROCKS;
    for (int i = 0; i < n_rocks; i++) {
        rock_x[i] = (float)stage_rng_range(&rng, BANK_W + 8, PANE_W - BANK_W - 8);
        rock_y[i] = rock_first + rock_every * (float)i;
    }
    fly_side = (int)(stage_rng_u32(&rng) & 1u);
    rapid_dir = (stage_rng_u32(&rng) & 1u) ? 1 : -1;

    bx = PANE_W / 2.0f;
    by = 0.0f;
    bh = -stroke_turn / 2.0f;      /* 첫 노(왼쪽) 뒤에 좌우로 똑같이 흔들리게 반 번만큼 왼쪽을 보고 시작한다 */
    bv = 0.0f;
    fx = PANE_W / 2.0f;
    fy = -start_gap;
    fh = 0.0f;
    last_stroke = -1000;
    last_side = -1;
    bitter_left = bitter_count;
    bitter_until = -1;
    splash_until = -1;
    for (int p = 0; p < STAGE_MAX_PLAYERS; p++) last_wrong[p] = -1000;
}

static int in_rapid(float y) { return rapid_len > 0.0f && y >= rapid_at && y < rapid_at + rapid_len; }

static void pass_turn(void)
{
    turn_no++;
    turn_start = tick_no;
}

void stage_input(int player, int key, double t)
{
    if (caught) return;
    int rower = turn_no % n_players;
    int side  = turn_no % 2;                         /* 0: 왼쪽 노(A), 1: 오른쪽 노(D) */

    if (key == SK_ACT) {
        if (player == rower) { pass_turn(); say(MSG_PASS); }
        return;
    }
    if (key == SK_SUB) {                             /* 쓴 열매: 누구든, 언제든. 이미 쓴맛이면 아끼게 무시한다 */
        if (bitter_left > 0 && tick_no > bitter_until) {
            bitter_left--;
            bitter_until = tick_no + bitter_ticks;
            say(MSG_BITTER);
            for (int p = 0; p < n_players; p++) fx_flash(p, C_PURPLE, 100);
        }
        return;
    }
    if (key == SK_UP) {                              /* 물 튀기기: 내 차례를 쓴다. 보트는 나아가지 않는다 */
        if (player != rower || tick_no < splash_ready_at) return;
        splash_side = side;
        splash_until = tick_no + splash_ticks;
        splash_ready_at = tick_no + splash_cooldown;
        last_stroke = tick_no;
        last_side = side;
        say(MSG_SPLASH);
        fx_all(fx_shockwave, (int)bx + (side == 0 ? -9 : 9), BOAT_ROW + 8);
        pass_turn();
        return;
    }
    if (key != SK_LEFT && key != SK_RIGHT) return;

    if (player != rower || (key == SK_RIGHT) != (side == 1)) {
        /* 차례가 아니거나 반대쪽 노다. 키를 누르고 있어도 한 번씩만 깎이게 간격을 둔다.
         * 안내는 누른 사람의 칸에만 띄운다(다른 사람의 안내를 지우지 않는다). */
        if (tick_no - last_wrong[player] >= wrong_gap_ticks) {
            bv *= wrong_keep;
            last_wrong[player] = tick_no;
            fx_shake(player, 1, 100);
        }
        return;
    }
    if (tick_no - last_stroke < cooldown_ticks) {    /* 엇박: 너무 빨리 저었다. 위와 같이 한 번씩만 깎는다 */
        if (tick_no - last_early >= wrong_gap_ticks) {
            bv *= early_keep;
            last_early = tick_no;
            say(MSG_EARLY);
        }
        return;
    }

    bv += stroke_power;
    bh += side == 0 ? stroke_turn : -stroke_turn;
    last_stroke = tick_no;
    last_side = side;
    say(MSG_GOOD);
    /* 노가 물을 친 자리에 잔상. 칸마다 보트는 같은 자리에 그려진다. */
    fx_all(fx_trail, (int)bx + (side == 0 ? -9 : 9), BOAT_ROW + 2);
    pass_turn();
}

/* 초파리: 보이는 보트는 물체 자극, 닿은 보트는 맛 자극, 튄 물은 다가옴 자극으로 준다.
 * 걷기, 방향, 먹기, 달아남 반응을 읽어 움직이고 물었는지 정한다. */
static void fly_think(void)
{
    float dx = bx - fx, dy = by - fy, d = sqrtf(dx * dx + dy * dy);
    float bearing = atan2f(dx, dy) - fh;             /* 보트가 초파리의 정면에서 오른쪽으로 벗어난 각도 */
    while (bearing > PI_F)  bearing -= 2.0f * PI_F;
    while (bearing < -PI_F) bearing += 2.0f * PI_F;

    /* 물체: 한쪽에만 준다. 진짜 뇌는 양쪽에 같이 주면 왼쪽 걷기만 켜진다(docs/brain-notes.md 6절).
     * 정면 근처에서는 주던 쪽에 계속 줘서 틱마다 뒤집히지 않게 한다. */
    /* 쪽을 바꾼 뒤 side_hold_ticks 동안은 다시 바꾸지 않는다. 진짜 뇌는 반응이 2~3틱 늦어서 자주 바꾸면 방향 반응이
     * 엇박이 되어 반대쪽으로 돈다(docs/guide.md의 11/27) */
    int want = bearing > center_band ? 1 : (bearing < -center_band ? 0 : fly_side);
    if (want != fly_side && tick_no - fly_side_at >= side_hold_ticks) { fly_side = want; fly_side_at = tick_no; }

    /* 맛: 닿아 있는 동안 단맛. 쓴 열매가 남아 있으면 쓴맛도 같이.
     * 닿아 있는 동안에는 물체 자극을 주지 않는다. 둘을 같이 오래 주면 진짜 뇌의 먹기 반응이 기준 언저리에 머물고,
     * 폭주한 판이 있었다(한 틱에 41,022번 발화) */
    int bitter_on = tick_no <= bitter_until;
    int was_touching = touching;
    touching = d <= contact_dist;
    touch_ticks = touching ? touch_ticks + 1 : 0;
    if (!touching) brain_stimulate(fly_side ? BR_OBJECT_R : BR_OBJECT_L, see_level);
    if (touching) {
        brain_stimulate(BR_SUGAR, sugar_level);
        if (bitter_on) brain_stimulate(BR_BITTER, bitter_level);
    }
    if (!touching) { if (fly_side) stim_r = see_level; else stim_l = see_level; }
    if (touching) { stim_sugar = sugar_level; stim_bitter = bitter_on ? bitter_level : 0.0f; }

    /* 다가옴: 튄 물. 가까울수록 세다. 노가 있는 쪽에서 온다(초파리는 보트 쪽을 보고 있어서 좌우가 같다) */
    int splashing = tick_no <= splash_until && d < splash_range;
    if (splashing) {
        float loom = splash_level * (0.5f + 0.5f * (1.0f - d / splash_range));
        brain_stimulate(splash_side == 0 ? BR_LOOM_L : BR_LOOM_R, loom);
        if (splash_side == 0) stim_l = loom > stim_l ? loom : stim_l;
        else                  stim_r = loom > stim_r ? loom : stim_r;
    }

    /* 반응 읽기 */
    feed_now = brain_rate(BR_FEED);
    if (!was_touching) touch_feed = 0.0f;
    if (touching && !was_touching) touch_at = tick_no;
    if (touching && feed_now > touch_feed) touch_feed = feed_now;
    /* 빠져나옴: 닿아 있다가 물리지 않고 떨어졌다. 뱉거나 놀라 튀어서 떨어진 것은 이미 안내가 있어서 뺀다.
     * 붙었다 떨어졌다를 되풀이할 때 틱마다 번쩍이지 않게 번쩍임은 PHEW_GAP 틱에 한 번만 한다 */
    if (was_touching && !touching && tick_no - fly_spit_at > 1 && tick_no - fly_hop_at > 1) {
        phew_at = tick_no;
        phew_feed = touch_feed;
        if (tick_no - phew_flash_at >= PHEW_GAP) {
            phew_flash_at = tick_no;
            for (int p = 0; p < n_players; p++) fx_flash(p, C_GOOD, 100);
        }
    }
    if (touching && feed_now >= feed_level) {        /* 물었다 */
        caught = 1;
        caught_tick = tick_no;
        react_at = tick_no;
        bv = 0.0f;
        for (int p = 0; p < n_players; p++) {
            fx_shake(p, 3, 300);
            fx_flash(p, C_BAD, 100);
        }
        fx_shockwave(PANE_FLY, (int)fx, FLY_ROW);
        return;
    }
    if (!touching && feed_now >= feed_level) block_at = tick_no;   /* 먹으려 했는데 보트가 이미 떨어졌다 */
    if (touching && bitter_on && touch_ticks >= spit_ticks) {
        /* 쓴맛이라 먹기 반응이 기준에 못 미쳤다. 뱉고 물러난다(물러나는 거리는 게임 규칙이다) */
        fx -= sinf(fh) * bitter_push;
        fy -= cosf(fh) * bitter_push;
        fly_spit_at = tick_no;
        touch_ticks = 0;
        say(MSG_SPIT);
        fx_flash(PANE_FLY, C_PURPLE, 100);
        return;
    }
    if (brain_fired(BR_ESCAPE)) {
        if (tick_no - fly_hop_at >= hop_cooldown) {
            /* 놀랐다. 뒤로 튀고, 뇌가 돌려는 쪽으로 꺾인다 */
            float turn = brain_rate(BR_TURN_R) - brain_rate(BR_TURN_L);
            fx -= sinf(fh) * escape_push;
            fy -= cosf(fh) * escape_push;
            if (turn > 0.1f)       fh += escape_turn;
            else if (turn < -0.1f) fh -= escape_turn;
            fly_hop_at = tick_no;
            react_at = tick_no;
            say(MSG_STARTLE);
            fx_shockwave(PANE_FLY, (int)fx, FLY_ROW);
            return;
        }
        block_at = tick_no;                          /* 놀랐지만 방금 튀어서 아직 다시 튀지 못한다 */
    }

    int was_moving = fly_moving;
    float wl = brain_rate(BR_WALK_L), wr = brain_rate(BR_WALK_R);
    float walk = wl > wr ? wl : wr;
    float turn = (brain_rate(BR_STEER_R) - brain_rate(BR_STEER_L)) + (wr - wl) * pursue_turn;
    fh += fly_turn * clampf(turn, -1.0f, 1.0f);
    float step = fly_speed * walk;
    /* 보트를 뚫고 지나가지 않는다. 닿는 거리 바로 안쪽에서 멈춘다 */
    float room = d - (contact_dist - 1.5f);
    if (room < 0.0f) room = 0.0f;
    if (step > room) step = room;
    fx += sinf(fh) * step;
    fy += cosf(fh) * step;
    fly_moving = walk > 0.05f;
    /* 걷기 시작. 보트에 붙어 걷지 못하는 것은 "?"로 띄우지 않는다. 닿아 있는 동안 늘 그래서
     * 보트가 빠져나갔을 때의 "?"(먹기 반응은 넘었는데 놓침)와 구별되지 않는다 */
    if (fly_moving && !was_moving && step > 0.0f) react_at = tick_no;
}

void stage_tick(double dt)
{
    stim_l = stim_r = stim_sugar = stim_bitter = 0.0f;    /* 자극은 그 틱에만 있다. fly_think가 다시 적는다 */
    if (!caught) {
        int rapid = in_rapid(by);
        if (rapid && !was_in_rapid) {
            say(MSG_RAPID);
            for (int p = 0; p < n_players; p++) fx_flash(p, C_CYAN, 100);
        }
        was_in_rapid = rapid;
        if (tick_no - turn_start >= (rapid ? rapid_limit_ticks : turn_limit_ticks)) { pass_turn(); say(MSG_TIMEOUT); }

        bv *= drag_keep;
        bx += sinf(bh) * bv;
        by += cosf(bh) * bv;
        if (rapid) {                                 /* 물살: 앞으로 흘러가고 한쪽 강둑으로 밀린다 */
            by += rapid_flow;
            bx += rapid_drift * (float)rapid_dir;
        }

        float lo = BANK_W + 5.0f, hi = PANE_W - BANK_W - 5.0f;
        if (bx < lo || bx > hi) {
            bx = clampf(bx, lo, hi);
            bv *= bank_keep;
            if (bv > 0.1f) say(MSG_BANK);
        }
        for (int i = 0; i < n_rocks; i++) {
            float dx = bx - rock_x[i], dy = by - rock_y[i];
            float r = rock_radius + 4.0f;
            if (dx * dx + dy * dy >= r * r) continue;
            /* 정면으로 박으면 그 자리에 걸려 버린다. 한쪽으로 살짝 밀어서 바위를 타고 돌아가게 한다. */
            if (dx > -1.0f && dx < 1.0f) dx = (i % 2) ? 1.0f : -1.0f;
            float d = sqrtf(dx * dx + dy * dy);
            bx = rock_x[i] + dx / d * r;             /* 바위 밖으로 밀어낸다 */
            by = rock_y[i] + dy / d * r;
            if (hit_rock != i) {                     /* 바위 하나에 한 번만 느려진다 */
                hit_rock = i;
                bv *= rock_keep;
                say(MSG_ROCK);
                for (int p = 0; p < n_players; p++) fx_shake(p, 2, 150);
            }
        }

        fly_think();
    }
    tick_no++;
}

/* ---- 그리기 ---------------------------------------------------------------
 * 강을 위에서 본 그림이다. 세계의 (wx, wy)는 칸의 (wx, row - (wy - cam))에 그린다. */
static int to_row(float wy, float cam, int row) { return row - (int)lroundf(wy - cam); }

static void draw_river(canvas_t *c, float cam, int row)
{
    ui_clear(c, C_BLUE);
    for (int y = 0; y < PANE_H; y++) {
        int wy = (int)floorf(cam) + (row - y);
        int m = ((wy % 12) + 12) % 12;
        if (m != 0) continue;
        int k = ((wy / 12) % 5 + 5) % 5;             /* 물결. 보트가 가면 아래로 흘러서 속도가 보인다 */
        for (int x = BANK_W + 4 + k * 5; x < PANE_W - BANK_W - 6; x += 26)
            ui_rect(c, x, y, 6, 1, C_CYAN);
    }
    /* 급류: 흰 물줄기가 밀리는 쪽으로 비스듬히 흐른다. 틱마다 움직여서 물살이 보인다 */
    for (int y = 0; y < PANE_H; y++) {
        int wy = (int)floorf(cam) + (row - y);
        if (!in_rapid((float)wy)) continue;
        int m = (((wy + tick_no) % 6) + 6) % 6;
        if (m > 1) continue;
        int shift = ((wy * rapid_dir) % 14 + 14) % 14;
        for (int x = BANK_W + 1 + shift; x < PANE_W - BANK_W - 4; x += 14)
            ui_rect(c, x, y, 3, 1, C_WHITE);
        if (wy == (int)rapid_at || wy == (int)(rapid_at + rapid_len) - 1)
            ui_rect(c, BANK_W, y, PANE_W - 2 * BANK_W, 1, C_WHITE);
    }
    ui_rect(c, 0, 0, BANK_W, PANE_H, C_GREEN);
    ui_rect(c, PANE_W - BANK_W, 0, BANK_W, PANE_H, C_GREEN);
    for (int i = 0; i < n_rocks; i++) {
        int y = to_row(rock_y[i], cam, row), r = (int)rock_radius;
        if (y < -r || y > PANE_H + r) continue;
        ui_rect(c, (int)rock_x[i] - r, y - r + 1, 2 * r, 2 * r - 2, C_GRAY2);
        ui_rect(c, (int)rock_x[i] - r + 1, y - r, 2 * r - 2, 2 * r, C_GRAY2);
        ui_rect(c, (int)rock_x[i] - r + 2, y - r + 1, 3, 2, C_GRAY3);
    }
}

/* 빠져나옴 표시를 len 틱 동안 띄운다. 그 사이에 다시 닿으면 끝낸다(뒤이어 뱉거나 놀라 튄 안내와 겹치지 않게) */
static int phew_shown(int len)
{
    return !caught && !touching && touch_at < phew_at && tick_no - phew_at < len;
}

/* 젓기 준비: 앞 노질에서 cooldown_ticks가 지났으면 노랗게 깜빡이고, 아직이면(지금 저으면 엇박) 회색이다 */
static color_t ready_color(void)
{
    if (tick_no - last_stroke < cooldown_ticks) return C_GRAY3;
    return (tick_no / 2) % 2 == 0 ? C_YELLOW : C_WHITE;
}

static void draw_boat(canvas_t *c, int cx, int cy)
{
    float fxv = sinf(bh), fyv = -cosf(bh);           /* 앞쪽(화면 좌표) */
    float px = cosf(bh), py = sinf(bh);              /* 오른쪽 */
    int side = turn_no % 2;
    int rowing = tick_no - last_stroke < 4;

    for (int s = 0; s < 2; s++) {                    /* 노: 0 왼쪽, 1 오른쪽 */
        float sg = s == 0 ? -1.0f : 1.0f;
        float back = (rowing && last_side == s) ? 5.0f : 0.0f;    /* 방금 저은 노는 뒤로 젖힌다 */
        color_t col = (!caught && side == s) ? ready_color() : C_GRAY3;   /* 차례인 노는 저을 수 있을 때 깜빡인다 */
        ui_line(c, cx + (int)lroundf(px * sg * 3), cy + (int)lroundf(py * sg * 3),
                cx + (int)lroundf(px * sg * 10 - fxv * back), cy + (int)lroundf(py * sg * 10 - fyv * back), col);
    }
    for (int o = -2; o <= 2; o++) {                  /* 선체 */
        float len = o == -2 || o == 2 ? 5.0f : 7.0f;
        int ox = cx + (int)lroundf(px * o), oy = cy + (int)lroundf(py * o);
        ui_line(c, ox - (int)lroundf(fxv * len), oy - (int)lroundf(fyv * len),
                ox + (int)lroundf(fxv * len), oy + (int)lroundf(fyv * len), tick_no <= bitter_until ? C_PURPLE : C_BROWN);
    }
    if (tick_no <= splash_until) {                   /* 튀는 물: 노 뒤쪽으로 퍼진다 */
        float sg = splash_side == 0 ? -1.0f : 1.0f;
        int k = splash_ticks - (splash_until - tick_no);
        for (int j = -1; j <= 1; j++)
            ui_rect(c, cx + (int)lroundf(px * sg * (7 + 2 * j) - fxv * (6 + 3 * k)), cy + (int)lroundf(py * sg * (7 + 2 * j) - fyv * (6 + 3 * k)), 2, 2, C_WHITE);
    }
    ui_rect(c, cx + (int)lroundf(fxv * 7) - 1, cy + (int)lroundf(fyv * 7) - 1, 2, 2, C_ORANGE);   /* 뱃머리 */
    ui_rect(c, cx + (int)lroundf(-px * 1.5f) - 1, cy + (int)lroundf(-py * 1.5f) - 1, 2, 2, C_WHITE);   /* 탄 사람들 */
    ui_rect(c, cx + (int)lroundf(px * 1.5f) - 1, cy + (int)lroundf(py * 1.5f) - 1, 2, 2, C_PINK);
}

static void draw_fly(canvas_t *c, int cx, int cy)
{
    sprite_id_t id = caught ? SPR_FLY_EAT
                   : (tick_no - fly_spit_at < 8 ? SPR_FLY_HIT
                   : (tick_no - fly_hop_at < 6 ? SPR_FLY_JUMP
                   : (touching ? SPR_FLY_EAT : (fly_moving ? SPR_FLY_JUMP : SPR_FLY_IDLE))));
    ui_sprite(c, id, cx - ui_sprite_w(id) / 2, cy - ui_sprite_h(id) / 2, tick_no / 3);
}

/* 뇌 표시(초파리 칸): 이번 틱에 준 자극과 그 반응.
 * 물체, 다가옴 자극은 그 쪽 옆의 세로 막대(높이 = 세기 x 12), 맛 자극은 머리 위의 가로 막대(단맛 위에 쓴맛).
 * 반응이 행동이 된 틱부터 6틱 동안 머리 위에 "!", 반응은 켜졌는데 막혔으면 "?" */
static void draw_brain(canvas_t *c, int cx, int cy)
{
    int hw = ui_sprite_w(SPR_FLY_IDLE) / 2, hh = ui_sprite_h(SPR_FLY_IDLE) / 2;
    int foot = cy - hh + ui_sprite_h(SPR_FLY_IDLE);  /* 초파리 그림의 아래 끝 바로 밑 */
    if (stim_l > 0.0f) {
        int h = (int)lroundf(stim_l * 12.0f);
        if (h < 1) h = 1;
        ui_rect(c, cx - hw - 4, foot - h, 2, h, C_WARN);
    }
    if (stim_r > 0.0f) {
        int h = (int)lroundf(stim_r * 12.0f);
        if (h < 1) h = 1;
        ui_rect(c, cx + hw + 2, foot - h, 2, h, C_WARN);
    }
    if (stim_sugar > 0.0f)  ui_bar(c, cx - hw, cy - hh - 5, 2 * hw, stim_sugar, C_WARN);
    if (stim_bitter > 0.0f) ui_bar(c, cx - hw, cy - hh - 10, 2 * hw, stim_bitter, C_PURPLE);

    const char *mark = tick_no - react_at <= 6 ? "!" : (tick_no - block_at <= 6 ? "?" : NULL);
    if (mark)
        ui_text(c, cx - ui_text_w(mark) / 2, cy - hh - 19, mark, mark[0] == '!' ? C_WARN : C_GRAY3);
}

void stage_draw(int player, canvas_t *fly_canvas, canvas_t *me)
{
    char buf[32];

    /* 초파리 칸: 초파리를 따라가며 본다. 모두에게 같은 그림이다. */
    draw_river(fly_canvas, fy, FLY_ROW);
    draw_boat(fly_canvas, (int)bx, to_row(by, fy, FLY_ROW));
    draw_fly(fly_canvas, (int)fx, FLY_ROW);
    draw_brain(fly_canvas, (int)fx, FLY_ROW);
    ui_rect(fly_canvas, 0, 0, PANE_W, HUD_H, C_BLACK);
    /* 위 띠: 닿기 전에는 거리, 닿으면 뇌의 먹기 반응. 세로줄(기준)을 넘으면 문 것이다.
     * 물리지 않고 빠져나온 뒤 잠깐은 닿아 있는 동안 가장 컸던 먹기 반응을 남겨서 기준에 못 미친 것을 보인다 */
    int phew = phew_shown(PHEW_GAP);
    snprintf(buf, sizeof buf, "먹기 %d%%", (int)lroundf(clampf(phew_feed, 0.0f, 1.0f) * 100.0f));
    if (caught)        ui_text(fly_canvas, 2, 1, "먹이 잡음!", C_GOOD);
    else if (touching) ui_text(fly_canvas, 2, 1, tick_no <= bitter_until ? "쓰다!" : "먹을까?", tick_no <= bitter_until ? C_PURPLE : C_WARN);
    else if (phew)     ui_text(fly_canvas, 2, 1, buf, C_GOOD);
    else               ui_text(fly_canvas, 2, 1, "먹이 추적", C_TEXT);
    if (touching || caught || phew) {
        ui_bar(fly_canvas, 2, HUD_H - 4, PANE_W - 4, clampf(phew ? phew_feed : feed_now, 0.0f, 1.0f), C_ORANGE);
        ui_rect(fly_canvas, 2 + (int)((PANE_W - 4) * feed_level), HUD_H - 5, 1, 5, C_WHITE);
    } else {
        ui_bar(fly_canvas, 2, HUD_H - 4, PANE_W - 4, clampf(1.0f - gap_now() / gap_far, 0.0f, 1.0f), C_RED);
    }

    /* 내 칸: 보트를 따라가며 본다. */
    draw_river(me, by, BOAT_ROW);
    int fr = to_row(fy, by, BOAT_ROW);
    if (fr < PANE_H - 8) {
        draw_fly(me, (int)fx, fr);
    } else {                                         /* 초파리가 화면 아래로 벗어났다. 거리만 적는다 */
        snprintf(buf, sizeof buf, "V %d", (int)gap_now());
        ui_text(me, (int)clampf(fx - 12.0f, BANK_W, PANE_W - BANK_W - 30), PANE_H - 9, buf, C_RED);
    }
    draw_boat(me, (int)bx, BOAT_ROW);

    ui_rect(me, 0, 0, PANE_W, HUD_H, C_BLACK);
    if (caught) {
        ui_text(me, 2, 1, "잡혔다!", C_BAD);
    } else {
        int rower = turn_no % n_players, side = turn_no % 2;
        if (rower == player) {                       /* A/D 글자는 저을 수 있을 때 깜빡이고, 아직이면 회색이다 */
            ui_text(me, 2, 1, "내 차례", C_YELLOW);
            ui_text(me, 2 + ui_text_w("내 차례") + 6, 3, side == 0 ? "A" : "D", ready_color());
        } else if (n_players > 1 && (turn_no + 1) % n_players == player) {
            ui_text(me, 2, 1, "다음!", C_TEXT);      /* 다음 차례다. 미리 손을 준비한다 */
        } else {
            ui_text(me, 2, 1, "기다려요", C_GRAY3);
        }
        float left = 1.0f - (float)(tick_no - turn_start) / (float)(in_rapid(by) ? rapid_limit_ticks : turn_limit_ticks);
        ui_bar(me, 2, HUD_H - 4, PANE_W - 4, clampf(left, 0.0f, 1.0f), rower == player ? C_YELLOW : C_GRAY2);
    }

    if (!caught && in_rapid(by)) {                   /* 급류: 밀리는 쪽의 강둑을 깜빡인다 */
        if ((tick_no / 3) % 2 == 0)
            ui_rect(me, rapid_dir > 0 ? PANE_W - BANK_W : 0, HUD_H, BANK_W, PANE_H - HUD_H, C_WARN);
    }
    for (int k = 0; k < bitter_left; k++)            /* 남은 쓴 열매 */
        ui_rect(me, PANE_W - BANK_W - 6 - k * 6, PANE_H - 7, 4, 4, C_PURPLE);
    if (!caught && touching && (tick_no / 2) % 2 == 0)
        ui_text(me, (PANE_W - ui_text_w("붙었다!")) / 2, PANE_H - 26, "붙었다!", C_BAD);
    /* 빠져나옴은 "붙었다!" 자리에 띄운다. 위쪽 안내는 다음 노질의 "좋아요!"가 곧 덮어서 따로 둔다 */
    if (phew_shown(14))
        ui_text(me, (PANE_W - ui_text_w("휴! 빠져나옴")) / 2, PANE_H - 26, "휴! 빠져나옴", C_GOOD);

    if (!caught && tick_no < msg_until) {
        static const char *const text[] = { "", "좋아요!", "엇박!", "넘김", "시간 초과", "바위!", "강둑!",
                                            "물 튀김!", "쓴 열매!", "퉤! 물러남", "놀랐다!", "급류다!" };
        color_t col = (msg == MSG_GOOD || msg == MSG_SPIT || msg == MSG_STARTLE) ? C_GOOD
                    : (msg == MSG_PASS || msg == MSG_SPLASH ? C_TEXT : (msg == MSG_BITTER ? C_PURPLE : (msg == MSG_RAPID ? C_CYAN : C_WARN)));
        ui_text(me, (PANE_W - ui_text_w(text[msg])) / 2, HUD_H + 2, text[msg], col);
    }
    if (!caught && tick_no - last_wrong[player] < 14)    /* 내가 차례가 아닌데 눌렀다. 내 칸에만 띄운다 */
        ui_text(me, (PANE_W - ui_text_w("차례 아님!")) / 2, HUD_H + 16, "차례 아님!", C_WARN);
}

void stage_result(stage_result_t *out)
{
    /* 협동이라 모두 같은 점수다. 잡히면 버틴 시간만큼(99 미만)이고 초파리는 100이다.
     * 끝까지 달아나면 초파리와 벌린 거리 g(gap_far에 대한 비율)만큼 팀은 safe_base~safe_base+safe_far,
     * 초파리는 fly_miss_max~0이다. fly_miss_max < safe_base라서 달아나면 늘 이기고 잡히면 늘 진다.
     * 판 도중(아래 띠에 보이는 값)에는 팀은 지난 시간만큼, 초파리는 얼마나 가까이 왔는지(99 이하)다. */
    float total = (float)(n_ticks > 0 ? n_ticks : 1);
    float g = clampf(gap_now() / gap_far, 0.0f, 1.0f);
    float team, fly;
    if (caught) {
        team = 99.0f * (float)caught_tick / total;
        fly  = 100.0f;
    } else if (tick_no >= n_ticks) {
        team = safe_base + safe_far * g;
        fly  = fly_miss_max * (1.0f - g);
    } else {
        team = 100.0f * (float)tick_no / total;
        fly  = 99.0f * (1.0f - g);
    }
    for (int p = 0; p < n_players; p++)
        out->score[p] = clampf(team, 0.0f, 100.0f);
    out->fly_score = fly;
}
