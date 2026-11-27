/* 초파리 추격: 3차선 통로를 달려 거대 초파리에게서 달아난다 (보스전 2페이즈)
 *
 * 참가자는 저절로 앞으로 달린다. A, D로 차선을 옮기고 W(또는 스페이스)로 뛰고 S로 미끄러진다.
 * 장애물은 넷이다. 기둥(차선을 옮겨 피한다), 가로대(뛰어넘는다), 들보(밑으로 미끄러진다), 끈끈이(밟으면 느려진다).
 * 부딪히면 잠깐 느려지고 그만큼 초파리가 다가온다. 초파리는 장애물에 걸리지 않는다.
 *
 * 초파리는 한 마리이고 모두를 쫓는다. 뇌가 정하는 것은 둘이다.
 *  - 쫓기: 앞에 있는 가장 가까운 참가자를 물체 자극(BR_OBJECT_L/R)으로 준다. 걷기 반응(BR_WALK)이 빠르기이고
 *          방향 반응(BR_STEER)과 걷기의 좌우 차이로 그 참가자의 차선 쪽으로 옮겨 간다. 보스전과 같은 집단이다.
 *  - 물기: 초파리가 참가자에게 닿으면 단맛 자극(BR_SUGAR)을 준다. 먹기 반응(BR_FEED)이 feed_level 이상이면
 *          물린 것이고 그 참가자는 바로 탈락한다. 반응은 몇 틱 늦게 오므로 닿은 직후에 벌어지면 살 수 있다.
 *
 * 화면: 내 칸은 내 캐릭터의 뒤에서 본 통로다. 초파리 칸은 초파리의 눈(낱눈의 격자)에 비친 참가자들이다.
 *
 * 다른 스테이지와 다른 점: 초파리 한 마리가 모두를 쫓으므로 한 사람의 결과가 다른 사람의 입력에 따라 달라진다
 * (include/stage.h의 약속을 일부러 지키지 않는다). config.ini의 coop = 1로 tools/bot.sh의 그 검사를 건너뛴다. */
#include "stage.h"
#include "brain.h"

#include <math.h>
#include <stdio.h>

#define MAX_OBS   48
#define HUD_H     17
#define CX        (PANE_W / 2)
#define HOR       40                 /* 지평선의 줄 */
#define FOCAL     58.0f              /* 거리 1에서 길이 1이 차지하는 픽셀 수 */
#define CAM_H     1.7f               /* 눈높이 */
#define CAM_BACK  3.2f               /* 카메라는 내 캐릭터의 이만큼 뒤에 있다 */
#define HALF      1.6f               /* 통로의 반폭 */
#define CEIL      3.2f               /* 천장 높이 */
#define VIEW_FAR  44.0f

enum { OB_PILLAR, OB_BAR, OB_BEAM, OB_GOO };
enum { MSG_NONE, MSG_HIT, MSG_GOO, MSG_TOUCH };

typedef struct {
    float z, x;                  /* 달려온 거리, 옆 자리(-1 왼쪽 차선, 0 가운데, 1 오른쪽) */
    int   lane, from_lane, lane_at;      /* 가려는 차선, 떠난 차선, 옮기기 시작한 틱 */
    int   jump_at, slide_at;     /* 뛴 틱, 미끄러지기 시작한 틱 */
    int   stumble_until, goo_until;
    int   next_ob;               /* 아직 지나지 않은 첫 장애물 */
    int   alive, out_tick;
    int   msg, msg_until;
} runner_t;

static runner_t pl[STAGE_MAX_PLAYERS];
static struct { float z; int kind, lanes; } ob[MAX_OBS];     /* lanes: 막힌 차선(비트 0이 왼쪽) */
static int   n_ob;
static float fz, fx;                 /* 초파리가 온 거리, 옆 자리 */
static int   fly_side, fly_side_at = -1000, fly_target, fly_touch, fly_eat_until, fly_bites;
static float feed_now, walk_now;
static int   n_players, n_ticks, tick_no;

