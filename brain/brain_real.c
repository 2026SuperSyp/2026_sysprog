/* 진짜 뇌를 게임 틱에 맞춰 돌리는 부분.
 *
 * 한 틱(50ms)마다:
 *   1. 스테이지가 준 자극(0~1)을 감각 뉴런의 발화 빈도로 바꿔 넣는다.
 *   2. 뇌를 50ms만큼(500스텝) 진행한다.
 *   3. 출력 뉴런이 발화한 횟수를 세어 0~1의 반응으로 바꾼다.
 * 어떤 뉴런이 어느 집단인지는 data/brain_groups.txt에 있다(tools/brain_convert.py가 만든다). */
#include "brain/brain_real.h"
#include "brain/lif.h"
#include "brain.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define TICK_STEPS   (50 * LIF_STEPS_PER_MS)
#define TICK_SEC     0.05f
#define WINDOW_MAX   8
#define RUNAWAY_SPIKES 20000     /* 한 틱의 발화가 이보다 많으면 폭주로 본다(정상 최대는 약 6,500) */
#define OUT_MAX      8             /* 출력 집단 하나의 뉴런 수 상한 */

/* 감각: 자극 1.0일 때 뉴런 하나가 초당 몇 번 발화하는가.
 * 짠맛(LB1e)은 60을 넘기지 않는다. 90 이상에서는 뇌 전체가 폭주해서 자극을 꺼도 멈추지 않는다
 * (70까지는 60초씩 8번 돌려도 조용했다). */
static struct { int group; const char *name; float full_hz; const uint32_t *idx; int n; } inputs[] = {
    { BR_LOOM_L, "BR_LOOM_L", 150.0f, NULL, 0 },
    { BR_LOOM_R, "BR_LOOM_R", 150.0f, NULL, 0 },
    { BR_SUGAR,  "BR_SUGAR",  160.0f, NULL, 0 },
    { BR_BITTER, "BR_BITTER", 160.0f, NULL, 0 },
    { BR_SALT,   "BR_SALT",    60.0f, NULL, 0 },
    { BR_WATER,  "BR_WATER",  160.0f, NULL, 0 },
    { BR_OBJECT_L, "BR_OBJECT_L", 150.0f, NULL, 0 },       /* 물체를 보는 시각 뉴런(LC9) */
    { BR_OBJECT_R, "BR_OBJECT_R", 150.0f, NULL, 0 },
    { BR_SURFACE_L, "BR_SURFACE_L", 150.0f, NULL, 0 },     /* 날아가는 쪽에 다가오는 면(LPLC4) */
    { BR_SURFACE_R, "BR_SURFACE_R", 150.0f, NULL, 0 },
};
#define N_INPUTS ((int)(sizeof inputs / sizeof inputs[0]))

/* 반응: 뉴런 하나가 초당 full_hz번 발화하면 1.0으로 본다. window는 평균을 내는 틱 수다.
 * 방향 전환, 먹기는 뉴런이 몇 개뿐이라 들쭉날쭉해서 0.2초 동안 평균한다.
 * 후진은 반응이 약해서(초당 8~24번) 0.4초 동안 평균하고 기준도 낮게 잡는다. */
static struct {
    int group; const char *name; float full_hz; int window;
    const uint32_t *idx; int n;
    uint32_t seen;                       /* 지난 틱까지 센 발화 수 */
    uint32_t each[OUT_MAX];              /* 뉴런별로 지난 틱까지 센 발화 수 */
    uint32_t recent[WINDOW_MAX];         /* 최근 틱들의 발화 수 */
} outputs[] = {
    { BR_ESCAPE,   "BR_ESCAPE",   150.0f, 2, NULL, 0, 0, {0}, {0} },
    { BR_TURN_L,   "BR_TURN_L",    15.0f, 4, NULL, 0, 0, {0}, {0} },
    { BR_TURN_R,   "BR_TURN_R",    15.0f, 4, NULL, 0, 0, {0}, {0} },
    { BR_BACKWARD, "BR_BACKWARD",  10.0f, 8, NULL, 0, 0, {0}, {0} },
    { BR_FEED,     "BR_FEED",      70.0f, 4, NULL, 0, 0, {0}, {0} },
    /* 몸을 움직이는 명령 뉴런. 물체 자극 150에서 걷기 명령(DNp09)이 초당 45번쯤, 방향 전환(DNa02)이 25~60번 발화한다 */
    { BR_WALK_L,   "BR_WALK_L",    45.0f, 4, NULL, 0, 0, {0}, {0} },
    { BR_WALK_R,   "BR_WALK_R",    45.0f, 4, NULL, 0, 0, {0}, {0} },
    { BR_STEER_L,  "BR_STEER_L",   40.0f, 4, NULL, 0, 0, {0}, {0} },
    { BR_STEER_R,  "BR_STEER_R",   40.0f, 4, NULL, 0, 0, {0}, {0} },
    /* 떠 있는 동안 쓰는 명령 뉴런. 한쪽 다가옴 자극 150에서 반대쪽 DNp06이 초당 100번쯤, 양쪽 면 자극 150에서
     * 착지 뉴런(DNp07)이 초당 280번쯤 발화한다. 면 자극 30(0.2)이면 150번쯤이라 반응이 0.5를 넘는다 */
    { BR_AIRTURN_L, "BR_AIRTURN_L", 100.0f, 2, NULL, 0, 0, {0}, {0} },
    { BR_AIRTURN_R, "BR_AIRTURN_R", 100.0f, 2, NULL, 0, 0, {0}, {0} },
    { BR_LAND,     "BR_LAND",     280.0f, 2, NULL, 0, 0, {0}, {0} },
};
#define N_OUTPUTS ((int)(sizeof outputs / sizeof outputs[0]))

