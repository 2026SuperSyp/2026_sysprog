/* 뇌 프로세스 관리(서버 전용).
 *
 * 공유 메모리의 주인은 서버다. 이름에 서버의 pid를 넣어 만들고(/ssp_brain_<pid>), 끝날 때 지운다.
 * 뇌 프로세스는 스테이지와 같은 규칙으로 띄운다(server.c의 규칙 3).
 * 뇌가 죽으면 게임 마스터가 SIGCHLD 사건에서 거두고 다시 띄운다. 죽지는 않았는데 멈춘 것은
 * 감시 스레드가 알아낸다. 감시 스레드는 심장 박동 수만 보고 사건 하나를 넘길 뿐이고,
 * 죽이고 다시 띄우는 일은 게임 마스터가 한다. fork, exec, waitpid, kill은 게임 마스터만 한다.
 * 다시 띄우기 전에 뇌 쪽 칸을 비우고 재시작 횟수를 올린다. 심장 박동 수는 비우지 않는다
 * (지켜보는 쪽은 값이 바뀌는지만 본다). */
#include "core/brain_sup.h"
#include "core/selfdir.h"
#include "brain/brain_shm.h"

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <pthread.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/prctl.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

static char brain_bin[SELFDIR_MAX + 16];     /* 서버 실행 파일 옆의 brain_proc. brain_sup_start가 채운다 */
#define BRAIN_LOG     "build/logs/brain.log"
#define WATCH_MS      100        /* 감시 스레드가 심장 박동을 보는 간격 */
#define STALL_MS      1000       /* 돌던 뇌의 심장 박동이 이만큼 그대로면 멈춘 것으로 본다 */
#define START_MS      10000      /* 막 띄운 뇌는 준비(배선 파일 읽기)에 시간이 걸린다. 첫 박동은 이만큼 기다린다 */
#define MIN_LIFE_MS   1000       /* 이보다 빨리 죽으면 바로 다시 띄우지 않고 이 간격을 지킨다 */

static struct {
    brain_shm_t *shm;
    char name[64], model[16];
    pid_t owner, pid;                /* pid: 지금 떠 있는 뇌. 없으면 0 */
    int64_t started, retry_at;       /* 마지막으로 띄운 시각, 다시 띄우기로 한 시각(0이면 예정 없음) */
    int stall_kill;                  /* 멈춰서 우리가 죽였다 */
    char why[64];                    /* 뇌가 마지막으로 끝난 이유(로그에 쓴다) */
    void (*on_stall)(uint32_t);
} B;

static int64_t now_ms(void)
{
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return (int64_t)t.tv_sec * 1000 + t.tv_nsec / 1000000;
}

/* ---- 감시 스레드 -------------------------------------------------------------- */
static void *watchdog_main(void *arg)
{
    uint32_t epoch = brain_shm_load(&B.shm->restarts), beat = brain_shm_load(&B.shm->heartbeat);
    int seen = 0, told = 0;
    int64_t since = now_ms();
    (void)arg;
    for (;;) {
        struct timespec nap = { 0, WATCH_MS * 1000000L };
        nanosleep(&nap, NULL);
        uint32_t e = brain_shm_load(&B.shm->restarts), h = brain_shm_load(&B.shm->heartbeat);
        int64_t now = now_ms();
        if (e != epoch) {                            /* 다시 띄웠다. 처음부터 다시 센다 */
            epoch = e; beat = h; seen = 0; told = 0; since = now;
        } else if (h != beat) {
            beat = h; seen = 1; since = now;
        } else if (!told && now - since > (seen ? STALL_MS : START_MS)) {
            told = 1;                                /* 같은 뇌에 대해서는 한 번만 알린다 */
            B.on_stall(epoch);
        }
    }
    return NULL;
}