static float run_speed, jump_height, stumble_keep, goo_keep, start_gap, gap_far;
static float fly_speed, fly_strafe, pursue_turn, see_level, center_band, contact_z, contact_x, sugar_level, feed_level;
static int   lane_ticks, jump_ticks, slide_ticks, stumble_ticks, goo_ticks, eat_ticks, side_hold_ticks;
static float hint_dist;              /* 장애물이 이 거리 안으로 오면 무엇을 할지 화면에 적는다. 0이면 적지 않는다 */

static const stage_info_t info = {
    .title    = "초파리 추격",
    .rule     = "장애물을 피해 달려요\n초파리에게 물리면 탈락",
    .controls = "A D 차선 W 점프 S 슬라이드",
};

const stage_info_t *stage_info(void) { return &info; }

static float clampf(float v, float lo, float hi) { return v < lo ? lo : (v > hi ? hi : v); }

static const color_t BODY[3] = { C_GREEN, C_CYAN, C_ORANGE };

void stage_start(const stage_ctx_t *ctx)
{
    n_players = ctx->n_players;
    n_ticks   = ctx->n_ticks;

    run_speed     = cfg_get("run_speed", 0.5f);
    lane_ticks    = (int)cfg_get("lane_ticks", 3.0f);
    jump_ticks    = (int)cfg_get("jump_ticks", 10.0f);
    jump_height   = cfg_get("jump_height", 0.9f);
    slide_ticks   = (int)cfg_get("slide_ticks", 10.0f);
    stumble_ticks = (int)cfg_get("stumble_ticks", 12.0f);
    stumble_keep  = cfg_get("stumble_keep", 0.4f);
    goo_ticks     = (int)cfg_get("goo_ticks", 12.0f);
    goo_keep      = cfg_get("goo_keep", 0.6f);
    start_gap     = cfg_get("start_gap", 9.0f);
    gap_far       = cfg_get("gap_far", 20.0f);
    fly_speed     = cfg_get("fly_speed", 0.47f);
    fly_strafe    = cfg_get("fly_strafe", 0.07f);
    pursue_turn   = cfg_get("pursue_turn", 0.5f);
    see_level     = cfg_get("see_level", 1.0f);
    center_band   = cfg_get("center_band", 0.2f);
    contact_z     = cfg_get("contact_z", 0.9f);
    contact_x     = cfg_get("contact_x", 0.6f);
    sugar_level   = cfg_get("sugar_level", 1.0f);
    feed_level    = cfg_get("feed_level", 0.5f);
    eat_ticks     = (int)cfg_get("eat_ticks", 24.0f);
    side_hold_ticks = (int)cfg_get("side_hold_ticks", 12.0f);
    hint_dist     = cfg_get("hint_dist", 8.0f);
    float first   = cfg_get("first_obstacle", 14.0f);
    float gap_min = cfg_get("gap_min", 8.0f), gap_max = cfg_get("gap_max", 13.0f);
    if (lane_ticks < 1) lane_ticks = 1;
    if (jump_ticks < 2) jump_ticks = 2;
    if (slide_ticks < 2) slide_ticks = 2;
    if (gap_far < 1.0f) gap_far = 1.0f;
    if (gap_min < 3.0f) gap_min = 3.0f;
    if (gap_max < gap_min) gap_max = gap_min;

    /* 문제(장애물)는 씨앗에서만 만든다. */
    stage_rng_t rng;
    stage_rng_seed(&rng, ctx->seed);
    float z = first;
    for (n_ob = 0; n_ob < MAX_OBS; n_ob++) {
        int kind = stage_rng_range(&rng, 0, 9);
        ob[n_ob].z = z;
        if (kind < 4) {                              /* 기둥: 한 차선이나 두 차선을 막는다. 셋을 다 막지는 않는다 */
            ob[n_ob].kind = OB_PILLAR;
            int open = stage_rng_range(&rng, 0, 2);
            ob[n_ob].lanes = stage_rng_range(&rng, 0, 1) ? (7 & ~(1 << open)) : (1 << stage_rng_range(&rng, 0, 2));
        } else if (kind < 7) {                       /* 가로대: 통로를 가로지른다. 뛰어넘는다 */
            ob[n_ob].kind = OB_BAR;
            ob[n_ob].lanes = 7;
        } else if (kind < 9) {                       /* 들보: 통로를 가로지른다. 밑으로 미끄러진다 */
            ob[n_ob].kind = OB_BEAM;
            ob[n_ob].lanes = 7;
        } else {                                     /* 끈끈이: 한 차선 */
            ob[n_ob].kind = OB_GOO;
            ob[n_ob].lanes = 1 << stage_rng_range(&rng, 0, 2);
        }
        z += gap_min + (gap_max - gap_min) * stage_rng_float(&rng);
    }
    fly_side = (int)(stage_rng_u32(&rng) & 1u);

    for (int p = 0; p < STAGE_MAX_PLAYERS; p++) {
        int lane = p < 3 ? (p == 0 ? 0 : (p == 1 ? -1 : 1)) : 0;     /* 0번은 가운데, 1번은 왼쪽, 2번은 오른쪽에서 출발 */
        pl[p] = (runner_t){ .x = (float)lane, .lane = lane, .from_lane = lane, .lane_at = -1000,
                            .jump_at = -1000, .slide_at = -1000, .stumble_until = -1, .goo_until = -1, .alive = 1 };
    }
    fz = -start_gap;
    fx = 0.0f;
    fly_target = -1;
}

