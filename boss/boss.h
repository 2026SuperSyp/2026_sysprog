/* boss.h : 보스전 계산부 (입출력 없음)
 *
 * 미니게임 4라운드 뒤에 살아남은 참가자들이 거대한 초파리 한 마리와 싸운다. 화면은 1인칭 3D(레이캐스팅)로
 * 그리지만 그것은 클라이언트의 일이고, 이 파일은 계산만 한다. 서버, 봇, 혼자 하기가 같은 계산부를 쓴다.
 * 입출력, 시계, 전역 상태가 없다. 같은 씨앗과 같은 입력이면 같은 결과가 나온다.
 *
 * 한 틱(50ms)은 이렇게 돈다. 계산부는 뇌를 직접 부르지 않는다. 부르는 쪽이 둘을 잇는다.
 *   boss_input(s, 참가자, 키)        틱 사이에 들어온 키(몇 번이든)
 *   brain_host_begin_tick()
 *   boss_stimulus(s, stim)           장면에서 자극을 계산한다 -> brain_stimulate(집단, stim[집단])
 *   rate[집단] = brain_rate(집단)
 *   brain_host_end_tick()
 *   boss_tick(s, rate)               반응으로 초파리를 움직이고, 공격하고, 판정한다
 *   boss_snapshot(s, &snap)          보낼 것과 그릴 것
 *
 * 키 (include/stage.h의 SK_*)
 *   SK_UP / SK_DOWN      한 걸음 앞 / 뒤. 한 걸음은 step_ticks(3틱)에 걸쳐 고르게 움직이고, 누르고 있어도 그보다
 *                        빨리 걷지 못한다
 *   SK_LEFT / SK_RIGHT 에 BOSS_STRAFE를 더하면   시선은 그대로 두고 왼쪽 / 오른쪽으로 한 걸음(옆걸음)
 *   이동 키에 BOSS_SLOW를 더하면                 그 걸음은 살금살금이다(걸음이 짧다, slow_step)
 *   BOSS_SNEAK           살금살금을 켜고 끈다. 켜져 있으면 모든 걸음이 살금살금이다
 *   BOSS_GUARD           막기. 잠깐(guard_ticks) 막는 자세를 한다. 그동안 돌진에 닿으면 다치지 않고 초파리가 잠깐 멈춘다.
 *                        한 번 쓰면 guard_cooldown 동안 다시 못 쓴다
 *   BOSS_JUMP            뛰어오른다(jump_ticks 동안, 높이는 boss_jump_curve). 돌진이 닿는 순간에 jump_clear보다 높이
 *                        떠 있으면 돌진이 발밑으로 지나가서 맞지 않는다. 막기와 달리 초파리는 멈추지 않는다.
 *                        아직 떠 있을 때 누르면 jump_buffer 동안 기억해 두었다가 내려서자마자 다시 뛴다.
 *                        걷다가 뛰면 걷던 빠르기와 방향 그대로 날아간다. 떠 있을 때 누른 이동 키는 날아가는 쪽을 바꾼다
 *   SK_LEFT / SK_RIGHT   한 칸(22.5도) 왼쪽 / 오른쪽으로 돈다(마우스가 없을 때와 봇이 쓴다)
 *   SK_1, SK_2, SK_3, SK_4   파리채 / 스프레이 / 끈끈이 / 먹이를 고른다
 *   SK_SUB               다음 무기로 바꾼다
 *   SK_ACT               고른 무기를 쓴다
 * 시선은 boss_look으로도 정한다(마우스). 각도를 통째로 받아 다음 틱에 그쪽을 보게 한다.
 *
 * 좌표: 칸 단위. x는 오른쪽, y는 아래쪽으로 는다. 각도 0은 오른쪽(+x)이고 각도가 줄면 왼쪽으로 돈다.
 * 수치는 모두 boss_cfg_t에 있고 boss/config.ini로 바꿀 수 있다. */
#ifndef SSP_BOSS_H
#define SSP_BOSS_H

#include <stdint.h>
#include "boss/body.h"
#include "brain.h"
#include "stage.h"

