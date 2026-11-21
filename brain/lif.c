/* 초파리 뇌 시뮬레이터. 설명은 lif.h에 있다.
 *
 * 뉴런 하나의 모델 (Shiu 등 2024의 수식과 상수)
 *   막 전위 v:    dv/dt = (g - (v - 휴지 전위)) / 20ms
 *   시냅스 입력 g: dg/dt = -g / 5ms
 *   앞 뉴런이 발화하면 1.8ms 뒤에 g가 (시냅스 수 x 0.275mV)만큼 변한다. 억제성이면 줄어든다.
 *   v가 문턱(-45mV)을 넘으면 발화하고, 휴지 전위(-52mV)로 돌아가 2.2ms 동안 쉰다.
 *   외부 자극은 막 전위에 0.275mV x 250을 한 번에 더한다(사실상 그 뉴런을 발화시킨다).
 *
 * 빠르게 돌리는 방법
 *   - 연결표는 파일을 mmap으로 붙여서 그대로 읽는다.
 *   - 매 스텝 계산하는 것은 "곧 발화할 수 있는" 뉴런뿐이다. 입력이 더 오지 않으면 문턱에
 *     닿을 수 없는 뉴런은 재워 두고, 다음 입력이 왔을 때 그동안 지난 시간만큼 한 번에 진행시킨다.
 *     입력이 없는 동안의 변화는 식으로 정확히 구할 수 있어서 결과는 매 스텝 계산한 것과 같다.
 *   - 발화는 1.8ms 뒤에 전달되므로, 스텝별 발화 목록을 고리 모양으로 18칸 들고 있는다. */
#include "brain/lif.h"

#include <fcntl.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

#define DT_MS        0.1f
#define TAU_M_MS     20.0f
#define TAU_S_MS     5.0f
#define THRESHOLD    7.0f          /* 문턱 - 휴지 전위 = -45 - (-52) mV */
#define REFR_STEPS   22            /* 2.2ms */
#define DELAY_STEPS  18            /* 1.8ms */
#define W_SYN        0.275f        /* 시냅스 하나의 세기(mV) */
#define STIM_MV      (0.275f * 250.0f)
#define NAP_MAX      4096          /* 이보다 오래 잔 뉴런은 완전히 쉬는 상태로 본다(약 0.4초) */
#define PEAK_GAIN    0.1575f       /* 시냅스 입력 g가 앞으로 막 전위를 최대 얼마나 올릴 수 있는가(g의 배수) */

#define MAX_GROUPS   32
#define HEADER_BYTES 24

typedef struct { char name[24]; uint32_t *idx; int n; } group_t;

/* 뉴런 하나의 상태. u는 휴지 전위에서 잰 막 전위, g는 시냅스 입력이다(mV). */
typedef struct {
    float u, g;
    uint32_t at;                   /* 재운 뉴런: u, g가 몇 번째 스텝의 값인가 */
    uint16_t refr;                 /* 남은 불응 스텝 */
    uint8_t  awake;                /* 매 스텝 계산하는 목록에 올라 있는가 */
} cell_t;

struct lif {
    /* 연결표(파일에서 읽기 전용) */
    void *map; size_t map_len;
    uint32_t n, n_syn;
    const uint32_t *row;           /* 뉴런 i가 보내는 연결은 row[i] ~ row[i+1]-1 */
    const uint32_t *post;          /* 받는 뉴런 */
    const int16_t  *w;             /* 시냅스 수. 억제성이면 음수 */

    cell_t *cell;                  /* 뉴런의 상태 */
    uint32_t *count;               /* 누적 발화 횟수 */
    uint32_t *active; uint32_t n_active;       /* 매 스텝 계산하는 뉴런 목록 */
    float nap_u[NAP_MAX], nap_g[NAP_MAX];      /* k스텝 동안 u, g에 곱해지는 값 */

    /* 스텝별 발화 목록(고리) */
    uint32_t *fired[DELAY_STEPS]; uint32_t n_fired[DELAY_STEPS], cap_fired[DELAY_STEPS];

    /* 외부 자극 */
    uint32_t *stim_idx; float *stim_p; uint32_t n_stim, cap_stim;

    group_t groups[MAX_GROUPS]; int n_groups;
    float keep_u, keep_g, feed;    /* 한 스텝 동안의 변화 계수 */
    uint64_t step, rng, total;
};

static float rand01(lif_t *l)
{
    l->rng ^= l->rng >> 12;
    l->rng ^= l->rng << 25;
    l->rng ^= l->rng >> 27;
    return (float)((l->rng * 0x2545F4914F6CDD1DULL) >> 40) / 16777216.0f;
}

