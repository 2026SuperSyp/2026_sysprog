/* 스테이지 실행 파일의 시작점. 스테이지마다 실행 파일이 하나씩 만들어진다.
 *   build/dev/stage_<이름> --local [--seed=N] [--players=N] [--brain=fast|slow|real]
 *   build/dev/stage_<이름> --bot   [--seed=N] [--players=N] [--brain=fast|slow|real] [--draws=N] [--solo=I]
 *   build/dev/stage_<이름> --pipe=R,W [...]   서버가 띄울 때. fd R에서 읽고 fd W로 쓴다
 * --brain=real은 진짜 배선으로 계산한다. 먼저 make brain-data로 data/ 를 만들어 두어야 한다. */
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
    int bot = 0, solo = -1, pipe_r = -1, pipe_w = -1;

    for (int i = 1; i < argc; i++) {
        const char *a = argv[i];
        if (strcmp(a, "--bot") == 0)                 bot = 1;
        else if (strcmp(a, "--local") == 0)          bot = 0;
        else if (strncmp(a, "--seed=", 7) == 0)      o.seed = (uint32_t)strtoul(a + 7, NULL, 10);
        else if (strncmp(a, "--players=", 10) == 0)  o.n_players = atoi(a + 10);
        else if (strncmp(a, "--ticks=", 8) == 0)     o.n_ticks = atoi(a + 8);
        else if (strncmp(a, "--draws=", 8) == 0)     o.draws_per_tick = atoi(a + 8);
        else if (strncmp(a, "--solo=", 7) == 0)      solo = atoi(a + 7);
        else if (sscanf(a, "--pipe=%d,%d", &pipe_r, &pipe_w) == 2) { }
        else if (strcmp(a, "--brain=slow") == 0)     o.brain = BRAIN_FAKE_SLOW;
        else if (strcmp(a, "--brain=fast") == 0)     o.brain = BRAIN_FAKE_FAST;
        else if (strcmp(a, "--brain=real") == 0)     o.brain = BRAIN_REAL;
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
    if (o.brain == BRAIN_REAL && brain_host_open_real("data") != 0) {
        fprintf(stdout, "진짜 뇌의 자료가 없습니다. 먼저 make brain-data 를 실행하세요.\n");
        return 2;
    }

    if (pipe_r >= 0 && pipe_w >= 0) return host_pipe_main(&o, pipe_r, pipe_w);
    return bot ? host_bot_main(&o, solo) : host_local_main(&o, argc, argv);
}
