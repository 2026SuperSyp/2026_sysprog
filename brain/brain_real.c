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
#define WINDOW_MAX   4
#define OUT_MAX      8             /* 출력 집단 하나의 뉴런 수 상한 */

/* 감각: 자극 1.0일 때 뉴런 하나가 초당 몇 번 발화하는가 */
static struct { int group; const char *name; float full_hz; const uint32_t *idx; int n; } inputs[] = {
    { BR_LOOM_L, "BR_LOOM_L", 150.0f, NULL, 0 },
    { BR_LOOM_R, "BR_LOOM_R", 150.0f, NULL, 0 },
    { BR_SUGAR,  "BR_SUGAR",  160.0f, NULL, 0 },
    { BR_BITTER, "BR_BITTER", 160.0f, NULL, 0 },
    { BR_SALT,   "BR_SALT",   160.0f, NULL, 0 },
    { BR_WATER,  "BR_WATER",  160.0f, NULL, 0 },
};
#define N_INPUTS ((int)(sizeof inputs / sizeof inputs[0]))

/* 반응: 뉴런 하나가 초당 full_hz번 발화하면 1.0으로 본다. window는 평균을 내는 틱 수다.
 * 방향 전환과 후진은 뉴런이 몇 개뿐이라 들쭉날쭉해서 0.2초 동안 평균한다. */
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
    { BR_BACKWARD, "BR_BACKWARD",  20.0f, 4, NULL, 0, 0, {0}, {0} },
    { BR_FEED,     "BR_FEED",      70.0f, 2, NULL, 0, 0, {0}, {0} },
};
#define N_OUTPUTS ((int)(sizeof outputs / sizeof outputs[0]))

static lif_t *brain;
static uint64_t seen_total;
static int tick_no;

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
    return 0;
}

int brain_real_ready(void) { return brain != NULL; }

void brain_real_reset(uint32_t seed)
{
    if (!brain) return;
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

    /* 전진은 배선에서 확인하지 못해서 규칙으로 둔다: 다가오는 것이 없으면 걷는다. */
    float loom = stim[BR_LOOM_L] > stim[BR_LOOM_R] ? stim[BR_LOOM_L] : stim[BR_LOOM_R];
    rate[BR_FORWARD] = 1.0f - clamp01(loom);
    /* 각성: 이번 틱에 뇌 전체가 발화한 횟수 */
    uint64_t total = lif_total_spikes(brain);
    rate[BR_AROUSAL] = clamp01((float)(total - seen_total) / 2500.0f);
    seen_total = total;
    tick_no++;
}
