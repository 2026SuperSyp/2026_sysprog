/* 일을 여러 스레드에 나눠 주기 (코어 전용, 선언은 ui/hd.h).
 *
 * 화면을 실제 해상도의 픽셀 그림으로 보내면 한 프레임이 300만 픽셀을 넘는다. 그리기, 색 줄이기, Sixel로 적기를
 * 한 스레드로 하면 한 프레임에 수십 ms가 걸려서, 줄(또는 띠) 단위로 나눠 여러 스레드가 같이 한다.
 *
 *   par_for(n, 묶음, fn, arg)   0부터 n-1까지를 묶음 크기로 잘라 fn(처음, 끝, arg)을 여러 스레드에서 부른다.
 *                               전부 끝난 뒤에 돌아온다. fn은 자기 구간만 건드려야 한다.
 *
 * 일꾼 스레드는 처음 부를 때 만들어 두고 계속 쓴다(프레임마다 만들면 만드는 값이 더 든다). 부른 스레드도 같이 일한다.
 * 구간은 미리 나눠 주지 않고, 한가한 스레드가 "다음 구간"을 가져간다(원자적 증가). 구간마다 걸리는 시간이 달라도
 * 고르게 나뉜다. 한 번에 한 곳에서만 부른다(화면을 그리는 스레드). par_for 안에서 par_for를 부르지 않는다. */
#include "ui/hd.h"

#include <pthread.h>
#include <stdatomic.h>
#include <unistd.h>

#define PAR_MAX 8                /* 일꾼까지 합친 스레드 수의 한도 */

static struct {
    pthread_mutex_t mu;
    pthread_cond_t wake, done;
    int n_threads;               /* 부르는 스레드까지 합친 수 */
    unsigned long job;           /* 일의 번호. 일꾼은 번호가 바뀌면 깨어난다 */
    par_fn fn;
    void *arg;
    int n, chunk;
    atomic_int next;             /* 다음에 가져갈 구간의 처음 */
    int busy;                    /* 이번 일을 아직 끝내지 않은 일꾼 수 */
} P = { .mu = PTHREAD_MUTEX_INITIALIZER, .wake = PTHREAD_COND_INITIALIZER, .done = PTHREAD_COND_INITIALIZER };
static pthread_once_t once = PTHREAD_ONCE_INIT;

static void take_chunks(void)
{
    for (;;) {
        int from = atomic_fetch_add(&P.next, P.chunk);
        if (from >= P.n) return;
        P.fn(from, from + P.chunk < P.n ? from + P.chunk : P.n, P.arg);
    }
}

static void *worker(void *arg)
{
    unsigned long seen = 0;
    (void)arg;
    for (;;) {
        pthread_mutex_lock(&P.mu);
        while (P.job == seen) pthread_cond_wait(&P.wake, &P.mu);
        seen = P.job;
        pthread_mutex_unlock(&P.mu);
        take_chunks();
        pthread_mutex_lock(&P.mu);
        if (--P.busy == 0) pthread_cond_signal(&P.done);
        pthread_mutex_unlock(&P.mu);
    }
    return NULL;
}

static void start(void)
{
    long cores = sysconf(_SC_NPROCESSORS_ONLN);
    int want = cores < 1 ? 1 : (cores > PAR_MAX ? PAR_MAX : (int)cores), made = 1;
    for (int i = 1; i < want; i++) {
        pthread_t t;
        if (pthread_create(&t, NULL, worker, NULL) != 0) break;
        pthread_detach(t);
        made++;
    }
    P.n_threads = made;
}

int par_threads(void)
{
    pthread_once(&once, start);
    return P.n_threads;
}

void par_for(int n, int chunk, par_fn fn, void *arg)
{
    if (n <= 0 || !fn) return;
    pthread_once(&once, start);
    if (chunk < 1) chunk = 1;
    if (P.n_threads <= 1 || n <= chunk) { fn(0, n, arg); return; }

    pthread_mutex_lock(&P.mu);
    P.fn = fn;
    P.arg = arg;
    P.n = n;
    P.chunk = chunk;
    atomic_store(&P.next, 0);
    P.busy = P.n_threads - 1;
    P.job++;
    pthread_cond_broadcast(&P.wake);
    pthread_mutex_unlock(&P.mu);

    take_chunks();

    pthread_mutex_lock(&P.mu);
    while (P.busy > 0) pthread_cond_wait(&P.done, &P.mu);      /* 늦게 깬 일꾼도 이번 일을 지나가야 다음 일을 맡길 수 있다 */
    pthread_mutex_unlock(&P.mu);
}