/* ---- 띄우기와 다시 띄우기 (게임 마스터) ------------------------------------------ */
static int spawn(void)
{
    char *path = brain_bin;
    char a_shm[96], a_model[32];

    /* fork와 exec 사이에는 안전한 함수만 부를 수 있으므로 글자열은 미리 만든다 */
    snprintf(a_shm, sizeof a_shm, "--shm=%s", B.name);
    snprintf(a_model, sizeof a_model, "--model=%s", B.model);
    char *argv[] = { path, a_shm, a_model, NULL };
    mkdir("build/logs", 0755);
    int log_fd = open(BRAIN_LOG, O_WRONLY | O_CREAT | O_APPEND | O_CLOEXEC, 0644);

    B.started = now_ms();
    B.stall_kill = 0;
    pid_t pid = fork();
    if (pid < 0) {
        if (log_fd >= 0) close(log_fd);
        return -1;
    }
    if (pid == 0) {
        sigset_t none;
        sigemptyset(&none);
        sigprocmask(SIG_SETMASK, &none, NULL);       /* 부모가 막아 둔 시그널을 푼다 */
        signal(SIGPIPE, SIG_DFL);
        prctl(PR_SET_PDEATHSIG, SIGKILL);            /* 서버가 죽으면 같이 죽는다 */
        if (log_fd >= 0) { dup2(log_fd, 1); dup2(log_fd, 2); }
        execv(path, argv);
        _exit(127);
    }
    if (log_fd >= 0) close(log_fd);
    B.pid = pid;
    return 0;
}

static void restart(void)
{
    brain_shm_clear_brain(B.shm);                    /* 죽은 뇌가 남긴 값을 비운다. 뇌는 이미 거뒀다 */
    brain_shm_bump(&B.shm->restarts);
    B.retry_at = 0;
    if (spawn() != 0) {
        fprintf(stderr, "서버: 뇌를 다시 띄우지 못했습니다(%s). 잠시 뒤에 다시 해 봅니다\n", strerror(errno));
        B.retry_at = now_ms() + MIN_LIFE_MS;
        return;
    }
    fprintf(stderr, "서버: 뇌를 다시 띄웠습니다 (%u번째, pid %d, 이유: %s)\n",
            brain_shm_load(&B.shm->restarts), (int)B.pid, B.why);
}

void brain_sup_reaped(pid_t pid, int status)
{
    if (!B.shm || pid != B.pid) return;
    B.pid = 0;
    if (B.stall_kill) snprintf(B.why, sizeof B.why, "멈춤");
    else if (WIFSIGNALED(status)) snprintf(B.why, sizeof B.why, "시그널 %d로 죽음", WTERMSIG(status));
    else snprintf(B.why, sizeof B.why, "종료 코드 %d로 끝남", WEXITSTATUS(status));

    if (now_ms() - B.started >= MIN_LIFE_MS) {
        restart();
    } else {
        /* 뜨자마자 죽는다(실행 파일이나 자료가 없는 경우 등). 쉬지 않고 되풀이하지 않게 간격을 둔다 */
        fprintf(stderr, "서버: 뇌가 뜬 지 %dms 안에 끝났습니다(%s). 조금 있다가 다시 띄웁니다\n", MIN_LIFE_MS, B.why);
        B.retry_at = B.started + MIN_LIFE_MS;
    }
}

void brain_sup_stalled(uint32_t epoch)
{
    /* 그 사이에 이미 다시 띄웠거나 죽어서 거둔 뒤라면 지난 사건이다 */
    if (!B.shm || B.pid <= 0 || epoch != brain_shm_load(&B.shm->restarts)) return;
    fprintf(stderr, "서버: 뇌(pid %d)의 심장 박동이 멈췄습니다. 죽이고 다시 띄웁니다\n", (int)B.pid);
    B.stall_kill = 1;
    kill(B.pid, SIGKILL);                            /* 거두고 다시 띄우는 것은 SIGCHLD 사건에서 한다 */
}

int brain_sup_wait_ms(void)
{
    if (!B.shm || !B.retry_at) return -1;
    int64_t left = B.retry_at - now_ms();
    return left < 0 ? 0 : (int)left;
}

void brain_sup_tick(void)
{
    if (B.shm && B.retry_at && B.pid == 0 && now_ms() >= B.retry_at) restart();
}

/* ---- 시작과 끝 ---------------------------------------------------------------- */
/* 서버가 끝날 때(exit) 불린다. 뇌를 거두고 공유 메모리의 이름을 지운다.
 * 매핑은 풀지 않는다. 감시 스레드가 아직 읽고 있을 수 있고, 프로세스가 끝나면 어차피 풀린다. */