static int load_groups(lif_t *l, const char *path)
{
    FILE *f = fopen(path, "r");
    if (!f) { fprintf(stderr, "뇌: %s 를 열 수 없습니다\n", path); return -1; }
    char name[24];
    int n;
    while (l->n_groups < MAX_GROUPS && fscanf(f, "%23s %d", name, &n) == 2) {
        group_t *gr = &l->groups[l->n_groups];
        if (n < 0 || n > 100000) break;
        snprintf(gr->name, sizeof gr->name, "%s", name);
        gr->idx = malloc(sizeof(uint32_t) * (size_t)(n > 0 ? n : 1));
        gr->n = 0;
        for (int i = 0; i < n; i++) {
            unsigned v;
            if (fscanf(f, "%u", &v) != 1) break;
            if (v < l->n) gr->idx[gr->n++] = v;
        }
        l->n_groups++;
    }
    fclose(f);
    return 0;
}

lif_t *lif_open(const char *bin_path, const char *groups_path)
{
    int fd = open(bin_path, O_RDONLY);
    if (fd < 0) { fprintf(stderr, "뇌: %s 를 열 수 없습니다 (sh tools/brain_data.sh 로 만듭니다)\n", bin_path); return NULL; }
    struct stat st;
    if (fstat(fd, &st) != 0 || st.st_size < HEADER_BYTES) { close(fd); return NULL; }
    /* MAP_POPULATE: 처음 쓸 때 멈칫하지 않도록 파일을 미리 다 읽어 둔다 */
    void *map = mmap(NULL, (size_t)st.st_size, PROT_READ, MAP_PRIVATE | MAP_POPULATE, fd, 0);
    close(fd);
    if (map == MAP_FAILED) { fprintf(stderr, "뇌: mmap 실패\n"); return NULL; }

    uint32_t head[4];
    memcpy(head, (const char *)map + 8, sizeof head);
    uint32_t n = head[1], n_syn = head[2];
    size_t need = HEADER_BYTES + 4 * ((size_t)n + 1) + 4 * (size_t)n_syn + 2 * (size_t)n_syn;
    if (memcmp(map, "SSPBRAIN", 8) != 0 || head[0] != 1 || need > (size_t)st.st_size) {
        fprintf(stderr, "뇌: %s 의 형식이 맞지 않습니다\n", bin_path);
        munmap(map, (size_t)st.st_size);
        return NULL;
    }

    lif_t *l = calloc(1, sizeof *l);
    l->map = map; l->map_len = (size_t)st.st_size;
    l->n = n; l->n_syn = n_syn;
    l->row  = (const uint32_t *)((const char *)map + HEADER_BYTES);
    l->post = l->row + n + 1;
    l->w    = (const int16_t *)(l->post + n_syn);
    l->cell = calloc(n, sizeof(cell_t));
    l->count = calloc(n, sizeof(uint32_t));
    l->active = malloc(sizeof(uint32_t) * n);

    /* 한 스텝(0.1ms) 뒤의 값: u' = u*keep_u + g*feed,  g' = g*keep_g  (미분방정식의 정확한 해) */
    l->keep_u = expf(-DT_MS / TAU_M_MS);
    l->keep_g = expf(-DT_MS / TAU_S_MS);
    l->feed   = TAU_S_MS / (TAU_S_MS - TAU_M_MS) * (l->keep_g - l->keep_u);
    for (int k = 0; k < NAP_MAX; k++) {
        l->nap_u[k] = expf(-DT_MS * (float)k / TAU_M_MS);
        l->nap_g[k] = expf(-DT_MS * (float)k / TAU_S_MS);
    }

    if (load_groups(l, groups_path) != 0) { lif_close(l); return NULL; }
    lif_reset(l, 1);
    return l;
}

void lif_close(lif_t *l)
{
    if (!l) return;
    munmap(l->map, l->map_len);
    free(l->cell); free(l->count); free(l->active);
    for (int i = 0; i < DELAY_STEPS; i++) free(l->fired[i]);
    for (int i = 0; i < l->n_groups; i++) free(l->groups[i].idx);
    free(l->stim_idx); free(l->stim_p);
    free(l);
}

void lif_reset(lif_t *l, uint64_t seed)
{
    memset(l->cell, 0, sizeof(cell_t) * l->n);
    memset(l->count, 0, sizeof(uint32_t) * l->n);
    memset(l->n_fired, 0, sizeof l->n_fired);
    l->n_active = 0;
    l->step = 0;
    l->total = 0;
    l->rng = (seed << 1) | 1ULL;
    for (int i = 0; i < 8; i++) rand01(l);
}

int lif_group(const lif_t *l, const char *name, const uint32_t **idx)
{
    for (int i = 0; i < l->n_groups; i++) {
        if (strcmp(l->groups[i].name, name) == 0) {
            if (idx) *idx = l->groups[i].idx;
            return l->groups[i].n;
        }
    }
    return 0;
}

void lif_stim_clear(lif_t *l) { l->n_stim = 0; }

void lif_stim_add(lif_t *l, const uint32_t *idx, int n, float hz)
{
    if (hz <= 0.0f || n <= 0) return;
    if (l->n_stim + (uint32_t)n > l->cap_stim) {
        l->cap_stim = (l->n_stim + (uint32_t)n) * 2;
        l->stim_idx = realloc(l->stim_idx, sizeof(uint32_t) * l->cap_stim);
        l->stim_p = realloc(l->stim_p, sizeof(float) * l->cap_stim);
    }
    for (int i = 0; i < n; i++) {
        l->stim_idx[l->n_stim] = idx[i];
        l->stim_p[l->n_stim++] = hz * DT_MS / 1000.0f;       /* 한 스텝에 발화할 확률 */
    }
}

