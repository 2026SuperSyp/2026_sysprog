/* 뇌 프로세스. 서버가 띄운다.
 *   build/dev/brain_proc --shm=<이름> --model=fast|slow|real [--data=data] [--own]
 *
 * 이미 있는 공유 메모리에 붙어서 아래를 되풀이한다(약속은 brain_shm.h).
 *   1. 게임 쪽 칸을 읽는다. 판 번호가 바뀌었으면 그 씨앗으로 상태를 처음으로 되돌린다.
 *   2. 틱 번호가 올랐으면 그 자극으로 한 틱(50ms어치)을 계산하고 반응을 내놓는다.
 *   3. 할 일이 없으면 깨우는 신호를 잠깐(20ms) 기다린다.
 * 한 바퀴 돌 때마다 심장 박동 수를 올린다. 쉬고 있어도 20ms마다 오른다. 서버의 감시 스레드가 이 수를 본다.
 * 계산이 느려서 틱 번호가 둘 이상 올라 있으면 가장 새 자극으로 한 틱만 계산한다(따라잡으려 하지 않는다).
 * 진짜 모델은 배선 파일을 다 읽은 뒤에 공유 메모리에 붙는다. 그래서 첫 심장 박동은 준비가 끝났다는 뜻이다.
 *
 * --own은 서버 없이 시험할 때 쓴다. 공유 메모리를 직접 만들고, SIGTERM이나 SIGINT로 끝날 때 지운다. */
#include "brain/brain_model.h"
#include "brain/brain_shm.h"
#include "brain.h"

#include <signal.h>
#include <stdio.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>

#define IDLE_MS 20

static volatile sig_atomic_t quit;
static void on_quit(int sig) { (void)sig; quit = 1; }

static brain_shm_brain_t out;            /* 내가 쓰는 뇌 쪽 칸 */

static void publish(brain_shm_t *m, const float *rate)
{
    const brain_map_t *map = brain_model_map();
    memcpy(out.rate, rate, sizeof out.rate);
    out.has_map = map != NULL;
    if (map) {
        memcpy(out.density, map->density, sizeof out.density);
        memcpy(out.heat, map->heat, sizeof out.heat);
        memcpy(out.mark, map->mark, sizeof out.mark);
    }
    brain_shm_put_brain(m, &out);
    brain_shm_post(&m->done);
}

int main(int argc, char **argv)
{
    const char *name = NULL, *model_name = "fast", *data = "data";
    brain_mode_t model = BRAIN_FAKE_FAST;
    int own = 0;

    for (int i = 1; i < argc; i++) {
        const char *a = argv[i];
        if (strncmp(a, "--shm=", 6) == 0)         name = a + 6;
        else if (strncmp(a, "--model=", 8) == 0)  model_name = a + 8;
        else if (strncmp(a, "--data=", 7) == 0)   data = a + 7;
        else if (strcmp(a, "--own") == 0)         own = 1;
        else { fprintf(stderr, "뇌: 모르는 옵션: %s\n", a); return 2; }
    }
    if (strcmp(model_name, "fast") == 0)      model = BRAIN_FAKE_FAST;
    else if (strcmp(model_name, "slow") == 0) model = BRAIN_FAKE_SLOW;
    else if (strcmp(model_name, "real") == 0) model = BRAIN_REAL;
    else { fprintf(stderr, "뇌: 모르는 모델: %s (fast, slow, real 중 하나)\n", model_name); return 2; }
    if (!name) { fprintf(stderr, "뇌: --shm=<이름> 이 필요합니다\n"); return 2; }

    if (model == BRAIN_REAL && brain_model_open_real(data) != 0) {
        fprintf(stderr, "뇌: 진짜 뇌의 자료가 없습니다. 먼저 make brain-data 를 실행하세요\n");
        return 2;
    }

    brain_shm_t *m = own ? brain_shm_create(name) : brain_shm_attach(name);
    if (!m) { fprintf(stderr, "뇌: 공유 메모리 %s 을(를) 열 수 없습니다\n", name); return 1; }

    /* SA_RESTART를 주지 않는다. 기다리던 중이면 바로 깨어나 끝낸다 */
    struct sigaction sa;
    memset(&sa, 0, sizeof sa);
    sa.sa_handler = on_quit;
    sigaction(SIGTERM, &sa, NULL);
    sigaction(SIGINT, &sa, NULL);

    fprintf(stderr, "뇌: 시작 (pid %d, 모델 %s, 공유 메모리 %s)\n", (int)getpid(), model_name, name);

    brain_shm_game_t g;
    float rate[BRAIN_GROUP_COUNT];
    uint32_t n_calc = 0;
    double calc_sum = 0.0, calc_max = 0.0;
    memset(&g, 0, sizeof g);

    while (!quit) {
        brain_shm_bump(&m->heartbeat);
        if (!brain_shm_get_game(m, &g) || g.game_no == 0) {
            brain_shm_wait(&m->wake, IDLE_MS);
            continue;
        }

        if (g.game_no != out.game_no) {
            if (out.game_no)
                fprintf(stderr, "뇌: 판 %u 끝 (계산한 틱 %u, 건너뛴 틱 %u, 계산 평균 %.3fms 최대 %.3fms)\n",
                        out.game_no, n_calc, out.skipped, n_calc ? calc_sum / n_calc : 0.0, calc_max);
            memset(&out, 0, sizeof out);
            out.game_no = g.game_no;
            n_calc = 0;
            calc_sum = calc_max = 0.0;
            brain_model_reset(g.seed, model, rate);
            publish(m, rate);
            fprintf(stderr, "뇌: 판 %u 시작 (씨앗 %u)\n", g.game_no, g.seed);
        }

        if (g.tick == out.done_tick) {
            brain_shm_wait(&m->wake, IDLE_MS);
            continue;
        }

        int64_t t0 = brain_shm_now_ns();
        if (g.tick > out.done_tick + 1u) out.skipped += g.tick - out.done_tick - 1u;
        brain_model_tick(g.stim, rate);
        int64_t t1 = brain_shm_now_ns();
        double calc = (double)(t1 - t0) / 1e6;
        n_calc++;
        calc_sum += calc;
        if (calc > calc_max) calc_max = calc;
        out.done_tick = g.tick;
        out.calc_us = (uint32_t)((t1 - t0) / 1000);
        out.took_us = t1 > g.sent_ns ? (uint32_t)((t1 - g.sent_ns) / 1000) : 0;
        publish(m, rate);
    }

    if (own) shm_unlink(name);
    return 0;
}