static lif_t *brain;
static uint64_t seen_total;
static int tick_no;
static uint32_t last_seed;

/* 뇌 지도: 뉴런마다 어느 칸에 있는지와 칸별 값 */
#define MAP_CELLS (BRAIN_MAP_W * BRAIN_MAP_H)
#define NO_CELL   0xFFFFu
static uint16_t *cell_of;                 /* 뉴런 번호 -> 칸. 위치를 모르면 NO_CELL */
static uint16_t map_density[MAP_CELLS];
static float    map_heat[MAP_CELLS];
static uint8_t  map_mark[MAP_CELLS];
static const brain_map_t map = { BRAIN_MAP_W, BRAIN_MAP_H, map_density, map_heat, map_mark };

/* 뉴런 위치 파일을 읽어 칸을 정한다. 파일의 가로세로는 같은 비율(가로 0~255)이다. */
static void load_map(const char *dir)
{
    char path[512];
    snprintf(path, sizeof path, "%s/brain_map.bin", dir);
    FILE *f = fopen(path, "rb");
    if (!f) return;
    uint32_t n = lif_neurons(brain);
    uint8_t *pos = malloc((size_t)n * 2);
    if (fread(pos, 2, n, f) == n) {
        cell_of = malloc(sizeof(uint16_t) * n);
        memset(map_density, 0, sizeof map_density);
        for (uint32_t i = 0; i < n; i++) {
            int x = pos[2 * i] * (BRAIN_MAP_W - 1) / 255, y = pos[2 * i + 1] * (BRAIN_MAP_W - 1) / 255;
            if (pos[2 * i + 1] == 255 || y >= BRAIN_MAP_H) { cell_of[i] = NO_CELL; continue; }
            cell_of[i] = (uint16_t)(y * BRAIN_MAP_W + x);
            map_density[cell_of[i]]++;
        }
    }
    free(pos);
    fclose(f);
}

const brain_map_t *brain_real_map(void) { return cell_of ? &map : NULL; }

static float clamp01(float v) { return v < 0.0f ? 0.0f : (v > 1.0f ? 1.0f : v); }

int brain_real_open(const char *dir)
{
    char bin[512], groups[512];
    snprintf(bin, sizeof bin, "%s/brain.bin", dir);
    snprintf(groups, sizeof groups, "%s/brain_groups.txt", dir);
    brain = lif_open(bin, groups);
    if (!brain) return -1;
    for (int i = 0; i < N_INPUTS; i++)  inputs[i].n  = lif_group(brain, inputs[i].name,  &inputs[i].idx);
    for (int i = 0; i < N_OUTPUTS; i++) {
        outputs[i].n = lif_group(brain, outputs[i].name, &outputs[i].idx);
        if (outputs[i].n > OUT_MAX) outputs[i].n = OUT_MAX;
    }
    load_map(dir);
    /* 집단이 없으면 그 자극은 들어가지 않고 그 반응은 늘 0이다. 자료를 만든 뒤에 게임이 쓰는 집단이 늘었으면
     * 집단 목록을 다시 만들어야 한다. 무엇을 치면 되는지까지 알려 준다 */
    int missing = 0;
    const char *first = NULL;
    for (int i = 0; i < N_INPUTS; i++)  if (inputs[i].n == 0)  { missing++; if (!first) first = inputs[i].name; }
    for (int i = 0; i < N_OUTPUTS; i++) if (outputs[i].n == 0) { missing++; if (!first) first = outputs[i].name; }
    if (missing)
        fprintf(stderr, "\n뇌 자료가 옛것입니다: %s/brain_groups.txt 에 뉴런 집단 %d개가 없습니다(%s 등).\n"
                        "  그 집단을 쓰는 반응은 늘 0입니다. 보스전의 초파리가 걷지 않습니다.\n"
                        "  고치는 법: 서버를 끄고, 저장소 맨 위 폴더에서  make brain-groups  를 돌린 뒤(몇 초) 다시 켭니다.\n\n",
                dir, missing, first);
    return 0;
}