static float jump_z(const runner_t *r)
{
    int t = tick_no - r->jump_at;
    if (t < 0 || t >= jump_ticks) return 0.0f;
    float u = (float)t / (float)(jump_ticks - 1);            /* 0 -> 1 */
    return jump_height * 4.0f * u * (1.0f - u);
}
static int sliding(const runner_t *r) { int t = tick_no - r->slide_at; return t >= 0 && t < slide_ticks; }
static int in_air(const runner_t *r)  { int t = tick_no - r->jump_at;  return t >= 0 && t < jump_ticks; }

static void say(runner_t *r, int m) { r->msg = m; r->msg_until = tick_no + 12; }

void stage_input(int player, int key, double t)
{
    runner_t *r = &pl[player];
    if (!r->alive) return;
    if ((key == SK_LEFT || key == SK_RIGHT) && tick_no - r->lane_at >= lane_ticks) {
        int to = r->lane + (key == SK_LEFT ? -1 : 1);
        if (to < -1 || to > 1) return;
        r->from_lane = r->lane;
        r->lane = to;
        r->lane_at = tick_no;
    } else if ((key == SK_UP || key == SK_ACT) && !in_air(r) && !sliding(r)) {
        r->jump_at = tick_no;
    } else if (key == SK_DOWN && !in_air(r) && !sliding(r)) {
        r->slide_at = tick_no;
    }
}

/* 참가자 하나를 한 틱 달리게 한다. */
static void run_one(runner_t *r, int pane)
{
    float v = run_speed;
    if (tick_no <= r->stumble_until) v *= stumble_keep;
    else if (tick_no <= r->goo_until) v *= goo_keep;
    float z0 = r->z;
    r->z += v;

    int t = tick_no - r->lane_at;                            /* 차선 옮기기: 고르게 미끄러진다 */
    float u = t >= lane_ticks ? 1.0f : (float)(t + 1) / (float)lane_ticks;
    r->x = (float)r->from_lane + ((float)r->lane - (float)r->from_lane) * u;

    /* 이번 틱에 지나간 장애물 */
    while (r->next_ob < n_ob && ob[r->next_ob].z <= r->z) {
        int k = r->next_ob++;
        if (ob[k].z < z0) continue;
        int lane = u >= 0.5f ? r->lane : r->from_lane;       /* 절반을 넘어갔으면 옮겨 간 차선에 있는 것이다 */
        if (!(ob[k].lanes & (1 << (lane + 1)))) continue;
        int hit = 0;
        if (ob[k].kind == OB_PILLAR) hit = 1;
        else if (ob[k].kind == OB_BAR)  hit = jump_z(r) < 0.4f;
        else if (ob[k].kind == OB_BEAM) hit = !sliding(r);
        else if (ob[k].kind == OB_GOO && !in_air(r)) {
            r->goo_until = tick_no + goo_ticks;
            say(r, MSG_GOO);
            fx_flash(pane, C_GREEN, 50);
        }
        if (hit) {
            r->stumble_until = tick_no + stumble_ticks;
            say(r, MSG_HIT);
            fx_shake(pane, 2, 150);
            fx_flash(pane, C_BAD, 50);
        }
    }
}

