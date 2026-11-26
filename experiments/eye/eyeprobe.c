/* 실험 도구: 뉴런별 자극 빈도 파일을 받아 진짜 뇌를 돌리고, 뉴런별 발화 수를 파일로 낸다.
 *   eyeprobe <data 폴더> <자극 파일> <ms> <씨앗> <결과 파일>
 * 자극 파일: 한 줄에 "뉴런번호 초당발화수". 결과 파일: uint32 x 뉴런 수(누적 발화 수).
 * 표준 출력: 전체 발화 수, 틱(50ms)당 최대 발화 수, 틱당 평균/최대 계산 시간(ms) */
#include "brain/lif.h"
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
static double now_ms(void) { struct timespec t; clock_gettime(CLOCK_MONOTONIC, &t); return t.tv_sec * 1e3 + t.tv_nsec / 1e6; }
int main(int argc, char **argv)
{
    if (argc < 6) { fprintf(stderr, "usage\n"); return 2; }
    char bin[512], grp[512];
    snprintf(bin, sizeof bin, "%s/brain.bin", argv[1]);
    snprintf(grp, sizeof grp, "%s/brain_groups.txt", argv[1]);
    lif_t *l = lif_open(bin, grp);
    if (!l) return 1;
    int ms = atoi(argv[3]);
    lif_reset(l, (uint64_t)atoll(argv[4]));
    lif_stim_clear(l);
    FILE *f = fopen(argv[2], "r");
    if (!f) { perror(argv[2]); return 1; }
    unsigned idx; float hz; int n = 0;
    while (fscanf(f, "%u %f", &idx, &hz) == 2) { uint32_t i = idx; lif_stim_add(l, &i, 1, hz); n++; }
    fclose(f);
    int ticks = ms / 50;
    double tot = 0, worst = 0; uint64_t prev = 0, peak = 0;
    for (int t = 0; t < ticks; t++) {
        double t0 = now_ms();
        lif_run(l, 50 * LIF_STEPS_PER_MS);
        double dt = now_ms() - t0; tot += dt; if (dt > worst) worst = dt;
        uint64_t s = lif_total_spikes(l) - prev; prev = lif_total_spikes(l); if (s > peak) peak = s;
    }
    uint32_t nn = lif_neurons(l);
    uint32_t *c = malloc(sizeof(uint32_t) * nn);
    for (uint32_t i = 0; i < nn; i++) c[i] = lif_count(l, i);
    FILE *o = fopen(argv[5], "wb"); fwrite(c, sizeof(uint32_t), nn, o); fclose(o);
    printf("stim=%d total=%llu peak_tick=%llu ms_avg=%.2f ms_max=%.2f\n", n, (unsigned long long)lif_total_spikes(l),
           (unsigned long long)peak, tot / ticks, worst);
    return 0;
}