#define BOSS_MAP        18       /* 맵은 18 x 18칸(바깥 벽을 빼면 16 x 16). 24칸일 때는 쫓아다니는 길이 너무 길었다 */
#define BOSS_PLAYERS    3
#define BOSS_PROJ       8        /* 날아가는 끈끈이 수의 한도 */
#define BOSS_BAITS      4        /* 바닥에 놓인 먹이 수의 한도 */
#define BOSS_EVENTS     16       /* 한 틱에 남기는 사건 수의 한도 */
#define BOSS_DIRS       16       /* 키로 돌 때 한 바퀴를 나누는 수(한 번에 22.5도) */
#define BOSS_FIX        256      /* 스냅숏 좌표의 고정 소수점: 1칸 = 256 */
#define BOSS_SLOW       0x100    /* boss_input의 이동 키에 더한다: 이 걸음은 살금살금 */
#define BOSS_STRAFE     0x200    /* boss_input의 SK_LEFT, SK_RIGHT에 더한다: 돌지 않고 옆으로 걷는다 */
#define BOSS_SNEAK      0x400    /* boss_input의 키 하나: 살금살금을 켜고 끈다 */
#define BOSS_GUARD      0x800    /* boss_input의 키 하나: 막기 */
#define BOSS_JUMP       0x1000   /* boss_input의 키 하나: 뛰어오르기 */

enum { BOSS_SWATTER = 0, BOSS_SPRAY, BOSS_TRAP, BOSS_BAIT, BOSS_ITEMS };
enum { BOSS_GOING = 0, BOSS_PLAYERS_WIN, BOSS_FLY_WINS };

/* 사건. who는 참가자 번호(초파리가 한 일이면 대상 참가자, 없으면 255), x와 y는 일어난 곳 */
enum {
    BOSS_EV_SWAT_HIT = 1, BOSS_EV_SWAT_MISS,
    BOSS_EV_SPRAY_HIT, BOSS_EV_SPRAY_MISS,       /* x, y는 물줄기가 끝난 곳 */
    BOSS_EV_TRAP_THROW, BOSS_EV_TRAP_MISS,       /* 끈끈이가 벽에 닿았거나 다 날아갔다 */
    BOSS_EV_STUCK,                               /* 끈끈이에 붙었다 */
    BOSS_EV_CHARGE,                              /* 돌진을 시작한다(who가 대상) */
    BOSS_EV_HIT_PLAYER,                          /* 돌진에 부딪혔다 */
    BOSS_EV_WAVE,                                /* 날갯짓 */
    BOSS_EV_DEATH,                               /* 참가자가 쓰러졌다 */
    BOSS_EV_FLY_DOWN,                            /* 초파리가 쓰러졌다 */
    BOSS_EV_TAKEOFF,                             /* 초파리가 뛰어올랐다(x, y는 뛴 곳) */
    BOSS_EV_LAND,                                /* 내려앉았다 */
    BOSS_EV_BLOCK,                               /* 돌진을 막았다(who가 막은 참가자) */
    BOSS_EV_DODGE,                               /* 돌진을 뛰어넘어 피했다(who가 피한 참가자) */
    BOSS_EV_BAIT,                                /* 먹이를 놓았다(x, y는 놓인 곳) */
    BOSS_EV_EAT                                  /* 초파리가 먹기 시작했다 */
};

