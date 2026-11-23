/* 뇌 공유 메모리를 만들고, 붙고, 읽고 쓰는 부분. 서버, 뇌 프로세스, 스테이지가 같이 쓴다.
 * 약속은 brain_shm.h의 머리 주석에 있다. */
#include "brain/brain_shm.h"

#include <fcntl.h>
#include <sched.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

#define READ_TRIES 16          /* 읽기를 이만큼 해 보고 안 되면 직전 값을 쓴다 */

int64_t brain_shm_now_ns(void)
{
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return (int64_t)t.tv_sec * 1000000000LL + t.tv_nsec;
}

static brain_shm_t *map_object(int fd)
{
    void *p = mmap(NULL, sizeof(brain_shm_t), PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    return p == MAP_FAILED ? NULL : p;
}

brain_shm_t *brain_shm_create(const char *name)
{
    brain_shm_t *m = NULL;
    int fd = shm_open(name, O_RDWR | O_CREAT | O_EXCL, 0600);    /* shm_open은 exec 때 닫히게 연다 */
    if (fd < 0) return NULL;
    if (ftruncate(fd, (off_t)sizeof *m) == 0) m = map_object(fd);
    close(fd);
    if (!m) { shm_unlink(name); return NULL; }

    /* 새로 만든 것은 0으로 채워져 있다. 식별값은 나머지를 다 채운 뒤에 쓴다 */
    sem_init(&m->wake, 1, 0);
    sem_init(&m->done, 1, 0);
    m->version = BRAIN_SHM_VERSION;
    m->size = (uint32_t)sizeof *m;
    __atomic_store_n(&m->magic, BRAIN_SHM_MAGIC, __ATOMIC_SEQ_CST);
    return m;
}

brain_shm_t *brain_shm_attach(const char *name)
{
    brain_shm_t *m = NULL;
    struct stat st;
    int fd = shm_open(name, O_RDWR, 0);
    if (fd < 0) return NULL;
    if (fstat(fd, &st) == 0 && (size_t)st.st_size >= sizeof *m) m = map_object(fd);
    close(fd);
    if (m && (brain_shm_load(&m->magic) != BRAIN_SHM_MAGIC || m->version != BRAIN_SHM_VERSION ||
              m->size != sizeof *m)) {
        munmap(m, sizeof *m);
        m = NULL;
    }
    return m;
}

/* ---- seqlock ----------------------------------------------------------------- */
static void seq_write(uint32_t *seq, void *cell, const void *src, size_t n)
{
    /* 쓰다 죽은 쪽이 홀수로 남겼으면 그 홀수에서 이어 간다. 끝나면 짝수가 된다 */
    uint32_t s = __atomic_load_n(seq, __ATOMIC_SEQ_CST) | 1u;
    __atomic_store_n(seq, s, __ATOMIC_SEQ_CST);
    memcpy(cell, src, n);
    __atomic_store_n(seq, s + 1u, __ATOMIC_SEQ_CST);
}

static int seq_read(const uint32_t *seq, const void *cell, void *tmp, size_t n)
{
    for (int i = 0; i < READ_TRIES; i++) {
        uint32_t a = __atomic_load_n(seq, __ATOMIC_SEQ_CST);
        if (!(a & 1u)) {
            memcpy(tmp, cell, n);
            if (__atomic_load_n(seq, __ATOMIC_SEQ_CST) == a) return 1;
        }
        sched_yield();                           /* 쓰는 중이다. 잠깐 비켜 준다 */
    }
    return 0;
}

void brain_shm_put_game(brain_shm_t *m, const brain_shm_game_t *g)
{
    seq_write(&m->game_seq, &m->game, g, sizeof *g);
}

void brain_shm_put_brain(brain_shm_t *m, const brain_shm_brain_t *b)
{
    seq_write(&m->brain_seq, &m->brain, b, sizeof *b);
}

void brain_shm_clear_brain(brain_shm_t *m)
{
    static const brain_shm_brain_t empty;
    seq_write(&m->brain_seq, &m->brain, &empty, sizeof empty);
}

int brain_shm_get_game(brain_shm_t *m, brain_shm_game_t *out)
{
    brain_shm_game_t tmp;
    if (!seq_read(&m->game_seq, &m->game, &tmp, sizeof tmp)) return 0;
    *out = tmp;
    return 1;
}

int brain_shm_get_brain(brain_shm_t *m, brain_shm_brain_t *out)
{
    static brain_shm_brain_t tmp;                /* 크기가 커서 스택에 두지 않는다. 부르는 스레드는 하나다 */
    if (!seq_read(&m->brain_seq, &m->brain, &tmp, sizeof tmp)) return 0;
    *out = tmp;
    return 1;
}

/* ---- 깨우기 ------------------------------------------------------------------- */
void brain_shm_post(sem_t *s)
{
    int v = 0;
    /* 아직 받아 가지 않은 신호가 있으면 더 쌓지 않는다. 받는 쪽은 깬 뒤에 틱 번호를 다시 본다 */
    if (sem_getvalue(s, &v) == 0 && v > 0) return;
    sem_post(s);
}

int brain_shm_wait(sem_t *s, int ms)
{
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    t.tv_nsec += (long)(ms % 1000) * 1000000L;
    t.tv_sec += ms / 1000 + t.tv_nsec / 1000000000L;
    t.tv_nsec %= 1000000000L;
    return sem_clockwait(s, CLOCK_MONOTONIC, &t) == 0;       /* 시간이 다 됐거나 시그널이 왔으면 0 */
}
