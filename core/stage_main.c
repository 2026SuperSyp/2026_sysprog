/* 스테이지 실행 파일의 시작점. 스테이지마다 실행 파일이 하나씩 만들어진다.
 *   build/dev/stage_<이름> --local [--seed=N] [--players=N] [--brain=fast|slow]
 *   build/dev/stage_<이름> --bot   [--seed=N] [--players=N] [--brain=fast|slow] [--draws=N] [--solo=I] */
#include "core/host.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int main(int argc, char **argv)
{
    setvbuf(stdout, NULL, _IOLBF, 0);

    /* 실행 파일 이름이 stage_<이름>이므로 스테이지 폴더는 stages/<이름>이다. */
    static char dir[256];
    const char *base = strrchr(argv[0], '/');
    base = base ? base + 1 : argv[0];
    snprintf(dir, sizeof dir, "stages/%s", strncmp(base, "stage_", 6) == 0 ? base + 6 : base);

    host_opts_t o = { .stage_dir = dir, .seed = 1, .n_players = 3, .n_ticks = 400,
                      .brain = BRAIN_FAKE_FAST, .draws_per_tick = 1 };
    int bot = 0, solo = -1;

    for (int i = 1; i < argc; i++) {
        const char *a = argv[i];
        if (strcmp(a, "--bot") == 0)                 bot = 1;
        else if (strcmp(a, "--local") == 0)          bot = 0;
        else if (strncmp(a, "--seed=", 7) == 0)      o.seed = (uint32_t)strtoul(a + 7, NULL, 10);
        else if (strncmp(a, "--players=", 10) == 0)  o.n_players = atoi(a + 10);
        else if (strncmp(a, "--ticks=", 8) == 0)     o.n_ticks = atoi(a + 8);
        else if (strncmp(a, "--draws=", 8) == 0)     o.draws_per_tick = atoi(a + 8);
        else if (strncmp(a, "--solo=", 7) == 0)      solo = atoi(a + 7);
        else if (strcmp(a, "--brain=slow") == 0)     o.brain = BRAIN_FAKE_SLOW;
        else if (strcmp(a, "--brain=fast") == 0)     o.brain = BRAIN_FAKE_FAST;
        else {
            fprintf(stdout, "모르는 옵션: %s\n", a);
            return 2;
        }
    }
    if (o.n_players < 1) o.n_players = 1;
    if (o.n_players > 3) o.n_players = 3;
    if (o.n_ticks < 1) o.n_ticks = 1;
    if (o.draws_per_tick < 0) o.draws_per_tick = 0;
    if (solo >= 0) o.n_players = 1;

    return bot ? host_bot_main(&o, solo) : host_local_main(&o, argc, argv);
}