typedef struct {
    int   ticks;                 /* 판의 길이(틱). 180초 */
    float fly_hp[BOSS_PLAYERS];  /* 초파리 체력. 참가자가 1명, 2명, 3명일 때의 값을 따로 정한다 */
    float fly_radius, player_radius;
    float step, slow_step;                       /* 참가자의 한 걸음과 살금살금 한 걸음(칸) */
    int   step_ticks;                            /* 한 걸음에 걸리는 틱 수. 키를 누르고 있어도 이보다 빨리 걷지 못한다 */
    /* 초파리의 몸. 실제 초파리에서 잰 값이다(출처는 docs/brain-notes.md). 몸길이로 나눠 거대 초파리에 옮긴다 */
    float body_mm;                               /* 몸길이. 게임에서는 지름(fly_radius x 2)이 몸길이다 */
    float walk_mm_s, run_mm_s, back_mm_s;        /* 앞으로 걷기, 달아나기, 뒤로 걷기의 빠르기(반응이 1일 때) */
    float turn_deg_s;                            /* 도는 빠르기(반응이 1일 때) */
    /* 몸(boss/body.h). 걸음 생성기의 수치는 NeuroMechFly v2(Wang-Chen 외 2024)의 것이고, 나머지는 가정이다(docs/brain-notes.md) */
    float step_hz, cpg_couple, cpg_converge;     /* 걸음 빈도(게임에서의 값), 진동자 사이의 세기, 크기가 따라가는 빠르기 */
    float swing_front, swing_mid, swing_hind;    /* 발을 드는 동안이 끝나는 위상(라디안) */
    float foot_lift, foot_fore, foot_side;       /* 발을 드는 높이, 앞뒤 다리의 발이 놓이는 앞뒤 거리, 발이 놓이는 옆 거리(칸) */
    float body_spring_hz, body_damping;          /* 다리 용수철이 몸을 붙드는 빠르기(Hz)와 감쇠의 비 */
    float leg_grip, hit_push;                    /* 다리 하나가 버티는 힘, 파리채에 맞았을 때 밀려나는 빠르기(칸/초) */
    float speed_scale, turn_scale;               /* 게임용 배율. 실제 비율 그대로면 사람이 따라갈 수 없어서 전체를 줄인다 */
    /* 도약. 뇌의 달아남 출력(거대 섬유 뉴런)이 takeoff_level 이상이면 뛰어오른다 */
    float takeoff_level, takeoff_mm_s;           /* 뛰는 기준(반응), 뛰어오른 직후의 빠르기(잰 값) */
    float takeoff_scale;                         /* 도약에만 곱하는 게임용 배율(speed_scale 대신) */
    int   guard_ticks, guard_cooldown, guard_stun;   /* 막는 자세의 길이, 다시 쓸 때까지, 막힌 초파리가 멈추는 시간 */
    int   jump_ticks, jump_rest;                 /* 뛰어올라 떠 있는 시간, 내려선 뒤 다시 뛰지 못하는 시간 */
    int   jump_buffer;                           /* 뛸 수 없을 때 누른 키를 기억해 두는 시간. 그 안에 뛸 수 있게 되면 뛴다 */
    int   jump_carry;                            /* 뛰기 전 이만큼 안에 걷고 있었으면 그 빠르기와 방향으로 날아간다 */
    float jump_height, jump_clear;               /* 꼭대기의 높이(칸), 이 높이 이상이면 돌진이 발밑으로 지나간다 */
    /* 먹이. 초파리는 bait_smell 안에 보이는 먹이를 물체로 보고 걸어가고(뇌의 걷기 명령), 닿으면 단맛 자극
     * (bait_sugar)이 뇌로 들어간다. 뇌의 먹기 반응이 feed_level 이상이면 먹는다. 다 먹는 데 bait_ticks가 걸리고
     * 다 먹으면 체력이 bait_heal만큼 돌아온다. 먹이 쪽에 있다가 맞으면 먹이를 버리고 bait_wary 동안 먹이를 찾지 않는다 */
    int   bait_count, bait_ticks, bait_wary;
    float bait_throw, bait_smell, bait_sugar, bait_heal, feed_level;
    /* 보이는 물체. 초파리는 see_range 안에 보이는 참가자와 bait_smell 안에 보이는 먹이를 물체로 본다. 가까울수록 세고,
     * 세기에 see_player, see_bait를 곱한다. 이 자극이 뇌의 걷기 명령 뉴런을 켜서 초파리가 그쪽으로 걸어간다 */
    float see_range, see_player, see_bait;
    float pursue_turn;                           /* 걷기 명령의 좌우 차이가 도는 빠르기에 주는 몫 */
    int   air_ticks, air_rest_ticks;             /* 떠 있는 시간, 내려앉은 뒤 다시 뛰지 못하는 시간(둘 다 게임 규칙) */
    float swat_range, swat_damage; int swat_cooldown;
    float spray_range, spray_damage; int spray_ammo, spray_cooldown;
    float trap_speed, trap_range; int trap_count, stun_ticks;
    float charge_range, charge_speed; int charge_windup, charge_ticks, charge_cooldown;
    float wave_radius, wave_push; int wave_period;
    float loom_range, wall_sense, rush_level;    /* 다가옴을 느끼는 거리, 벽을 느끼는 거리, 몰린다고 보는 자극 */
    float wall_turn;                             /* 벽이 바로 앞일 때 반대쪽으로 도는 세기(도는 빠르기의 배수). 뇌와 무관한 몸의 규칙 */
    /* 다가옴 자극 = 가까움 x (loom_base + loom_gain x 다가오는 빠르기). 빠르기는 참가자가 초파리 쪽으로 움직인 거리를
     * loom_avg_ticks 동안 평균해서 loom_speed(칸/틱)로 나눈 것이다. 천천히 다가가면 자극이 약하다 */
    float loom_base, loom_gain, loom_speed;
    int   loom_avg_ticks;
    float near_range, near_gain;                 /* 이 거리 안의 참가자는 서 있기만 해도 자극이 된다(가장 가까울 때 near_gain만큼 더한다) */
    int   hurt_ticks;                            /* 맞은 뒤 이만큼은 때린 쪽에서 강한 자극이 들어오고, 돌진하지 않는다 */
} boss_cfg_t;

