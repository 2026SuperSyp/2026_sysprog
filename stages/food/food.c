/* 음식 골라 먹기 (담당: 김고은)
 *
 * 위에서 음식이 세 줄(왼쪽, 가운데, 오른쪽) 가운데 한 줄로 떨어진다.
 * A, D(또는 ←, →)로 움직인다. 머리에 닿은 것은 저절로 먹는다.
 * 단 음식은 받아먹고, 쓴 것, 짠 것, 썩은 것, 먹을 수 없는 것(의자, 돌 등)은 피해야 한다.
 * 먹은 음식은 발밑에 탑으로 쌓이고, 나쁜 음식을 먹으면 하트가 깎이고 탑이 무너진다.
 * 하트를 다 잃으면 그 자리에서 멈추고 점수가 고정된다(판은 끝까지 간다).
 *
 * 초파리
 *  - 이동은 규칙이다: 다음에 떨어지는 음식의 줄로 간다. (진짜 뇌에는 음식을 찾아가는
 *    감각인 냄새가 연결되어 있지 않다. docs/guide.md "진짜 뇌")
 *  - 먹을지 말지는 뇌가 정한다: 음식이 몸에 닿아 있는 동안 그 음식의 맛을 다리로 느끼게
 *    (자극) 하고, 뇌의 먹기 반응(BR_FEED)이 기준을 넘으면 먹는다. 사람은 닿으면 바로 먹지만
 *    초파리는 다리로 맛을 보고 거부할 수 있다(실제 초파리도 다리에 맛 감각 뉴런이 있다).
 *  - 함정: 단짠 캐러멜은 단맛이 세다. 진짜 뇌는 단맛이 1.0이면 짠맛을 줘도 먹는다
 *    (docs/guide.md "가짜 뇌와 진짜 뇌의 차이"). 그래서 초파리는 단짠에 속는다. */
#include "stage.h"
#include "brain.h"

#include <stdio.h>

#define MAX_FOODS   64
#define MAX_LAYERS  64
#define HUD_H       12
#define N_LANES     3
#define LANE_W      30                 /* 줄 사이 거리 */
#define GROUND_Y    90                 /* 땅의 높이. 모두 같다 */
#define BODY_H      22                 /* 사람 그림의 키. 초파리는 이 높이에 떠서 난다 */
#define LAYER_H     4                  /* 탑 한 층의 높이 */
#define SPAWN_Y     (HUD_H + 16)        /* 음식의 아래쪽이 처음 나오는 높이(점수 줄과 겹치지 않게) */

/* 음식 종류. 맛의 세기는 뇌에 주는 자극이다(0.0~1.0). 그림은 assets/<sprite>.spr */
enum { F_DONUT, F_COOKIE, F_ICECREAM, F_MELON, F_CANDY, F_PUDDING, F_CAKE,   /* 단맛: 먹기 */
       F_COFFEE, F_PILL, F_BITTERMELON, F_BROCCOLI, F_GINSENG,                  /* 쓴맛: 피하기 */
       F_SALTSHAKER, F_SOYSAUCE, F_ANCHOVY, F_CHEESE, F_CANNEDHAM, F_STEAK,     /* 짠맛: 피하기 */
       F_CHAIR, F_ROCK, F_DUCK, F_DICE, F_BULB,                                 /* 못 먹는 것 */
       F_CAMPFIRE, F_CHEST, F_BONE, F_COIN,
       F_CARAMEL,                                                               /* 함정 */
       F_COUNT };
#define FIRST_GOOD F_DONUT
#define LAST_GOOD  F_CAKE
/* 나쁜 것은 쓴 것, 짠 것, 못 먹는 것 가운데 갈래를 먼저 고르고(같은 확률) 그 안에서 고른다 */
static const int BAD_RANGE[3][2] = { { F_COFFEE, F_GINSENG }, { F_SALTSHAKER, F_STEAK }, { F_CHAIR, F_COIN } };

typedef struct {
    const char *name;
    const char *sprite;
    float   sugar, bitter, salt;
    int     good;
    color_t layer;                     /* 탑에 쌓였을 때의 색 */
} food_kind_t;