/* 입력이 더 오지 않아도 문턱에 닿을 수 있는가. 닿을 수 없으면 재워도 된다.
 * 입력이 없을 때 u는 max(u, 0) + PEAK_GAIN * g 를 넘지 못한다(g가 양수일 때). */
static inline int may_fire(float u, float g)
{
    float top = u > 0.0f ? u : 0.0f;
    if (g > 0.0f) top += PEAK_GAIN * g;
    return top >= THRESHOLD - 0.01f;
}

/* 재워 둔 뉴런을 지금 스텝까지 한 번에 진행시킨다. */
static inline void catch_up(const lif_t *l, cell_t *c, uint32_t now)
{
    uint32_t k = now - c->at;
    if (k == 0) return;
    if (k >= NAP_MAX) {
        c->u = 0.0f;
        c->g = 0.0f;
    } else {
        /* k스텝 뒤: u' = u*a + g*(ts/(ts-tm))*(b - a),  g' = g*b   (a, b는 k스텝 동안의 감쇠) */
        float a = l->nap_u[k], b = l->nap_g[k];
        c->u = c->u * a + c->g * (TAU_S_MS / (TAU_S_MS - TAU_M_MS)) * (b - a);
        c->g = c->g * b;
    }
    c->at = now;
}

static inline void wake_if_needed(lif_t *l, cell_t *c, uint32_t i)
{
    if (!c->awake && may_fire(c->u, c->g)) {
        c->awake = 1;
        l->active[l->n_active++] = i;
    }
}

static void step_once(lif_t *l)
{
    int slot = (int)(l->step % DELAY_STEPS);
    uint32_t now = (uint32_t)l->step;
    cell_t *restrict cell = l->cell;

    /* 1. 1.8ms 전에 발화한 뉴런들의 신호가 지금 도착한다 */
    const uint32_t *src = l->fired[slot];
    for (uint32_t k = 0; k < l->n_fired[slot]; k++) {
        uint32_t end = l->row[src[k] + 1];
        for (uint32_t s = l->row[src[k]]; s < end; s++) {
            uint32_t p = l->post[s];
            cell_t *c = &cell[p];
            if (!c->awake) catch_up(l, c, now);
            c->g += W_SYN * (float)l->w[s];
            wake_if_needed(l, c, p);
        }
    }
    l->n_fired[slot] = 0;          /* 이 칸은 이번 스텝의 발화를 담는 데 다시 쓴다 */

    /* 2. 외부 자극 */
    for (uint32_t k = 0; k < l->n_stim; k++) {
        if (rand01(l) < l->stim_p[k]) {
            cell_t *c = &cell[l->stim_idx[k]];
            if (!c->awake) catch_up(l, c, now);
            c->u += STIM_MV;
            wake_if_needed(l, c, l->stim_idx[k]);
        }
    }

    /* 3. 깨어 있는 뉴런만 한 스텝 진행한다. 문턱에 닿을 수 없게 된 뉴런은 재운다 */
    uint32_t keep = 0;
    for (uint32_t a = 0; a < l->n_active; a++) {
        uint32_t i = l->active[a];
        cell_t *c = &cell[i];
        if (c->refr) {                             /* 불응기: 값이 멈춰 있다 */
            c->refr--;
            l->active[keep++] = i;
            continue;
        }
        float nu = c->u * l->keep_u + c->g * l->feed;
        float ng = c->g * l->keep_g;
        if (nu > THRESHOLD) {                      /* 발화 */
            c->u = 0.0f;
            c->g = 0.0f;
            c->refr = REFR_STEPS;
            l->count[i]++;
            l->total++;
            if (l->n_fired[slot] == l->cap_fired[slot]) {
                l->cap_fired[slot] = l->cap_fired[slot] ? l->cap_fired[slot] * 2 : 256;
                l->fired[slot] = realloc(l->fired[slot], sizeof(uint32_t) * l->cap_fired[slot]);
            }
            l->fired[slot][l->n_fired[slot]++] = i;
            l->active[keep++] = i;
            continue;
        }
        c->u = nu;
        c->g = ng;
        if (may_fire(nu, ng)) {
            l->active[keep++] = i;
        } else {
            c->awake = 0;
            c->at = now + 1;
        }
    }
    l->n_active = keep;
    l->step++;
}

void lif_run(lif_t *l, int steps)
{
    for (int s = 0; s < steps; s++) step_once(l);
}

uint32_t lif_neurons(const lif_t *l)                 { return l->n; }
uint32_t lif_count(const lif_t *l, uint32_t idx)     { return idx < l->n ? l->count[idx] : 0; }
uint64_t lif_total_spikes(const lif_t *l)            { return l->total; }
uint32_t lif_active(const lif_t *l)                  { return l->n_active; }