typedef struct {
    float x, y;
    float ang;                   /* 보는 방향(라디안, 0 이상 2파이 미만) */
    int   hp, alive, item, ammo, traps, cool;
    int   move, strafe, turn, use, pick;         /* 다음 틱에 할 일. move는 앞(1)/뒤(-1), strafe는 오른쪽(1)/왼쪽(-1) */
    int   slow, sneak;           /* 다음 걸음이 살금살금인가(키에 BOSS_SLOW), 살금살금을 켜 두었는가(BOSS_SNEAK) */
    int   guard, guard_cool;     /* 막는 자세가 남은 틱, 막기를 다시 쓸 때까지 남은 틱 */
    int   jump, jump_rest;       /* 뛰어올라 떠 있는 남은 틱, 내려선 뒤 다시 뛸 때까지 남은 틱 */
    int   jump_buf;              /* 뛸 수 없을 때 누른 키를 기억하는 남은 틱 */
    int   carry;                 /* 이번 뜀은 걷던 기세로 날아간다(내려설 때까지 틱마다 step_dx, step_dy만큼 간다) */
    int   moved_ago;             /* 마지막으로 움직인 뒤 지난 틱 */
    float z;                     /* 발이 바닥에서 떠 있는 높이(칸) */
    int   baits;                 /* 남은 먹이 */
    int   has_look;              /* boss_look이 왔다. 다음 틱에 look을 보는 방향으로 삼는다 */
    float look;
    int   step_left;             /* 걷고 있는 걸음의 남은 틱. 0이어야 새 걸음을 뗀다 */
    float step_dx, step_dy;      /* 걷고 있는 걸음이 한 틱에 가는 거리(칸) */
    float closing, approach;     /* 이번 틱에 초파리 쪽으로 움직인 거리(칸), 그것의 평균(칸/틱) */
} boss_player_t;

typedef struct {
    float x, y, ang;
    int   hp, hp_max, stun;
    int   charge_cool, windup, charging, target;
    int   wave_in;
    int   stuck_ticks;           /* 가려고 했는데 거의 못 간 틱 수 */
    float v, w;                  /* 방금 틱에 움직인 거리(칸)와 돈 각도(라디안). 기록용 */
    body_t body;                 /* 몸: 다리 여섯의 걸음과 힘(boss/body.h). 걸을 때의 자리와 방향은 여기서 나온다 */
    int   hurt;                  /* 맞은 뒤 남은 틱 */
    int   air, air_rest;         /* 떠 있는 남은 틱, 내려앉은 뒤 쉬는 남은 틱 */
    int   dodged;                /* 이번 돌진을 뛰어넘어 피한 참가자들(비트). 피한 사람은 그 돌진에 다시 맞지 않는다 */
    int   feeding, lured;        /* 이번 틱에 먹고 있다, 먹이 쪽으로 가고 있다 */
    int   bait;                  /* 그 먹이의 번호 */
    int   wary;                  /* 놀라서 먹이를 찾지 않는 남은 틱 */
    float air_dx, air_dy;        /* 뛰어오른 방향(길이 1) */
    float hurt_x, hurt_y;        /* 때린 사람이 있던 곳 */
} boss_fly_t;

typedef struct { int on, owner; float x, y, vx, vy, left; } boss_proj_t;
typedef struct { int on; float x, y; int left; } boss_bait_t;      /* left: 다 먹을 때까지 남은 틱 */
typedef struct { uint8_t kind, who; int16_t x, y; } boss_event_t;