/* 초파리: 앞에 있는 가장 가까운 참가자를 물체로 보고 쫓는다. 닿으면 맛을 본다. */
static void fly_think(void)
{
    /* 쫓을 사람: 초파리보다 앞에 있는(닿을 만큼 겹친 사람도 넣는다) 가장 가까운 사람 */
    int best = -1;
    for (int p = 0; p < n_players; p++) {
        if (!pl[p].alive || pl[p].z < fz - contact_z) continue;
        if (best < 0 || pl[p].z < pl[best].z) best = p;
    }
    fly_target = best;
    fly_touch = 0;
    feed_now = brain_rate(BR_FEED);
    float wl = brain_rate(BR_WALK_L), wr = brain_rate(BR_WALK_R);
    walk_now = wl > wr ? wl : wr;
    float turn = (brain_rate(BR_STEER_R) - brain_rate(BR_STEER_L)) + (wr - wl) * pursue_turn;
    if (best < 0) return;
    runner_t *r = &pl[best];

    /* 물체: 그 사람이 있는 쪽 한쪽에만 준다(양쪽에 같이 주면 진짜 뇌는 왼쪽 걷기만 켜진다. docs/brain-notes.md 6절).
     * 쪽을 바꾼 뒤 side_hold_ticks 동안은 다시 바꾸지 않는다. 진짜 뇌는 반응이 2~3틱 늦어서, 3틱마다 쪽을 바꾸면
     * 방향 반응이 엇박이 되어 반대쪽으로 돈다(재 본 값: 3틱마다 -0.38, 12틱마다 +0.38, 안 바꾸면 +0.69) */
    float off = r->x - fx;
    int want = off > center_band ? 1 : (off < -center_band ? 0 : fly_side);
    if (want != fly_side && tick_no - fly_side_at >= side_hold_ticks) { fly_side = want; fly_side_at = tick_no; }

    float dz = r->z - fz;
    fly_touch = fabsf(dz) <= contact_z && fabsf(off) <= contact_x && jump_z(r) < 0.5f;
    /* 닿아 있는 동안에는 물체 자극을 주지 않고 맛만 본다. 둘을 같이 주면 진짜 뇌의 먹기 반응이 기준 언저리에 머문다 */
    if (!fly_touch) brain_stimulate(fly_side ? BR_OBJECT_R : BR_OBJECT_L, see_level);
    if (fly_touch) {
        brain_stimulate(BR_SUGAR, sugar_level);
        if (feed_now >= feed_level) {                /* 물었다. 그 사람은 탈락이고 초파리는 잠깐 먹는다 */
            r->alive = 0;
            r->out_tick = tick_no;
            fly_bites++;
            fly_eat_until = tick_no + eat_ticks;
            fx_shake(best, 3, 300);
            fx_flash(best, C_BAD, 150);
            fx_shockwave(PANE_FLY, PANE_W / 2, PANE_H / 2);
            return;
        }
        say(r, MSG_TOUCH);
    }
    if (tick_no <= fly_eat_until) return;

    fx = clampf(fx + fly_strafe * clampf(turn, -1.0f, 1.0f), -1.2f, 1.2f);
    float step = fly_speed * walk_now;
    /* 쫓는 사람을 뚫고 지나가지 않는다 */
    if (fabsf(r->x - fx) <= contact_x) {
        float room = dz - contact_z * 0.5f;
        if (room < 0.0f) room = 0.0f;
        if (step > room) step = room;
    }
    fz += step;
}

void stage_tick(double dt)
{
    for (int p = 0; p < n_players; p++)
        if (pl[p].alive) run_one(&pl[p], p);
    fly_think();
    tick_no++;
}

/* ---- 그리기 ---------------------------------------------------------------
 * 카메라에서 앞으로 dz만큼 떨어진 (wx, wy)는 칸의 (CX + F(wx - camx)/dz, HOR + F(camh - wy)/dz)에 그린다. */