static void cleanup(void)
{
    if (!B.shm || getpid() != B.owner) return;
    if (B.pid > 0) {
        kill(B.pid, SIGKILL);
        waitpid(B.pid, NULL, 0);
        B.pid = 0;
    }
    shm_unlink(B.name);
}

/* 서버가 SIGKILL 등으로 죽으면 공유 메모리가 남는다. 주인이 사라진 것을 찾아 지운다. */
static void sweep_stale(void)
{
    DIR *d = opendir("/dev/shm");
    struct dirent *e;
    if (!d) return;
    while ((e = readdir(d)) != NULL) {
        char name[300];
        int pid = 0;
        if (sscanf(e->d_name, "ssp_brain_%d", &pid) != 1 || pid <= 0) continue;
        if (kill(pid, 0) == 0 || errno != ESRCH) continue;       /* 그 서버가 아직 살아 있다 */
        snprintf(name, sizeof name, "/%s", e->d_name);
        shm_unlink(name);
    }
    closedir(d);
}

int brain_sup_start(const char *model, int wait_mode, void (*on_stall)(uint32_t epoch))
{
    pthread_t t;
    snprintf(brain_bin, sizeof brain_bin, "%s/brain_proc", selfdir());
    if (access(brain_bin, X_OK) != 0) {
        fprintf(stderr, "서버: %s 가 없습니다. make 로 먼저 빌드하세요\n", brain_bin);
        return -1;
    }
    sweep_stale();
    snprintf(B.name, sizeof B.name, "/ssp_brain_%d", (int)getpid());
    snprintf(B.model, sizeof B.model, "%s", model);
    shm_unlink(B.name);                              /* 같은 pid를 썼던 옛 서버가 남긴 것이 있으면 지운다 */
    brain_shm_t *m = brain_shm_create(B.name);
    if (!m) {
        fprintf(stderr, "서버: 공유 메모리 %s 을(를) 만들 수 없습니다(%s)\n", B.name, strerror(errno));
        return -1;
    }
    B.owner = getpid();
    B.on_stall = on_stall;
    B.shm = m;
    atexit(cleanup);

    /* 스테이지와 뇌는 이 환경 변수를 물려받아 같은 공유 메모리에 붙는다 */
    setenv(BRAIN_SHM_ENV, B.name, 1);
    if (wait_mode) setenv(BRAIN_SHM_WAIT_ENV, "1", 1);
    else unsetenv(BRAIN_SHM_WAIT_ENV);

    if (spawn() != 0) {
        fprintf(stderr, "서버: 뇌를 띄우지 못했습니다(%s)\n", strerror(errno));
        return -1;
    }
    /* 뇌가 준비를 마칠 때까지(첫 심장 박동) 기다린다. 그 전에 끝나 버리면 서버도 시작하지 않는다.
     * 모델 이름이 틀렸거나 진짜 뇌의 자료가 없는 경우다 */
    int64_t t0 = now_ms();
    while (brain_shm_load(&m->heartbeat) == 0) {
        struct timespec nap = { 0, 10 * 1000000L };
        if (waitpid(B.pid, NULL, WNOHANG) == B.pid) {
            B.pid = 0;
            fprintf(stderr, "서버: 뇌가 시작하지 못했습니다. %s 의 끝부분을 보세요\n", BRAIN_LOG);
            return -1;
        }
        if (now_ms() - t0 > START_MS) {
            fprintf(stderr, "서버: 뇌가 %d초 안에 준비되지 않았습니다\n", START_MS / 1000);
            return -1;                               /* 끝날 때 cleanup이 거둔다 */
        }
        nanosleep(&nap, NULL);
    }
    fprintf(stderr, "서버: 뇌를 띄웠습니다 (pid %d, 모델 %s, 공유 메모리 %s, 준비 %dms)\n",
            (int)B.pid, B.model, B.name, (int)(now_ms() - t0));

    if (pthread_create(&t, NULL, watchdog_main, NULL) != 0) {
        fprintf(stderr, "서버: 감시 스레드를 만들 수 없습니다\n");
        return -1;
    }
    pthread_detach(t);
    return 0;
}