/* 쓴 것과 짠 것은 "누가 봐도 그 맛"인 것만 둔다. 단맛은 섞지 않는다(단짠만 예외). */
static const food_kind_t KIND[F_COUNT] = {
    [F_DONUT]       = { "도넛",       "donut",       1.0f, 0.0f, 0.0f, 1, C_PINK   },
    [F_COOKIE]      = { "쿠키",       "cookie",      0.8f, 0.0f, 0.0f, 1, C_ORANGE },
    [F_ICECREAM]    = { "아이스크림", "icecream",    0.9f, 0.0f, 0.0f, 1, C_YELLOW },
    [F_MELON]       = { "수박",       "melon",       0.7f, 0.0f, 0.0f, 1, C_RED    },
    [F_CANDY]       = { "사탕",       "candy",       1.0f, 0.0f, 0.0f, 1, C_PURPLE },
    [F_PUDDING]     = { "푸딩",       "pudding",     1.0f, 0.0f, 0.0f, 1, C_YELLOW },
    [F_CAKE]        = { "케이크",     "cake",        1.0f, 0.0f, 0.0f, 1, C_PINK   },
    [F_COFFEE]      = { "커피",       "coffee",      0.0f, 1.0f, 0.0f, 0, C_BROWN  },
    [F_PILL]        = { "쓴약",       "pill",        0.0f, 1.0f, 0.0f, 0, C_WHITE  },
    [F_BITTERMELON] = { "여주",       "bittermelon", 0.0f, 0.9f, 0.0f, 0, C_GREEN  },
    [F_BROCCOLI]    = { "브로콜리",   "broccoli",    0.0f, 0.7f, 0.0f, 0, C_GREEN  },
    [F_GINSENG]     = { "홍삼",       "ginseng",     0.0f, 0.9f, 0.0f, 0, C_BROWN  },
    [F_SALTSHAKER]  = { "소금",       "saltshaker",  0.0f, 0.0f, 1.0f, 0, C_WHITE  },
    [F_SOYSAUCE]    = { "간장",       "soysauce",    0.0f, 0.0f, 0.9f, 0, C_BROWN  },
    [F_ANCHOVY]     = { "멸치",       "anchovy",     0.0f, 0.0f, 0.8f, 0, C_GRAY3  },
    [F_CHEESE]      = { "치즈",       "cheese",      0.0f, 0.0f, 0.7f, 0, C_YELLOW },
    [F_CANNEDHAM]   = { "통조림 햄",  "cannedham",   0.0f, 0.0f, 0.9f, 0, C_PINK   },
    [F_STEAK]       = { "스테이크",   "steak",       0.0f, 0.0f, 0.7f, 0, C_RED    },
    [F_CHAIR]       = { "의자",       "chair",       0.0f, 0.0f, 0.0f, 0, C_BROWN  },   /* 맛이 없다. 먹기 뉴런이 켜질 까닭이 없다 */
    [F_ROCK]        = { "돌",         "rock",        0.0f, 0.0f, 0.0f, 0, C_GRAY2  },
    [F_DUCK]        = { "고무 오리",  "duck",        0.0f, 0.0f, 0.0f, 0, C_YELLOW },
    [F_DICE]        = { "주사위",     "dice",        0.0f, 0.0f, 0.0f, 0, C_WHITE  },
    [F_BULB]        = { "전구",       "bulb",        0.0f, 0.0f, 0.0f, 0, C_YELLOW },
    [F_CAMPFIRE]    = { "모닥불",     "campfire",    0.0f, 0.0f, 0.0f, 0, C_ORANGE },
    [F_CHEST]       = { "보물상자",   "chest",       0.0f, 0.0f, 0.0f, 0, C_BROWN  },
    [F_BONE]        = { "뼈다귀",     "bone",        0.0f, 0.0f, 0.0f, 0, C_WHITE  },
    [F_COIN]        = { "동전",       "coin",        0.0f, 0.0f, 0.0f, 0, C_YELLOW },
    [F_CARAMEL]     = { "단짠",       "caramel",     1.0f, 0.0f, 0.8f, 0, C_ORANGE },   /* 함정: 사탕처럼 생겼고 단맛이 세다 */
};
static sprite_id_t spr_food[F_COUNT];