typedef struct { float x, z, h; } cam_t;

static int px_of(const cam_t *c, float wx, float dz) { return CX + (int)lroundf(FOCAL * (wx - c->x) / dz); }
static int py_of(const cam_t *c, float wy, float dz) { return HOR + (int)lroundf(FOCAL * (c->h - wy) / dz); }

static void box(canvas_t *cv, const cam_t *c, float x0, float x1, float y0, float y1, float dz, color_t col)
{
    if (dz < 0.5f) return;
    int ax = px_of(c, x0, dz), bx = px_of(c, x1, dz), ay = py_of(c, y1, dz), by = py_of(c, y0, dz);
    if (bx <= ax) bx = ax + 1;
    if (by <= ay) by = ay + 1;
    ui_rect(cv, ax, ay, bx - ax, by - ay, col);
}

static void draw_corridor(canvas_t *cv, const cam_t *c)
{
    ui_clear(cv, C_BROWN);                                   /* 벽 */
    for (int y = HOR + 1; y < PANE_H; y++) {                 /* 바닥: 줄마다 거리를 구해 띠를 칠한다 */
        float dz = FOCAL * c->h / (float)(y - HOR);
        int xl = px_of(c, -HALF, dz), xr = px_of(c, HALF, dz);
        int band = ((int)floorf((c->z + dz) / 2.0f)) & 1;
        ui_rect(cv, xl, y, xr - xl + 1, 1, band ? C_GRAY2 : C_GRAY1);
    }
    for (int y = HUD_H; y < HOR; y++) {                      /* 천장 */
        float dz = FOCAL * (CEIL - c->h) / (float)(HOR - y);
        int xl = px_of(c, -HALF, dz), xr = px_of(c, HALF, dz);
        ui_rect(cv, xl, y, xr - xl + 1, 1, C_BLACK);
    }
    box(cv, c, -HALF, HALF, 0.0f, CEIL, VIEW_FAR, C_BLACK);  /* 멀리는 어둡다 */
    /* 벽의 이음매: 달리면 뒤로 흘러서 빠르기가 보인다 */
    float first = ceilf((c->z + 1.2f) / 3.0f) * 3.0f;
    for (float wz = first; wz < c->z + VIEW_FAR; wz += 3.0f) {
        float dz = wz - c->z;
        for (int s = -1; s <= 1; s += 2) {
            int x = px_of(c, s * HALF, dz);
            ui_line(cv, x, py_of(c, CEIL, dz), x, py_of(c, 0.0f, dz), C_BLACK);
        }
    }
    for (int s = -1; s <= 1; s += 2)                         /* 차선 */
        ui_line(cv, px_of(c, s * 0.5f, 1.3f), py_of(c, 0.0f, 1.3f), px_of(c, s * 0.5f, VIEW_FAR), py_of(c, 0.0f, VIEW_FAR), C_GRAY3);
}

static void draw_obstacle(canvas_t *cv, const cam_t *c, int k)
{
    float dz = ob[k].z - c->z;
    if (dz < 0.6f || dz > VIEW_FAR) return;
    if (ob[k].kind == OB_BAR) {
        box(cv, c, -HALF, HALF, 0.0f, 0.42f, dz, C_RED);
        box(cv, c, -HALF, HALF, 0.3f, 0.42f, dz, C_YELLOW);
        return;
    }
    if (ob[k].kind == OB_BEAM) {
        box(cv, c, -HALF, HALF, 1.0f, 1.55f, dz, C_ORANGE);
        box(cv, c, -HALF, HALF, 1.0f, 1.12f, dz, C_YELLOW);
        return;
    }
    for (int lane = -1; lane <= 1; lane++) {
        if (!(ob[k].lanes & (1 << (lane + 1)))) continue;
        if (ob[k].kind == OB_PILLAR) {
            box(cv, c, lane - 0.45f, lane + 0.45f, 0.0f, 2.6f, dz, C_GRAY3);
            box(cv, c, lane + 0.25f, lane + 0.45f, 0.0f, 2.6f, dz, C_GRAY2);
        } else {                                             /* 끈끈이: 바닥에 깔린 얼룩 */
            int ya = py_of(c, 0.0f, dz + 0.9f), yb = py_of(c, 0.0f, dz > 1.0f ? dz - 0.4f : 0.6f);
            int xa = px_of(c, lane - 0.45f, dz), xb = px_of(c, lane + 0.45f, dz);
            ui_rect(cv, xa, ya, xb - xa + 1, yb - ya + 1, C_GREEN);
        }
    }
}

