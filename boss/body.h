/* body.h : 초파리의 몸 (걸음과 힘). 입출력이 없는 계산부다
 *
 * 뇌에서 내려온 명령은 좌우 두 값이다(왼쪽 다리들, 오른쪽 다리들). 이 두 값이 다리 여섯 개의 걸음을 조절하고,
 * 딛고 있는 다리가 몸을 밀어 몸이 움직인다. 구조는 초파리 몸 모형 NeuroMechFly v2(Wang-Chen 외 2024,
 * Nature Methods 21:2353)의 보행 제어기를 따랐고, 식을 보고 직접 구현했다.
 *
 * 걸음 생성기: 다리마다 진동자가 하나씩 있다. 진동자 i는 위상 θ와 크기 r을 가진다.
 *     dθ_i/dt = 2π ν_i + Σ_j r_j w sin(θ_j - θ_i - φ_ij)        (이웃한 다리끼리는 φ = π: 번갈아 딛는다)
 *     dr_i/dt = α (R_i - r_i)
 *   명령 δ가 그쪽 다리들의 R = |δ|(보폭의 배수)와 ν의 부호(δ가 음수면 거꾸로 돌아 뒤로 딛는다)를 정한다.
 *   서로 맞물리면 세 다리씩 번갈아 딛는 걸음이 된다(왼앞, 왼뒤, 오른가운데 / 왼가운데, 오른앞, 오른뒤).
 *   한 바퀴 가운데 앞부분이 발을 들어 앞으로 옮기는 동안이고(swing_end까지) 나머지가 딛고 있는 동안이다.
 *
 * 힘: 그 모형은 관절 수십 개의 3차원 물리를 계산하지만 여기서는 평면의 몸 하나로 줄였다.
 *   딛는 순간 발은 바닥에 고정되고, 다리는 "발이 있어야 할 자리"와 "발이 실제로 있는 자리"의 차이만큼 몸을 끄는
 *   용수철이다. 다리 하나가 낼 수 있는 힘(grip)을 넘으면 발이 미끄러진다. 몸에는 질량과 관성이 있어서
 *   밀리면 밀려났다가 돌아오고, 빠르기와 도는 빠르기는 따로 정하지 않아도 보폭과 좌우 차이에서 나온다.
 *
 * 좌표: 몸 기준으로 앞이 +x, 오른쪽이 +y, 위가 +z. 맵에서는 향한 쪽이 (cos ang, sin ang), 오른쪽이 (-sin ang, cos ang).
 * 단위는 칸과 초. 다리 번호는 왼쪽 앞, 가운데, 뒤, 오른쪽 앞, 가운데, 뒤 순서다. */
#ifndef SSP_BOSS_BODY_H
#define SSP_BOSS_BODY_H

#define BODY_LEGS 6
#define BODY_DT   0.001f             /* 한 번에 계산하는 시간(초). 한 틱(0.05초)을 50번에 나눠 계산한다 */

typedef struct {
    float step_hz;                   /* 걸음 빈도(초당 걸음 수) */
    float couple, converge;          /* 진동자 사이의 세기 w, 크기가 R을 따라가는 빠르기 α */
    float swing_end[3];              /* 발을 들고 있는 동안이 끝나는 위상(라디안). 앞, 가운데, 뒤 다리 */
    float sweep[3];                  /* 명령이 1일 때 딛고 있는 동안 발이 몸 뒤쪽으로 가는 거리(칸). 앞, 가운데, 뒤 */
    float lift;                      /* 발을 드는 높이(칸) */
    float foot_x[3], foot_y;         /* 서 있을 때 발의 자리(몸 기준). 앞, 가운데, 뒤의 x와 바깥쪽 y */
    float mass, inertia;
    float stiff, damp;               /* 다리 용수철의 세기와 감쇠 */
    float grip;                      /* 다리 하나가 낼 수 있는 가장 큰 힘. 넘으면 발이 미끄러진다 */
} body_cfg_t;

typedef struct {
    float phase[BODY_LEGS], mag[BODY_LEGS];      /* 진동자의 위상(라디안, 계속 늘거나 준다)과 크기 */
    int   down[BODY_LEGS];                       /* 딛고 있는가 */
    float ax[BODY_LEGS], ay[BODY_LEGS];          /* 딛고 있는 발의 자리(맵 좌표) */
    float ex[BODY_LEGS], ey[BODY_LEGS];          /* 지난번의 용수철 늘어남(감쇠를 계산하는 데 쓴다) */
    float x, y, ang, vx, vy, w;                  /* 몸의 자리, 향한 쪽, 빠르기(칸/초), 도는 빠르기(라디안/초) */
    int   slips;                                 /* 발이 미끄러진 횟수(기록) */
} body_t;

void body_init(body_t *b, const body_cfg_t *c, float x, float y, float ang);
/* 다른 것이 몸을 옮겼을 때(날아서 내려앉음, 돌진): 그 자리에 세우고 빠르기를 0으로, 딛고 있는 발을 다시 놓는다 */
void body_place(body_t *b, const body_cfg_t *c, float x, float y, float ang);
/* BODY_DT만큼 계산한다. left와 right가 뇌에서 내려온 명령이다. 벽은 모른다(부른 쪽이 자리를 바로잡는다) */
void body_step(body_t *b, const body_cfg_t *c, float left, float right);
void body_push(body_t *b, float vx, float vy);   /* 밀려난다: 빠르기에 더한다 */
/* 위상과 크기에서 발의 자리를 구한다(몸 기준. out[0] 앞, out[1] 오른쪽, out[2] 위). 그리는 쪽도 같이 쓴다 */
void body_foot(const body_cfg_t *c, int leg, float phase, float mag, float out[3]);
int  body_swinging(const body_cfg_t *c, int leg, float phase);   /* 그 위상에서 발을 들고 있는가 */

#endif