/* 아래 줄에 세는 맛의 갈래: 단맛, 쓴맛, 짠맛, 못 먹는 것(맛이 없는 물건) */
enum { CAT_SWEET, CAT_BITTER, CAT_SALTY, CAT_JUNK, CAT_COUNT };
static const char   *CAT_LABEL[CAT_COUNT] = { "단", "쓴", "짠", NULL };   /* 못 먹는 것은 쓰레기통 그림 */
static const color_t CAT_COLOR[CAT_COUNT] = { C_PINK, C_PURPLE, C_CYAN, C_GRAY3 };

static int category(int kind)
{
    const food_kind_t *k = &KIND[kind];
    if (k->good) return CAT_SWEET;
    if (k->sugar == 0 && k->bitter == 0 && k->salt == 0) return CAT_JUNK;
    return k->bitter >= k->salt ? CAT_BITTER : CAT_SALTY;
}

/* 판 전체에서 같은 음식. 모두에게 같다. */
typedef struct {
    int   kind;
    int   lane;                        /* 0, 1, 2 */
    float y;                           /* 음식의 아래쪽 */
    int   alive;
} food_t;

enum { D_NONE = 0, D_EATEN, D_PASSED };

/* 한 명 몫의 상태. 초파리도 같은 모양을 쓴다. */
typedef struct {
    unsigned char dec[MAX_FOODS];      /* 음식마다 먹었는지, 보냈는지 */
    unsigned char tasted[MAX_FOODS];   /* 초파리: 다리로 맛을 봤는지 */
    unsigned char dodged[MAX_FOODS];   /* 초파리: 다가오는 물건을 보고 피했는지(그 물건 줄로는 다시 안 간다) */
    int   spit_until;                  /* 초파리: 이 틱까지 "퉤!"(맛보고 거부함) */
    unsigned char layer[MAX_LAYERS];   /* 탑에 쌓인 음식 종류 */
    int   n_layers;
    int   lane;                        /* 가려는 줄 */
    float x;                           /* 지금 위치(줄 사이를 달려가는 중일 수 있다) */
    int   face_left;
    int   last_move;
    int   hearts;
    int   yuck_until;                  /* 이 틱까지 웩(못 먹음) */
    int   chew_until;                  /* 이 틱까지 씹는 모습 */
    int   combo_until;                 /* 이 틱까지 콤보 표시 */
    int   combo;
    int   eaten[CAT_COUNT];            /* 갈래마다 먹은 개수 */
    int   last_cat, last_until;        /* 방금 먹은 갈래를 이 틱까지 밝게 */
    float score;
} side_t;

static side_t pl[STAGE_MAX_PLAYERS];
static side_t fly;
static food_t foods[MAX_FOODS];
static int    n_foods, next_spawn, n_players, tick_no, total_ticks;

static int    kind_seq[MAX_FOODS], lane_seq[MAX_FOODS];   /* 씨앗에서 미리 만든 음식 순서와 줄 */
static float  fall_speed, eat_zone, touch_w, score_good, score_bad, score_skip_bad, feed_threshold, run_speed;
static float  loom_px, escape_threshold;
static int    spit_ticks, gap_start, gap_end, yuck_ticks, max_hearts, crumble_layers, tower_max, move_gap, fly_notice_y;
static sprite_id_t spr_person, spr_trash;

static const stage_info_t info = {
    .title    = "음식 골라 먹기",
    .rule     = "단 것만 받아먹기\n쓴 것, 짠 것, 물건은 피하기!",
    .controls = "A D",
};

const stage_info_t *stage_info(void) { return &info; }

static int lane_x(int lane) { return PANE_W / 2 + (lane - 1) * LANE_W; }