static void draw_runner(canvas_t *cv, const cam_t *c, const runner_t *r, int who)
{
    float dz = r->z - c->z;
    if (dz < 0.6f || dz > VIEW_FAR) return;
    float y0 = jump_z(r), x = r->x;
    color_t body = BODY[who % 3];
    if (tick_no <= r->stumble_until && (tick_no / 2) % 2) body = C_BAD;
    box(cv, c, x - 0.3f, x + 0.3f, -0.02f, 0.02f, dz, C_BLACK);                  /* 그림자 */
    if (sliding(r)) {                                                            /* 미끄러지는 중: 납작하다 */
        box(cv, c, x - 0.3f, x + 0.3f, 0.05f, 0.5f, dz, body);
        box(cv, c, x - 0.18f, x + 0.18f, 0.5f, 0.75f, dz, C_WHITE);
        return;
    }
    int step = (tick_no / 2) % 2;                                                /* 다리를 번갈아 든다 */
    box(cv, c, x - 0.22f, x - 0.05f, y0 + (step ? 0.12f : 0.0f), y0 + 0.45f, dz, C_GRAY1);
    box(cv, c, x + 0.05f, x + 0.22f, y0 + (step ? 0.0f : 0.12f), y0 + 0.45f, dz, C_GRAY1);
    box(cv, c, x - 0.3f, x + 0.3f, y0 + 0.45f, y0 + 1.05f, dz, body);
    box(cv, c, x - 0.22f, x + 0.22f, y0 + 1.08f, y0 + 1.45f, dz, C_WHITE);
}

/* 뒤에서 본 초파리(내 앞으로 지나간 경우에 보인다) */
static void draw_fly_back(canvas_t *cv, const cam_t *c)
{
    float dz = fz - c->z;
    if (dz < 0.6f || dz > VIEW_FAR) return;
    box(cv, c, fx - 0.95f, fx - 0.3f, 0.9f, 1.25f, dz, C_WING);
    box(cv, c, fx + 0.3f, fx + 0.95f, 0.9f, 1.25f, dz, C_WING);
    box(cv, c, fx - 0.5f, fx + 0.5f, 0.15f, 1.1f, dz, C_FLY_BODY);
    box(cv, c, fx - 0.5f, fx + 0.5f, 0.35f, 0.48f, dz, C_BLACK);
    box(cv, c, fx - 0.5f, fx + 0.5f, 0.62f, 0.75f, dz, C_BLACK);
}

