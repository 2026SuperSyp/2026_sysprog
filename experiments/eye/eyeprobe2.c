/* 실험 도구 2: 틱(50ms)마다 바뀌는 뉴런별 자극을 넣고, 지켜볼 집단의 틱별 발화 수를 찍는다.
 *   eyeprobe2 <data 폴더> <일정 파일> <집단 파일> <씨앗>
 * 일정 파일: "T <n>" 한 줄 뒤에 "뉴런번호 초당발화수" n줄. 틱 수만큼 되풀이.
 * 집단 파일: 한 줄에 "이름 개수 번호 ...". 출력: 틱마다 "전체발화 계산ms 집단1 집단2 ..."(발화 수 합) */
#include "brain/lif.h"
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
static double now_ms(void) { struct timespec t; clock_gettime(CLOCK_MONOTONIC, &t); return t.tv_sec * 1e3 + t.tv_nsec / 1e6; }
int main(int argc, char **argv)
{
    if (argc < 5) return 2;
    char bin[512], grp[512];
    snprintf(bin, sizeof bin, "%s/brain.bin", argv[1]); snprintf(grp, sizeof grp, "%s/brain_groups.txt", argv[1]);
    lif_t *l = lif_open(bin, grp); if (!l) return 1;
    lif_reset(l, (uint64_t)atoll(argv[4]));
    FILE *g = fopen(argv[3], "r"); if (!g) return 1;
    static uint32_t *gi[64]; static int gn[64]; int ng = 0; char name[64]; int n;
    while (ng < 64 && fscanf(g, "%63s %d", name, &n) == 2) {
        gi[ng] = malloc(sizeof(uint32_t) * (n > 0 ? n : 1)); gn[ng] = n;
        for (int i = 0; i < n; i++) if (fscanf(g, "%u", &gi[ng][i]) != 1) return 1;
        ng++;
    }
    fclose(g);
    uint32_t nn = lif_neurons(l); uint32_t *prev = calloc(nn, sizeof(uint32_t));
    FILE *f = fopen(argv[2], "r"); if (!f) return 1;
    char tag[8]; uint64_t ptot = 0;
    while (fscanf(f, "%7s %d", tag, &n) == 2) {
        lif_stim_clear(l);
        for (int i = 0; i < n; i++) { unsigned idx; float hz; if (fscanf(f, "%u %f", &idx, &hz) != 2) return 1; uint32_t k = idx; lif_stim_add(l, &k, 1, hz); }
        double t0 = now_ms(); lif_run(l, 50 * LIF_STEPS_PER_MS); double dt = now_ms() - t0;
        printf("%llu %.1f", (unsigned long long)(lif_total_spikes(l) - ptot), dt); ptot = lif_total_spikes(l);
        for (int k = 0; k < ng; k++) {
            unsigned s = 0;
            for (int i = 0; i < gn[k]; i++) { uint32_t c = lif_count(l, gi[k][i]); s += c - prev[gi[k][i]]; }
            printf(" %u", s);
        }
        for (int k = 0; k < ng; k++) for (int i = 0; i < gn[k]; i++) prev[gi[k][i]] = lif_count(l, gi[k][i]);
        printf("\n");
    }
    return 0;
}