void stage_start(const stage_ctx_t *ctx)
{
    n_players   = ctx->n_players;
    total_ticks = ctx->n_ticks;

    fall_speed     = cfg_get("fall_speed", 1.6f);
    gap_start      = (int)cfg_get("gap_start_ticks", 30.0f);
    gap_end        = (int)cfg_get("gap_end_ticks", 22.0f);
    next_spawn     = (int)cfg_get("first_food_ticks", 10.0f);
    eat_zone       = cfg_get("eat_zone", 10.0f);
    touch_w        = cfg_get("touch_w", 10.0f);
    score_good     = cfg_get("score_good", 10.0f);
    score_bad      = cfg_get("score_bad", 10.0f);
    score_skip_bad = cfg_get("score_skip_bad", 2.0f);
    yuck_ticks     = (int)cfg_get("yuck_ticks", 16.0f);
    spit_ticks     = (int)cfg_get("fly_spit_ticks", 12.0f);
    max_hearts     = (int)cfg_get("hearts", 3.0f);
    crumble_layers = (int)cfg_get("crumble_layers", 2.0f);
    tower_max      = (int)cfg_get("tower_max_layers", 8.0f);
    run_speed      = cfg_get("run_speed", 5.0f);
    move_gap       = (int)cfg_get("move_gap_ticks", 3.0f);
    fly_notice_y   = (int)cfg_get("fly_notice_y", 40.0f);
    feed_threshold = cfg_get("feed_threshold", 0.5f);
    loom_px        = cfg_get("fly_loom_px", 24.0f);
    escape_threshold = cfg_get("escape_threshold", 0.5f);
    float bad_ratio  = cfg_get("bad_ratio", 0.45f);
    float trap_ratio = cfg_get("trap_ratio", 0.35f);
    int   max_traps  = (int)cfg_get("max_traps", 2.0f);

    /* 문제는 씨앗에서만 만든다. 같은 종류가 세 번 연달아 나오지 않게 한다. */
    stage_rng_t rng;
    stage_rng_seed(&rng, ctx->seed);
    int n_traps = 0;
    for (int i = 0; i < MAX_FOODS; i++) {
        int k;
        do {
            if (stage_rng_float(&rng) >= bad_ratio) {
                k = stage_rng_range(&rng, FIRST_GOOD, LAST_GOOD);
            } else if (stage_rng_float(&rng) < trap_ratio && n_traps < max_traps) {
                k = F_CARAMEL;
            } else {
                const int *r = BAD_RANGE[stage_rng_range(&rng, 0, 2)];
                k = stage_rng_range(&rng, r[0], r[1]);
            }
        } while (i >= 2 && kind_seq[i - 1] == k && kind_seq[i - 2] == k);
        kind_seq[i] = k;
        if (k == F_CARAMEL) n_traps++;
        lane_seq[i] = stage_rng_range(&rng, 0, N_LANES - 1);
    }

    spr_person = ui_sprite_load("person");
    spr_trash  = ui_sprite_load("trash");
    for (int k = 0; k < F_COUNT; k++) spr_food[k] = ui_sprite_load(KIND[k].sprite);
    for (int p = 0; p < STAGE_MAX_PLAYERS; p++)
        pl[p] = (side_t){ .hearts = max_hearts, .lane = 1, .x = lane_x(1), .last_move = -100 };
    fly = (side_t){ .hearts = max_hearts, .lane = 1, .x = lane_x(1), .last_move = -100 };
}

static int is_out(const side_t *s) { return s->hearts <= 0; }

/* 땅은 모두 같은 높이고 탑은 위로 자란다. 화면에 보이는 탑은 tower_max층까지다(층 수는 계속 센다). */
static int shown_layers(const side_t *s) { return s->n_layers < tower_max ? s->n_layers : tower_max; }
static int feet_y(const side_t *s)       { return GROUND_Y - shown_layers(s) * LAYER_H; }
static int head_y(const side_t *s)       { return feet_y(s) - BODY_H; }
static int running(const side_t *s)      { float d = s->x - lane_x(s->lane); return d > 0.5f || d < -0.5f; }