/* 초파리의 눈: 낱눈의 격자. 낱눈마다 그 방향에 보이는 것의 색을 찍는다. */
static void draw_fly_eye(canvas_t *cv)
{
    cam_t c = { fx, fz + 0.3f, 0.8f };
    ui_clear(cv, C_BLACK);
    for (int row = 0; (HUD_H + 3 + row * 5) < PANE_H - 2; row++) {
        int y = HUD_H + 3 + row * 5;
        for (int col = 0; col < 19; col++) {
            int x = 1 + col * 5 + ((row & 1) ? 2 : 0);       /* 줄마다 반 칸씩 어긋난다 */
            if (x + 3 > PANE_W) continue;
            int sx = x + 1, sy = y + 1;
            color_t colr = sy > HOR ? C_GRAY1 : C_BLACK;     /* 바닥과 그 위 */
            if (sy > HOR) {
                float dz = FOCAL * c.h / (float)(sy - HOR);
                float wx = c.x + (float)(sx - CX) * dz / FOCAL;
                if (wx < -HALF || wx > HALF) colr = C_BROWN;
            } else {
                float dz = FOCAL * (CEIL - c.h) / (float)(HOR - sy + 1);
                float wx = c.x + (float)(sx - CX) * dz / FOCAL;
                if (wx < -HALF || wx > HALF) colr = C_BROWN;
            }
            for (int p = 0; p < n_players; p++) {            /* 참가자: 가까울수록 낱눈을 많이 덮는다 */
                const runner_t *r = &pl[p];
                float dz = r->z - c.z;
                if (!r->alive || dz < 0.25f || dz > VIEW_FAR) continue;
                float yc = jump_z(r) + (sliding(r) ? 0.35f : 0.75f);
                int cxp = px_of(&c, r->x, dz), cyp = py_of(&c, yc, dz);
                float rad = FOCAL * (sliding(r) ? 0.4f : 0.75f) / dz;
                float ddx = (float)(sx - cxp) / 0.55f, ddy = (float)(sy - cyp);     /* 사람은 세로로 길다 */
                if (ddx * ddx + ddy * ddy <= rad * rad) colr = (p == fly_target) ? BODY[p % 3] : C_GRAY3;
            }
            ui_rect(cv, x, y, 3, 3, colr);
        }
    }
    ui_rect(cv, 0, 0, PANE_W, HUD_H, C_BLACK);
    if (tick_no <= fly_eat_until)  ui_text(cv, 2, 1, "냠냠!", C_GOOD);
    else if (fly_touch)            ui_text(cv, 2, 1, "먹을까?", C_WARN);
    else                           ui_text(cv, 2, 1, "초파리의 눈", C_TEXT);
    if (fly_touch || tick_no <= fly_eat_until) {             /* 닿으면 뇌의 먹기 반응. 세로줄(기준)을 넘으면 문다 */
        ui_bar(cv, 2, HUD_H - 4, PANE_W - 4, clampf(feed_now, 0.0f, 1.0f), C_ORANGE);
        ui_rect(cv, 2 + (int)((PANE_W - 4) * feed_level), HUD_H - 5, 1, 5, C_WHITE);
    } else {                                                 /* 평소에는 걷기 반응 */
        ui_bar(cv, 2, HUD_H - 4, PANE_W - 4, clampf(walk_now, 0.0f, 1.0f), C_CYAN);
    }
}

/* 다가오는 장애물에 무엇을 해야 하는지. 내 차선에서 걸리는 것만 알려 준다. 없으면 NULL */
static const char *hint_for(const runner_t *r, color_t *col)
{
    if (hint_dist <= 0.0f || r->next_ob >= n_ob) return NULL;
    int k = r->next_ob;
    if (ob[k].z - r->z > hint_dist) return NULL;
    if (!(ob[k].lanes & (1 << (r->lane + 1)))) return NULL;          /* 내 차선은 비어 있다 */
    switch (ob[k].kind) {
    case OB_BAR:  *col = C_YELLOW; return in_air(r) ? NULL : "점프!";
    case OB_BEAM: *col = C_ORANGE; return sliding(r) ? NULL : "숙여!";
    case OB_GOO:  *col = C_GREEN;  return in_air(r) ? NULL : "끈끈이!";
    default: {                                                        /* 기둥: 가까운 빈 차선 쪽을 알려 준다 */
        *col = C_WHITE;
        int left_open  = r->lane > -1 && !(ob[k].lanes & (1 << r->lane));
        int right_open = r->lane < 1  && !(ob[k].lanes & (1 << (r->lane + 2)));
        if (left_open && right_open) return "옆으로!";
        if (left_open)  return "왼쪽으로!";
        if (right_open) return "오른쪽으로!";
        return r->lane > 0 ? "왼쪽 끝으로!" : "오른쪽 끝으로!";       /* 두 칸을 가야 한다 */
    }
    }
}