int brain_real_ready(void) { return brain != NULL; }

void brain_real_reset(uint32_t seed)
{
    if (!brain) return;
    last_seed = seed;
    lif_reset(brain, seed);
    for (int i = 0; i < N_OUTPUTS; i++) {
        outputs[i].seen = 0;
        memset(outputs[i].each, 0, sizeof outputs[i].each);
        memset(outputs[i].recent, 0, sizeof outputs[i].recent);
    }
    seen_total = 0;
    tick_no = 0;
    memset(map_heat, 0, sizeof map_heat);
    memset(map_mark, 0, sizeof map_mark);
}

void brain_real_tick(const float *stim, float *rate)
{
    if (!brain) return;
    lif_stim_clear(brain);
    for (int i = 0; i < N_INPUTS; i++)
        lif_stim_add(brain, inputs[i].idx, inputs[i].n, stim[inputs[i].group] * inputs[i].full_hz);

    lif_run(brain, TICK_STEPS);

    /* 폭주 막기. 이 뇌는 어떤 자극에서는 스스로 계속 발화하는 상태에 빠지고, 자극을 꺼도 멈추지 않는다.
     * 그러면 한 틱 계산이 100ms를 넘는다. 그런 틱이 나오면 뇌를 처음 상태로 되돌리고 이번 틱은 조용한 것으로 한다. */
    if (lif_total_spikes(brain) - seen_total > RUNAWAY_SPIKES) {
        fprintf(stderr, "경고: 뇌가 폭주해서(한 틱에 %llu번 발화) 처음 상태로 되돌립니다\n",
                (unsigned long long)(lif_total_spikes(brain) - seen_total));
        brain_real_reset(last_seed + 1);
        memset(rate, 0, sizeof(float) * BRAIN_GROUP_COUNT);
        rate[BR_FORWARD] = 1.0f;
        return;
    }

    /* 뇌 지도: 방금 발화한 뉴런의 칸을 데운다 */
    if (cell_of) {
        const uint32_t *fired;
        uint32_t n_fired = lif_fired(brain, &fired);
        for (int c = 0; c < MAP_CELLS; c++) {
            map_heat[c] *= 0.5f;
            if (map_mark[c]) map_mark[c]--;
        }
        for (uint32_t k = 0; k < n_fired; k++)
            if (cell_of[fired[k]] != NO_CELL) map_heat[cell_of[fired[k]]] += 1.0f;
    }

    memset(rate, 0, sizeof(float) * BRAIN_GROUP_COUNT);
    for (int i = 0; i < N_OUTPUTS; i++) {
        uint32_t now = 0, sum = 0;
        for (int k = 0; k < outputs[i].n; k++) {
            uint32_t c = lif_count(brain, outputs[i].idx[k]);
            now += c;
            /* 출력 뉴런이 이번 틱에 발화했으면 지도에 표시한다 */
            if (cell_of && c > outputs[i].each[k] && cell_of[outputs[i].idx[k]] != NO_CELL)
                map_mark[cell_of[outputs[i].idx[k]]] = 3;
            outputs[i].each[k] = c;
        }
        outputs[i].recent[tick_no % WINDOW_MAX] = now - outputs[i].seen;
        outputs[i].seen = now;
        for (int k = 0; k < outputs[i].window; k++)
            sum += outputs[i].recent[(tick_no + WINDOW_MAX - k) % WINDOW_MAX];
        float hz = outputs[i].n > 0 ? (float)sum / (float)outputs[i].n / (TICK_SEC * (float)outputs[i].window) : 0.0f;
        rate[outputs[i].group] = clamp01(hz / outputs[i].full_hz);
    }

    /* BR_FORWARD(스테이지용)는 규칙으로 둔다: 다가오는 것이 없으면 걷는다. 보스전은 이것 대신 걷기 명령 뉴런
     * (BR_WALK_L/R)을 쓴다. */
    float loom = stim[BR_LOOM_L] > stim[BR_LOOM_R] ? stim[BR_LOOM_L] : stim[BR_LOOM_R];
    rate[BR_FORWARD] = 1.0f - clamp01(loom);
    /* 각성: 이번 틱에 뇌 전체가 발화한 횟수 */
    uint64_t total = lif_total_spikes(brain);
    rate[BR_AROUSAL] = clamp01((float)(total - seen_total) / 2500.0f);
    seen_total = total;
    tick_no++;
}