/* 음식이 몸에 닿았나: 가로로 touch_w 안이고, 음식의 아래쪽이 머리 근처다. 달리는 중에도 닿는다. */
static int in_mouth(const side_t *s, const food_t *f)
{
    float dx = lane_x(f->lane) - s->x;
    int h = head_y(s);
    return f->alive && dx < touch_w && dx > -touch_w && f->y >= h - eat_zone && f->y <= h + 4;
}

/* 몸에 닿아 있고 아직 정하지 않은 음식. 없으면 -1 */
static int food_at_mouth(const side_t *s)
{
    for (int i = 0; i < n_foods; i++)
        if (s->dec[i] == D_NONE && in_mouth(s, &foods[i])) return i;
    return -1;
}

static void eat(side_t *s, int i, int pane)
{
    int k = foods[i].kind;
    s->dec[i] = D_EATEN;
    s->chew_until = tick_no + 6;
    s->eaten[category(k)]++;
    s->last_cat = category(k);
    s->last_until = tick_no + 10;
    if (KIND[k].good) {
        s->combo++;
        s->score += score_good + (s->combo >= 3 ? score_good * 0.5f : 0.0f);   /* 3연속부터 보너스 */
        if (s->n_layers < MAX_LAYERS) s->layer[s->n_layers++] = (unsigned char)k;  /* 탑이 한 층 오른다 */
        if (s->combo >= 3) {
            s->combo_until = tick_no + 12;
            fx_shockwave(pane, (int)s->x, head_y(s) - 6);
        }
        fx_trail(pane, (int)s->x, feet_y(s));
    } else {
        s->combo = 0;
        s->hearts--;
        s->score -= score_bad;
        if (s->score < 0) s->score = 0;
        s->n_layers -= crumble_layers;                                   /* 탑이 무너진다 */
        if (s->n_layers < 0) s->n_layers = 0;
        s->yuck_until = tick_no + yuck_ticks;
        fx_shake(pane, 2, 200);
        fx_flash(pane, C_BAD, 80);
    }
}

/* 한 줄 옮긴다. 걸음 사이에 최소 간격을 둔다. 웩이나 퉤 하는 동안은 못 움직인다. */
static void step(side_t *s, int dir)
{
    if (is_out(s) || tick_no - s->last_move < move_gap) return;
    if (tick_no < s->yuck_until || tick_no < s->spit_until) return;
    int to = s->lane + dir;
    if (to < 0 || to >= N_LANES) return;
    s->lane = to;
    s->face_left = dir < 0;
    s->last_move = tick_no;
}

void stage_input(int player, int key, double t)
{
    (void)t;
    side_t *s = &pl[player];
    if (key == SK_LEFT)  step(s, -1);
    if (key == SK_RIGHT) step(s, +1);
}

/* 사람: 몸에 닿은 것은 저절로 먹는다. 웩 하는 동안에는 닿아도 그냥 떨어진다. */
static void catch_food(side_t *s, int pane)
{
    if (is_out(s) || tick_no < s->yuck_until) return;
    int i = food_at_mouth(s);
    if (i >= 0) eat(s, i, pane);
}

/* 머리를 지나간 음식을 정리한다. 나쁜 음식을 잘 보냈으면 조금 준다. */
static void settle(side_t *s, int i)
{
    if (s->dec[i] != D_NONE || foods[i].y <= head_y(s) + 4) return;
    s->dec[i] = D_PASSED;
    if (s->tasted[i]) {                                /* 초파리가 맛을 보고 거부했다: 뱉느라 잠깐 멈춘다 */
        s->spit_until = tick_no + spit_ticks;
        fx_trail(PANE_FLY, (int)s->x, head_y(s));
    }
    if (is_out(s)) return;
    if (KIND[foods[i].kind].good) s->combo = 0;
    else                          s->score += score_skip_bad;
}

/* 달려가는 중이면 목표 줄 쪽으로 다가간다 */
static void run(side_t *s, int pane)
{
    float tx = lane_x(s->lane), d = tx - s->x;
    if (d > run_speed)       s->x += run_speed;
    else if (d < -run_speed) s->x -= run_speed;
    else                     s->x = tx;
    if (running(s)) fx_trail(pane, (int)s->x, feet_y(s) - 2);
}