typedef struct {
    boss_cfg_t    cfg;
    stage_rng_t   rng;
    uint8_t       wall[BOSS_MAP][BOSS_MAP];
    int           n_players, tick, outcome;
    boss_player_t p[BOSS_PLAYERS];
    boss_fly_t    fly;
    body_cfg_t    bcfg;            /* 몸의 수치(cfg에서 구한다) */
    float         walk_v1, turn_gain;  /* 명령이 1일 때 걷는 빠르기(칸/초), 도는 명령을 좌우 보폭의 차이로 바꾸는 값 */
    boss_proj_t   proj[BOSS_PROJ];
    boss_bait_t   bait[BOSS_BAITS];
    boss_event_t  ev[BOSS_EVENTS];
    int           n_ev;
    /* 기록(봇과 조정에 쓴다) */
    int           dmg_dealt[BOSS_PLAYERS], dmg_taken[BOSS_PLAYERS];
} boss_state_t;

/* 보낼 것. 그리는 데 필요한 것만 담은 고정 크기 구조체다. 좌표는 BOSS_FIX배 정수다. */
typedef struct {
    uint16_t tick, ticks_left;
    uint8_t  outcome, n_players, n_proj, n_ev;
    struct {
        int16_t  x, y;
        uint16_t ang;            /* 보는 방향. 0..65535가 한 바퀴 */
        uint8_t  hp, alive, item, ammo, traps, cool, sneak, guard;   /* guard: 막는 자세가 남은 틱 */
        uint8_t  guard_cool, z;  /* 막기를 다시 쓸 때까지 남은 틱(255에서 자른다), 뛰어오른 높이(칸의 1/100) */
        uint8_t  baits, pad;     /* 남은 먹이 */
    } p[BOSS_PLAYERS];
    struct {
        int16_t  x, y;
        uint16_t ang;            /* 0..65535가 한 바퀴 */
        int16_t  hp, hp_max;
        uint8_t  stun, windup, charging, wave_in;   /* stun, wave_in은 남은 틱(255에서 자른다) */
        uint8_t  air, target;                       /* 떠 있는 남은 틱, 돌진이 겨누는 참가자(없으면 255) */
        uint8_t  feeding, lured;                    /* 먹고 있다, 먹이 쪽으로 가고 있다 */
        uint8_t  leg[BODY_LEGS], leg_mag[BODY_LEGS];   /* 다리의 걸음: 위상(256이 한 바퀴)과 크기(127이 1). 발의 자리는 body_foot으로 구한다 */
    } fly;
    struct { int16_t x, y; } proj[BOSS_PROJ];
    struct { int16_t x, y; uint8_t left, pad; } bait[BOSS_BAITS];      /* 놓인 먹이. left: 남은 양(255가 처음) */
    uint8_t n_bait, pad;
    boss_event_t ev[BOSS_EVENTS];
} boss_snap_t;

void boss_cfg_default(boss_cfg_t *c);
void boss_cfg_load(boss_cfg_t *c, const char *path);    /* 기본값에서 시작해 파일에 있는 것만 바꾼다 */

/* lives[i]는 참가자 i의 시작 체력(1 이상)이다. 서버는 모두에게 3을 준다(미니게임에서 남은 기회와 무관하다) */
void boss_init(boss_state_t *s, const boss_cfg_t *c, uint32_t seed, int n_players, const int lives[]);
void boss_input(boss_state_t *s, int player, int key);
void boss_look(boss_state_t *s, int player, float ang);     /* 보는 방향을 정한다(라디안). 다음 틱에 반영된다 */
/* 뛰어오른 높이의 모양. t는 뛰기 시작한 뒤로 지난 정도(0~1)이고 꼭대기가 1이다. 서버의 판정과 클라이언트의 화면이 같이 쓴다 */
float boss_jump_curve(float t);
void boss_stimulus(const boss_state_t *s, float stim[BRAIN_GROUP_COUNT]);
void boss_tick(boss_state_t *s, const float rate[BRAIN_GROUP_COUNT]);
void boss_snapshot(const boss_state_t *s, boss_snap_t *out);

/* 몸의 수치를 설정에서 구한다. 그리는 쪽도 같은 값으로 발의 자리를 구한다 */
void boss_body_cfg(const boss_cfg_t *c, body_cfg_t *out);

/* 그리기와 검사용 */
int  boss_solid(const boss_state_t *s, int cx, int cy);

#endif