void stage_draw(int player, canvas_t *fly_canvas, canvas_t *me)
{
    char buf[24];
    const runner_t *self = &pl[player];
    /* 물린 사람은 살아 있는 사람을 따라가며 본다(관전). 아무도 없으면 물린 자리에 머문다 */
    int view = player;
    if (!self->alive)
        for (int p = 0; p < n_players; p++)
            if (pl[p].alive) { view = p; break; }
    const runner_t *r = &pl[view];
    cam_t c = { r->x * 0.6f, r->z - CAM_BACK, CAM_H };

    draw_fly_eye(fly_canvas);

    draw_corridor(me, &c);
    /* 먼 것부터 그린다. 이미 지나친 장애물은 그리지 않는다(카메라와 나 사이에 있어서 화면을 가린다).
     * 바로 뒤까지 온 초파리는 카메라 앞에 들어오므로 보인다. 그 위에 나를 그려서 내 자리가 가려지지 않게 한다 */
    for (int k = n_ob - 1; k >= 0; k--)
        if (ob[k].z > r->z + 0.2f) draw_obstacle(me, &c, k);
    for (int p = 0; p < n_players; p++)
        if (p != view && pl[p].alive && pl[p].z > r->z) draw_runner(me, &c, &pl[p], p);
    draw_fly_back(me, &c);
    if (r->alive) draw_runner(me, &c, r, view);
    for (int p = 0; p < n_players; p++)
        if (p != view && pl[p].alive && pl[p].z <= r->z) draw_runner(me, &c, &pl[p], p);

    /* 위 띠: 초파리와의 거리. 막대가 찰수록 가깝다 */
    float gap = r->z - fz;
    int danger = gap < gap_far * 0.25f;
    ui_rect(me, 0, 0, PANE_W, HUD_H, C_BLACK);
    if (!self->alive) {
        ui_text(me, 2, 1, view != player ? "물렸다 관전" : "물렸다!", C_BAD);
        if (view == player) ui_sprite(me, SPR_FLY_EAT, (PANE_W - ui_sprite_w(SPR_FLY_EAT)) / 2, 56, tick_no / 3);
    } else {
        ui_text(me, 2, 1, danger ? "뒤에 있다!" : "달려!", danger && (tick_no / 3) % 2 ? C_BAD : C_TEXT);
        snprintf(buf, sizeof buf, "%d", gap > 0.0f ? (int)gap : 0);
        ui_text(me, PANE_W - 2 - ui_text_w(buf), 3, buf, danger ? C_BAD : C_GRAY3);
    }
    if (r->alive)
        ui_bar(me, 2, HUD_H - 4, PANE_W - 4, clampf(1.0f - gap / gap_far, 0.0f, 1.0f), danger ? C_RED : C_ORANGE);
    if (self->alive) {
        /* 띠 바로 아래 한 줄: 방금 일어난 일(쿵, 끈적, 붙었다)이 먼저이고, 없으면 다가오는 장애물에 할 일 */
        color_t col = C_BAD;
        const char *line = NULL;
        if (tick_no < r->msg_until) {
            static const char *const text[] = { "", "쿵!", "끈적!", "붙었다!" };
            line = text[r->msg];
            if (r->msg == MSG_GOO) col = C_GREEN;
        } else {
            line = hint_for(r, &col);
        }
        if (line) {
            int w = ui_text_w(line);
            ui_rect(me, (PANE_W - w) / 2 - 2, HUD_H + 1, w + 4, 14, C_BLACK);
            ui_text(me, (PANE_W - w) / 2, HUD_H + 2, line, col);
        }
    }
}

void stage_result(stage_result_t *out)
{
    /* 끝까지 달아나면 100, 물리면 버틴 시간만큼(99 이하). 초파리는 한 명이라도 물었으면 100, 아니면 가장 가까이
     * 간 정도(99 이하). 그래서 살아남으면 이기고 물리면 진다. */
    float total = (float)(n_ticks > 0 ? n_ticks : 1);
    float nearest = gap_far;
    for (int p = 0; p < n_players; p++) {
        out->score[p] = pl[p].alive ? 100.0f * (float)tick_no / total : 99.0f * (float)pl[p].out_tick / total;
        if (out->score[p] > 100.0f) out->score[p] = 100.0f;
        if (pl[p].alive && pl[p].z - fz < nearest) nearest = pl[p].z - fz;
    }
    out->fly_score = fly_bites > 0 ? 100.0f : 99.0f * clampf(1.0f - nearest / gap_far, 0.0f, 1.0f);
}