/* 머리 위로 떨어지는 물건(맛이 없는 것). 가까울수록 크게 보인다: 0~1. 없으면 -1 */
static int looming_object(const side_t *s, float *closeness)
{
    int h = head_y(s);
    for (int i = 0; i < n_foods; i++) {
        const food_t *f = &foods[i];
        if (!f->alive || s->dec[i] != D_NONE || category(f->kind) != CAT_JUNK) continue;
        float dx = lane_x(f->lane) - s->x;
        if (dx >= touch_w + 4 || dx <= -touch_w - 4) continue;
        float above = (float)(h - eat_zone) - f->y;            /* 닿기까지 남은 높이 */
        if (above > loom_px || above < -eat_zone) continue;
        float c = 1.0f - above / loom_px;
        *closeness = c > 1.0f ? 1.0f : c;
        return i;
    }
    return -1;
}

/* 초파리: 다음 음식의 줄로 날아가고(규칙), 머리 위 음식의 맛을 느껴 뇌가 먹자고 하면 먹는다.
 * 맛이 없는 물건은 다가오는 그림자(looming)로 뇌에 준다. 도망 뉴런이 켜지면 옆 줄로 피하고,
 * 못 피하면 머리에 맞아 하트를 잃는다. */
static void fly_think(void)
{
    if (is_out(&fly)) return;

    float close = 0.0f;
    int obj = looming_object(&fly, &close);
    if (obj >= 0) {
        /* 가장자리 줄에서는 열린 쪽으로 돌도록 반대편에서 다가오는 것으로 준다(L을 주면 오른쪽으로 돈다) */
        int from_left = fly.lane == 0 ? 1 : (fly.lane == N_LANES - 1 ? 0 : (obj % 2));
        brain_stimulate(from_left ? BR_LOOM_L : BR_LOOM_R, close);
        if (brain_rate(BR_ESCAPE) >= escape_threshold) {
            int dir = brain_rate(BR_TURN_R) >= brain_rate(BR_TURN_L) ? +1 : -1;
            if (fly.lane + dir < 0 || fly.lane + dir >= N_LANES) dir = -dir;
            int before = fly.lane;
            step(&fly, dir);
            if (fly.lane != before) fly.dodged[obj] = 1;
        }
    }

    for (int i = 0; i < n_foods; i++) {                         /* 아직 머리를 지나지 않은 첫 음식 */
        if (!foods[i].alive || fly.dec[i] != D_NONE || fly.dodged[i]) continue;
        /* 음식이 fly_notice_y보다 내려와야 알아차린다(사람이 이길 틈을 두는 난이도 숫자) */
        if (foods[i].y >= fly_notice_y && foods[i].lane != fly.lane)
            step(&fly, foods[i].lane > fly.lane ? +1 : -1);
        break;
    }

    int i = food_at_mouth(&fly);
    if (i < 0) return;
    if (category(foods[i].kind) == CAT_JUNK) {                  /* 못 피한 물건에 맞았다 */
        if (tick_no >= fly.yuck_until) eat(&fly, i, PANE_FLY);
        return;
    }
    const food_kind_t *k = &KIND[foods[i].kind];
    fly.tasted[i] = 1;
    /* 맛은 음식이 머리 위에 머무는 동안 매 틱 준다. 진짜 뇌는 반응이 2~3틱 늦다. */
    if (k->sugar  > 0) brain_stimulate(BR_SUGAR,  k->sugar);
    if (k->bitter > 0) brain_stimulate(BR_BITTER, k->bitter);
    if (k->salt   > 0) brain_stimulate(BR_SALT,   k->salt);

    if (tick_no >= fly.yuck_until && brain_rate(BR_FEED) >= feed_threshold)
        eat(&fly, i, PANE_FLY);
}

