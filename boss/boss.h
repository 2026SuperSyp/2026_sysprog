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
 *   SK_UP / SK_DOWN      한 걸음 앞 / 뒤 (누를 때마다 한 걸음)
 *   SK_LEFT / SK_RIGHT   한 칸(22.5도) 왼쪽 / 오른쪽으로 돈다
 *   SK_1, SK_2, SK_3     파리채 / 스프레이 / 끈끈이를 고른다
 *   SK_SUB               다음 무기로 바꾼다
 *   SK_ACT               고른 무기를 쓴다
 *
 * 좌표: 칸 단위. x는 오른쪽, y는 아래쪽으로 는다. 각도 0은 오른쪽(+x)이고 각도가 줄면 왼쪽으로 돈다.
 * 수치는 모두 boss_cfg_t에 있고 boss/config.ini로 바꿀 수 있다. */
#ifndef SSP_BOSS_H
#define SSP_BOSS_H

#include <stdint.h>
#include "brain.h"
#include "stage.h"

#define BOSS_MAP        24       /* 맵은 24 x 24칸 */
#define BOSS_PLAYERS    3
#define BOSS_PROJ       8        /* 날아가는 끈끈이 수의 한도 */
#define BOSS_EVENTS     16       /* 한 틱에 남기는 사건 수의 한도 */
#define BOSS_DIRS       16       /* 참가자가 볼 수 있는 방향 수 */
#define BOSS_FIX        256      /* 스냅숏 좌표의 고정 소수점: 1칸 = 256 */

enum { BOSS_SWATTER = 0, BOSS_SPRAY, BOSS_TRAP, BOSS_ITEMS };
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
    BOSS_EV_FLY_DOWN                             /* 초파리가 쓰러졌다 */
};

typedef struct {
    int   ticks;                 /* 판의 길이(틱). 180초 */
    float fly_hp_per_player;     /* 초파리 체력 = 이것 x 참가자 수 */
    float fly_radius, player_radius;
    float step, fly_speed, fly_turn;             /* 한 걸음(칸), 초파리의 빠르기(칸/틱), 도는 빠르기(라디안/틱) */
    float swat_range, swat_damage; int swat_cooldown;
    float spray_range, spray_damage; int spray_ammo, spray_cooldown;
    float trap_speed, trap_range; int trap_count, stun_ticks;
    float charge_range, charge_speed; int charge_windup, charge_ticks, charge_cooldown;
    float wave_radius, wave_push; int wave_period;
    float loom_range, wall_sense, rush_level;    /* 다가옴을 느끼는 거리, 벽을 느끼는 거리, 몰린다고 보는 자극 */
    float near_range, near_gain;                 /* 이 거리 안의 참가자는 서 있기만 해도 자극이 된다(가장 가까울 때 near_gain만큼 더한다) */
    int   hurt_ticks;                            /* 맞은 뒤 이만큼은 때린 쪽에서 강한 자극이 들어오고, 돌진하지 않는다 */
} boss_cfg_t;

typedef struct {
    float x, y;
    int   dir;                   /* 0 .. BOSS_DIRS-1 */
    int   hp, alive, item, ammo, traps, cool;
    int   move, turn, use, pick; /* 다음 틱에 할 일 */
    float last_dist;             /* 지난 틱의 초파리까지 거리(다가오는 빠르기를 잰다) */
} boss_player_t;

typedef struct {
    float x, y, ang;
    int   hp, hp_max, stun;
    int   charge_cool, windup, charging, target;
    int   wave_in;
    int   stuck_ticks;           /* 가려고 했는데 거의 못 간 틱 수 */
    int   hurt;                  /* 맞은 뒤 남은 틱 */
    float hurt_x, hurt_y;        /* 때린 사람이 있던 곳 */
} boss_fly_t;

typedef struct { int on, owner; float x, y, vx, vy, left; } boss_proj_t;
typedef struct { uint8_t kind, who; int16_t x, y; } boss_event_t;

typedef struct {
    boss_cfg_t    cfg;
    stage_rng_t   rng;
    uint8_t       wall[BOSS_MAP][BOSS_MAP];
    int           n_players, tick, outcome;
    boss_player_t p[BOSS_PLAYERS];
    boss_fly_t    fly;
    boss_proj_t   proj[BOSS_PROJ];
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
        int16_t x, y;
        uint8_t dir, hp, alive, item, ammo, traps, cool, pad;
    } p[BOSS_PLAYERS];
    struct {
        int16_t  x, y;
        uint16_t ang;            /* 0..65535가 한 바퀴 */
        int16_t  hp, hp_max;
        uint8_t  stun, windup, charging, wave_in;   /* stun, wave_in은 남은 틱(255에서 자른다) */
    } fly;
    struct { int16_t x, y; } proj[BOSS_PROJ];
    boss_event_t ev[BOSS_EVENTS];
} boss_snap_t;

void boss_cfg_default(boss_cfg_t *c);
void boss_cfg_load(boss_cfg_t *c, const char *path);    /* 기본값에서 시작해 파일에 있는 것만 바꾼다 */

/* lives[i]는 미니게임에서 남은 목숨(1~3). 그것이 보스전의 체력이 된다 */
void boss_init(boss_state_t *s, const boss_cfg_t *c, uint32_t seed, int n_players, const int lives[]);
void boss_input(boss_state_t *s, int player, int key);
void boss_stimulus(const boss_state_t *s, float stim[BRAIN_GROUP_COUNT]);
void boss_tick(boss_state_t *s, const float rate[BRAIN_GROUP_COUNT]);
void boss_snapshot(const boss_state_t *s, boss_snap_t *out);

/* 그리기와 검사용 */
int  boss_solid(const boss_state_t *s, int cx, int cy);

#endif