void stage_tick(double dt)
{
    (void)dt;
    /* 새 음식. 갈수록 자주 떨어진다 */
    if (tick_no >= next_spawn && n_foods < MAX_FOODS) {
        foods[n_foods] = (food_t){ .kind = kind_seq[n_foods], .lane = lane_seq[n_foods],
                                   .y = SPAWN_Y, .alive = 1 };
        n_foods++;
        float p = total_ticks > 0 ? (float)tick_no / total_ticks : 0.0f;
        next_spawn = tick_no + gap_start + (int)((gap_end - gap_start) * p);
    }

    fly_think();
    run(&fly, PANE_FLY);
    for (int p = 0; p < n_players; p++) { run(&pl[p], p); catch_food(&pl[p], p); }

    /* 음식을 떨어뜨리고, 머리를 지나간 음식을 모두에게 정리한다 */
    for (int i = 0; i < n_foods; i++) {
        if (!foods[i].alive) continue;
        foods[i].y += fall_speed;
        settle(&fly, i);
        for (int p = 0; p < n_players; p++) settle(&pl[p], i);
        if (foods[i].y > PANE_H + 12) foods[i].alive = 0;
    }
    tick_no++;
}

/* ---- 그리기 ---- */

static void draw_food(canvas_t *c, int kind, int x, int y)   /* (x, y): 그림의 아래 가운데 */
{
    sprite_id_t id = spr_food[kind];
    ui_sprite(c, id, x - ui_sprite_w(id) / 2, y - ui_sprite_h(id), 0);
}

/* 배경과 바닥. 예제(돌 피하기)와 같은 어두운 톤 */
static void draw_backdrop(canvas_t *c)
{
    ui_clear(c, C_BG);
    ui_rect(c, 0, GROUND_Y, PANE_W, 2, C_GRAY2);
}

/* 발밑의 탑. 땅에서 위로 쌓이고, 맨 위층이 발에 닿는다. 사람이 달리면 탑도 같이 미끄러진다. */
static void draw_tower(canvas_t *c, const side_t *s)
{
    int x = (int)s->x;
    for (int l = s->n_layers - 1, y = feet_y(s); l >= 0 && y < GROUND_Y; l--, y += LAYER_H) {
        int w = 16 - ((s->n_layers - 1 - l) % 2) * 2;          /* 층마다 폭을 살짝 달리 */
        ui_rect(c, x - w / 2, y, w, LAYER_H - 1, KIND[s->layer[l]].layer);
        ui_rect(c, x - w / 2, y + LAYER_H - 1, w, 1, C_BLACK);
    }
}

static void draw_heart(canvas_t *c, int x, int y, color_t col)   /* 7x6 픽셀 하트 */
{
    ui_rect(c, x + 1, y, 2, 1, col);
    ui_rect(c, x + 4, y, 2, 1, col);
    ui_rect(c, x, y + 1, 7, 2, col);
    ui_rect(c, x + 1, y + 3, 5, 1, col);
    ui_rect(c, x + 2, y + 4, 3, 1, col);
    ui_px(c, x + 3, y + 5, col);
}

static void draw_hud(canvas_t *c, const side_t *s)
{
    char buf[32];
    ui_rect(c, 2, 2, 8, 8, C_YELLOW);                           /* 코인 */
    ui_rect(c, 4, 4, 4, 4, C_ORANGE);
    snprintf(buf, sizeof buf, "%d", (int)s->score);
    ui_text(c, 12, 2, buf, C_TEXT);

    snprintf(buf, sizeof buf, "%d층", s->n_layers);
    ui_text(c, PANE_W / 2 - ui_text_w(buf) / 2, 0, buf, C_TEXT);

    for (int h = 0; h < max_hearts; h++)                        /* 하트 */
        draw_heart(c, PANE_W - 9 * (max_hearts - h) - 1, 3, h < s->hearts ? C_RED : C_GRAY1);
}

/* 칸 맨 아래: 갈래마다 먹은 개수. 사람과 초파리 칸에 똑같이 그린다 */
static void draw_tally(canvas_t *c, const side_t *s)
{
    char buf[16];
    int y = GROUND_Y + 5;
    for (int k = 0; k < CAT_COUNT; k++) {
        int x = 2 + k * 23;
        int lit = tick_no < s->last_until && s->last_cat == k;
        if (lit) ui_rect(c, x - 1, y - 1, 22, 14, C_GRAY1);
        if (CAT_LABEL[k]) ui_text(c, x, y, CAT_LABEL[k], CAT_COLOR[k]);
        else              ui_sprite(c, spr_trash, x + 1, y, 0);
        snprintf(buf, sizeof buf, "%d", s->eaten[k] > 9 ? 9 : s->eaten[k]);
        ui_text(c, x + 14, y, buf, lit ? C_WHITE : C_TEXT);
    }
}

static void draw_side(canvas_t *c, const side_t *s, int is_fly)
{
    int hy = head_y(s), fy = feet_y(s), x = (int)s->x;
    draw_backdrop(c);
    draw_tower(c, s);

    /* 떨어지는 음식. 먹은 것은 안 보이고, 지나간 것은 옆으로 튕겨 나간다 */
    for (int i = 0; i < n_foods; i++) {
        if (!foods[i].alive || s->dec[i] == D_EATEN || foods[i].y > GROUND_Y) continue;
        int fx = lane_x(foods[i].lane), y = (int)foods[i].y;
        float near = lane_x(foods[i].lane) - s->x;
        if (s->dec[i] == D_PASSED && near < touch_w + 8 && near > -touch_w - 8) {
            /* 몸 위로 떨어졌는데 못 먹은 것(웩, 탈락 중)은 머리에 맞고 옆으로 튕겨 나간다 */
            float d = foods[i].y - (hy + 4);
            fx += (int)((i % 2 ? 1 : -1) * d * 3.0f);
            y = hy + 4 - (int)(d * 0.4f);
        }
        draw_food(c, foods[i].kind, fx, y);
    }

    int yuck = tick_no < s->yuck_until;
    int chew = tick_no < s->chew_until;
    if (is_fly) {
        /* 초파리는 자기 탑 위에 떠서 난다(사람의 머리 높이) */
        sprite_id_t id = (yuck || is_out(s)) ? SPR_FLY_HIT
                       : (chew ? SPR_FLY_EAT : (running(s) ? SPR_FLY_JUMP : SPR_FLY_IDLE));
        int bob = (tick_no / 4) % 2;
        ui_sprite(c, id, x - ui_sprite_w(id) / 2, hy + bob, tick_no / 3);
        ui_rect(c, x - 4, fy - 2, 8, 1, C_GRAY1);              /* 그림자 */
    } else {                                                    /* 사람 (assets/person.spr) */
        int frame = (yuck || is_out(s)) ? 2 : (chew ? 1 : (running(s) ? 3 + (tick_no / 2) % 2 : 0));
        if (s->face_left) frame += 5;
        ui_sprite(c, spr_person, x - ui_sprite_w(spr_person) / 2, fy - ui_sprite_h(spr_person), frame);
    }

    if (tick_no < s->combo_until) {                             /* 콤보 */
        int cy = hy - 30 < HUD_H + 4 ? HUD_H + 4 : hy - 30;
        int cx = x < 30 ? 30 : (x > PANE_W - 30 ? PANE_W - 30 : x);
        ui_rect(c, cx - 22, cy, 44, 14, C_YELLOW);
        ui_rect(c, cx - 18, cy - 3, 36, 20, C_YELLOW);
        ui_text(c, cx - ui_text_w("콤보!") / 2, cy + 1, "콤보!", C_RED);
    }
    if (yuck && !is_out(s)) ui_text(c, x + 8 > PANE_W - 24 ? x - 32 : x + 8, hy - 2, "웩!", C_BAD);
    else if (tick_no < s->spit_until)                          /* 초파리가 맛보고 거부함 */
        ui_text(c, x + 8 > PANE_W - 24 ? x - 32 : x + 8, hy - 2, "퉤!", C_GRAY3);

    draw_tally(c, s);
    draw_hud(c, s);
    if (is_out(s)) {
        ui_rect(c, 10, 46, PANE_W - 20, 16, C_GRAY1);
        ui_text(c, PANE_W / 2 - ui_text_w("탈락") / 2, 48, "탈락", C_BAD);
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
