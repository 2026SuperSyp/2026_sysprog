/* 서버. 접속을 받고, 게임의 단계를 진행하고, 라운드마다 스테이지 프로세스를 띄운다.
 *
 *   build/dev/server [--port=N] [--order=이름,이름,...] [--rounds=N] [--seed=N]
 *                    [--brain=fast|slow|real] [--fast] [--once] [--brain-inproc] [--brain-strict] [--boss=초]
 *   검사용: [--tick-ms=N] 틱 사이를 N ms로 한다(--fast여도 지킨다), [--ticks=N] 한 판을 N틱으로 한다,
 *           [--idle-ms=N] 말이 없는 접속을 N ms 뒤에 끊는다(기본 10초)
 *
 * 스레드
 *   시그널 스레드        시그널을 기다렸다가 사건으로 넘긴다
 *   accept 스레드        접속을 받는다
 *   접속자 읽기 스레드   메시지를 읽어 사건으로 넘긴다 (접속마다)
 *   접속자 쓰기 스레드   보낼 것을 꺼내 쓴다 (접속마다). 소켓 하나에 쓰는 스레드는 이것 하나다
 *   게임 마스터(main)    사건을 하나씩 처리한다. 게임 상태, 자식 프로세스, 파이프는 이 스레드만 만진다
 *   감시 스레드          뇌 프로세스의 심장 박동을 보고, 멈추면 사건으로 넘긴다 (core/brain_sup.c)
 *
 * 게임 마스터가 기다리는 곳은 poll 하나다. 사건 큐를 깨우는 eventfd와 스테이지 파이프를 함께 보고,
 * 다음 마감 시각까지만 기다린다.
 *
 * 지키는 규칙
 *   1. 시그널: 첫 스레드를 만들기 전에 SIGCHLD, SIGALRM, SIGINT, SIGTERM을 막고, 시그널 스레드가
 *      sigwaitinfo로 받는다. 시그널 처리기는 없다. SIGPIPE는 무시한다.
 *   2. 자식의 주인: fork, exec, waitpid는 게임 마스터만 한다.
 *   3. fd: 모든 fd는 exec 때 닫히게 만든다. 스테이지에 넘길 두 개만 fork 뒤에 풀어 준다.
 *
 * 뇌는 따로 뜬 프로세스(build/dev/brain_proc)가 계산하고 스테이지와는 공유 메모리로 주고받는다.
 * --brain-inproc을 주면 전처럼 스테이지 안에서 계산한다.
 * 뇌가 뜨지 못하면 규칙으로 움직이는 뇌(fast)로 바꿔 이어 간다. --brain-strict를 주면 바꾸지 않는다(core/brain_sup.h).
 *
 * 마지막 라운드 뒤에 살아남은 사람이 있으면 보스전을 한다(아래 "보스전"). --boss=초 로 길이를 바꾸고, 0이면 건너뛴다. */
#include "boss/boss.h"
#include "brain/brain_host.h"
#include "core/brain_sup.h"
#include "core/frame.h"
#include "core/msg.h"
#include "core/selfdir.h"
#include "core/toplist.h"
#include "protocol.h"
#include "stage.h"
#include "ui/ui_internal.h"

#include <errno.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <poll.h>
#include <pthread.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/eventfd.h>
#include <sys/prctl.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

#define MAX_CONN     16
#define MAX_QUEUED   256         /* 접속자 한 명에게 밀린 메시지가 이보다 많으면 그 접속만 끊는다 */
#define MAX_ORDER    8
#define TICK_MS      50
#define STAGE_WAIT_MS 2000       /* 스테이지가 이만큼 답하지 않으면 죽인다 */
#define HELLO_WAIT_MS 3000       /* 접속하고 이만큼 HELLO가 오지 않으면 끊는다 */
#define SEND_WAIT_MS  3000       /* 접속자가 이만큼 하나도 받아 가지 않으면 끊는다 */
#define IDLE_WAIT_MS  10000      /* 들어온 접속자가 이만큼 아무것도 보내지 않으면 끊는다. 클라이언트는 2초마다 PING을 보낸다 */
#define SEND_BUF      65536      /* 접속자마다 커널에 쌓아 두는 보낼 것의 한도(바이트) */
#define INPUT_WIN_MS  100        /* 접속자 한 명의 입력은 이 시간 동안 */
#define INPUT_WIN_MAX 256        /* 이만큼만 받고 넘치는 것은 버린다 */
#define EVQ_KEEP      64         /* 사건 큐에서 입력이 쓰지 못하게 남겨 두는 칸 */
#define SCORE_MAX     1000000    /* 한 라운드 점수로 받아 주는 한도 */
#define BOSS_WIN_BONUS 100       /* 보스전에서 이기고 살아남은 사람에게 더하는 점수 */
#define BOSS_END_MS    3000      /* 보스전이 끝난 뒤 결과(이겼다, 졌다)를 보여 주는 시간. 그 뒤에 랭킹으로 간다 */
#define BOSS_INTRO_MS  5000      /* 보스전을 시작하기 전에 키 안내를 보여 주는 시간. 그동안 초파리는 움직이지 않는다 */
#define BOSS_START_HP  3         /* 보스전에 들어간 사람의 체력. 미니게임에서 남은 기회와 무관하게 모두 같다 */

/* ---- 옵션 ------------------------------------------------------------------ */
static struct {
    int port, n_rounds, fast, once;
    uint32_t seed;
    const char *brain;
    char order[MAX_ORDER][32];
    int n_order;
    int card_ms, ready_ms, verdict_ms, rank_ms;
    int tick_ms, ticks;              /* 검사용. 0이면 쓰지 않는다 */
    int boss_sec;                    /* 보스전 길이(초). 음수이면 boss/config.ini의 값, 0이면 보스전을 건너뛴다 */
    int idle_ms;                     /* 말이 없는 접속을 끊는 시간. 검사에서 --idle-ms=N 으로 줄인다 */
} opt = { PROTO_PORT, 4, 0, 0, 0, "fast", {{0}}, 0, 3000, 2000, 2000, 6000, 0, 0, -1, IDLE_WAIT_MS };

static int64_t now_ms(void)
{
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return (int64_t)t.tv_sec * 1000 + t.tv_nsec / 1000000;
}

/* ---- 사건 큐: 다른 스레드가 넣고 게임 마스터가 꺼낸다 ------------------------- */
enum { EV_JOIN, EV_LEAVE, EV_INPUT, EV_SIGNAL, EV_BRAIN };
typedef struct {
    int type, slot, gen, signo;
    char name[PROTO_NAME_LEN];
    input_msg_t in;
} event_t;

#define EVQ_CAP 1024
static event_t evq[EVQ_CAP];
static int evq_head, evq_tail;
static pthread_mutex_t evq_mu = PTHREAD_MUTEX_INITIALIZER;
static int evfd = -1;

static void ev_push(const event_t *e)
{
    pthread_mutex_lock(&evq_mu);
    int next = (evq_tail + 1) % EVQ_CAP;
    int room = (evq_head - next + EVQ_CAP) % EVQ_CAP;    /* 이것을 넣고도 남는 칸 */
    /* 꽉 차면 버린다. 입력은 먼저 버려서 접속, 끊김, 시그널이 들어갈 자리를 남긴다 */
    if (next != evq_head && (e->type != EV_INPUT || room >= EVQ_KEEP)) {
        evq[evq_tail] = *e;
        evq_tail = next;
    }
    pthread_mutex_unlock(&evq_mu);
    uint64_t one = 1;
    if (write(evfd, &one, sizeof one) < 0) { /* 이미 깨어 있다 */ }
}

static int ev_pop(event_t *e)
{
    int got = 0;
    pthread_mutex_lock(&evq_mu);
    if (evq_head != evq_tail) {
        *e = evq[evq_head];
        evq_head = (evq_head + 1) % EVQ_CAP;
        got = 1;
    }
    pthread_mutex_unlock(&evq_mu);
    return got;
}

/* ---- 접속 ------------------------------------------------------------------ */
typedef struct out_msg { struct out_msg *next; uint32_t len; uint8_t data[]; } out_msg_t;

typedef struct {
    pthread_mutex_t mu;
    pthread_cond_t cv;
    int used, fd, gen, closing;
    out_msg_t *head, *tail;          /* 순서대로 다 가야 하는 메시지 */
    int queued;
    out_msg_t *latest;               /* 최신 것만 있으면 되는 메시지(화면). 한 칸만 두고 덮어쓴다 */
} conn_t;

static conn_t conns[MAX_CONN];
static pthread_mutex_t conns_mu = PTHREAD_MUTEX_INITIALIZER;     /* 자리의 used를 지킨다 */

/* 접속자에게 보낼 메시지를 맡긴다. 실제로 쓰는 것은 쓰기 스레드다. */
static void conn_post(int slot, int gen, uint8_t type, uint8_t flags, const void *body, uint32_t len, int latest)
{
    conn_t *c = &conns[slot];
    out_msg_t *m = malloc(sizeof *m + sizeof(msg_hdr_t) + len);
    msg_hdr_t h = { len, type, flags, (uint16_t)slot };
    if (!m) return;
    m->next = NULL;
    m->len = (uint32_t)sizeof h + len;
    memcpy(m->data, &h, sizeof h);
    if (len > 0) memcpy(m->data + sizeof h, body, len);

    pthread_mutex_lock(&c->mu);
    if (!c->used || c->gen != gen || c->closing) {
        free(m);
    } else if (latest) {
        free(c->latest);
        c->latest = m;
    } else {
        if (c->tail) c->tail->next = m; else c->head = m;
        c->tail = m;
        if (++c->queued > MAX_QUEUED) {              /* 너무 느린 접속자는 끊는다 */
            c->closing = 1;
            shutdown(c->fd, SHUT_RDWR);
        }
    }
    pthread_cond_signal(&c->cv);
    pthread_mutex_unlock(&c->mu);
}

static int send_bytes(int fd, const uint8_t *p, size_t n)
{
    while (n > 0) {
        ssize_t w = send(fd, p, n, MSG_NOSIGNAL);
        if (w < 0) {
            if (errno == EINTR) continue;
            return -1;
        }
        /* 일부만 보내고 돌아왔으면 나머지를 이어서 보낸다. 느려도 받아 가는 접속자는 끊지 않는다.
         * SEND_WAIT_MS 동안 한 바이트도 못 보냈을 때만 send가 실패로 돌아온다 */
        p += w;
        n -= (size_t)w;
    }
    return 0;
}

static void *writer_main(void *arg)
{
    conn_t *c = arg;
    for (;;) {
        pthread_mutex_lock(&c->mu);
        while (!c->head && !c->latest && !c->closing) pthread_cond_wait(&c->cv, &c->mu);
        if (c->closing) { pthread_mutex_unlock(&c->mu); break; }
        out_msg_t *list = c->head, *latest = c->latest;
        c->head = c->tail = NULL;
        c->queued = 0;
        c->latest = NULL;
        pthread_mutex_unlock(&c->mu);

        int bad = 0;
        while (list) {
            out_msg_t *next = list->next;
            if (!bad && send_bytes(c->fd, list->data, list->len) != 0) bad = 1;
            free(list);
            list = next;
        }
        if (latest) {
            if (!bad && send_bytes(c->fd, latest->data, latest->len) != 0) bad = 1;
            free(latest);
        }
        if (bad) shutdown(c->fd, SHUT_RDWR);         /* 읽기 스레드가 알아채고 정리한다 */
    }
    return NULL;
}

/* 접속 하나가 서버를 붙들지 못하게 한다. 보낼 것은 조금만 쌓아 두고, 상대가 SEND_WAIT_MS 동안 하나도
 * 받아 가지 않으면 send가 실패하게 한다. recv_ms는 읽기를 기다리는 한도다(0이면 끝없이 기다린다). */
static void conn_limits(int fd, int recv_ms)
{
    int sndbuf = SEND_BUF;
    struct timeval snd = { SEND_WAIT_MS / 1000, (SEND_WAIT_MS % 1000) * 1000 };
    struct timeval rcv = { recv_ms / 1000, (recv_ms % 1000) * 1000 };
    setsockopt(fd, SOL_SOCKET, SO_SNDBUF, &sndbuf, sizeof sndbuf);
    setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &snd, sizeof snd);
    setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &rcv, sizeof rcv);
}

static const char *ranking_path;             /* 랭킹 파일. 값은 아래 게임 마스터 쪽에서 정한다 */

/* 첫 메시지(HELLO)를 deadline(ms)까지 다 받는다. 한 바이트씩 아주 느리게 보내는 접속이 자리를
 * 붙들지 못하게, 읽을 때마다가 아니라 전체에 시간 한도를 둔다. 1이면 다 받았다. */
static int recv_hello(int fd, msg_hdr_t *h, uint8_t *body, uint32_t cap, int64_t deadline)
{
    uint32_t got = 0, need = sizeof *h;
    for (;;) {
        if (got == sizeof *h) {
            if (h->len > cap) return -1;
            need = (uint32_t)sizeof *h + h->len;
        }
        if (got == need) return 1;
        int64_t left = deadline - now_ms();
        if (left <= 0) return 0;
        struct pollfd pf = { fd, POLLIN, 0 };
        int r = poll(&pf, 1, (int)left);
        if (r < 0 && errno == EINTR) continue;
        if (r <= 0) return r;
        ssize_t n = got < sizeof *h ? recv(fd, (uint8_t *)h + got, sizeof *h - got, MSG_DONTWAIT)
                                    : recv(fd, body + (got - sizeof *h), need - got, MSG_DONTWAIT);
        if (n < 0 && (errno == EINTR || errno == EAGAIN)) continue;
        if (n <= 0) return -1;
        got += (uint32_t)n;
    }
}

static void *reader_main(void *arg)
{
    conn_t *c = arg;
    int slot = (int)(c - conns), gen = c->gen, one = 1, writer_up = 0, win_n = 0;
    int64_t win_at = 0;
    pthread_t writer;
    msg_hdr_t h;
    static __thread uint8_t body[4096];
    int64_t hello_by = now_ms() + HELLO_WAIT_MS;
    setsockopt(c->fd, IPPROTO_TCP, TCP_NODELAY, &one, sizeof one);
    conn_limits(c->fd, HELLO_WAIT_MS);

    /* 첫 메시지는 HELLO여야 하고, 접속하고 HELLO_WAIT_MS 안에 다 와야 한다 */
    hello_msg_t hello;
    if (recv_hello(c->fd, &h, body, sizeof body, hello_by) != 1 || h.type != MSG_HELLO || h.len != sizeof hello) goto out;
    memcpy(&hello, body, sizeof hello);
    if (hello.version != PROTO_VERSION) {
        const char *why = "version mismatch";
        msg_send(c->fd, MSG_REJECT, 0, 0, why, (uint32_t)strlen(why));
        goto out;
    }
    hello.name[PROTO_NAME_LEN - 1] = '\0';
    if (hello.peek) {
        /* 순위만 보러 온 접속(입장 화면의 "지난 랭킹 보기"). 게임 마스터를 거치지 않고 여기서 답하고 끊는다 */
        static __thread top_msg_t top;
        memset(&top, 0, sizeof top);
        toplist_load(&top, ranking_path, NULL, 0);
        msg_send(c->fd, MSG_TOP, 0, 0, &top, sizeof top);
        goto out;
    }
    /* 들어온 뒤에도 오래 말이 없으면 끊는다. 아무것도 하지 않는 접속이 자리를 다 차지하지 못하게 한다.
     * 로비에서 가만히 있는 사람은 클라이언트가 보내는 PING 덕분에 끊기지 않는다 */
    conn_limits(c->fd, opt.idle_ms);
    if (pthread_create(&writer, NULL, writer_main, c) != 0) goto out;
    writer_up = 1;

    welcome_msg_t wm = { PROTO_VERSION, (uint16_t)slot };
    conn_post(slot, gen, MSG_WELCOME, 0, &wm, sizeof wm, 0);
    event_t join = { .type = EV_JOIN, .slot = slot, .gen = gen };
    snprintf(join.name, sizeof join.name, "%s", hello.name[0] ? hello.name : "PLAYER");
    ev_push(&join);

    for (;;) {
        errno = 0;
        if (msg_recv(c->fd, &h, body, sizeof body) != 1) {
            if (errno == EAGAIN || errno == EWOULDBLOCK)
                fprintf(stderr, "서버: %d초 동안 말이 없는 접속(%d번)을 끊었습니다\n", opt.idle_ms / 1000, slot);
            break;
        }
        if (h.type == MSG_INPUT && h.len == sizeof(input_msg_t)) {
            /* 너무 빠른 입력은 버린다. 한 접속이 사건 큐와 스테이지 통로를 채우지 못하게 한다 */
            int64_t now = now_ms();
            if (now - win_at >= INPUT_WIN_MS) { win_at = now; win_n = 0; }
            if (++win_n > INPUT_WIN_MAX) continue;
            event_t e = { .type = EV_INPUT, .slot = slot, .gen = gen };
            memcpy(&e.in, body, sizeof e.in);
            ev_push(&e);
        } else if (h.type == MSG_PING) {
            conn_post(slot, gen, MSG_PONG, 0, NULL, 0, 0);
        }
    }

out:
    /* 정리: 쓰기 스레드를 끝내고, 소켓을 닫고, 게임 마스터에 알린 뒤 자리를 비운다 */
    pthread_mutex_lock(&c->mu);
    c->closing = 1;
    pthread_cond_signal(&c->cv);
    pthread_mutex_unlock(&c->mu);
    shutdown(c->fd, SHUT_RDWR);                      /* 쓰기 스레드가 send에서 막혀 있으면 깨운다 */
    if (writer_up) pthread_join(writer, NULL);
    close(c->fd);
    pthread_mutex_lock(&c->mu);
    while (c->head) { out_msg_t *n = c->head->next; free(c->head); c->head = n; }
    c->tail = NULL;
    c->queued = 0;
    free(c->latest);
    c->latest = NULL;
    pthread_mutex_unlock(&c->mu);
    if (writer_up) {
        event_t leave = { .type = EV_LEAVE, .slot = slot, .gen = gen };
        ev_push(&leave);
    }
    pthread_mutex_lock(&conns_mu);
    pthread_mutex_lock(&c->mu);                      /* conn_post가 c->mu만 잡고 used를 읽는다 */
    c->used = 0;
    pthread_mutex_unlock(&c->mu);
    pthread_mutex_unlock(&conns_mu);
    return NULL;
}

static void *accept_main(void *arg)
{
    int lfd = *(int *)arg;
    for (;;) {
        int fd = accept4(lfd, NULL, NULL, SOCK_CLOEXEC);
        if (fd < 0) {
            if (errno == EINTR || errno == ECONNABORTED) continue;
            /* fd나 메모리가 잠깐 모자란 것이면 쉬었다가 다시 받는다. 여기서 끝내면 다시는 접속을 받지 못한다 */
            if (errno == EMFILE || errno == ENFILE || errno == ENOBUFS || errno == ENOMEM) {
                usleep(100 * 1000);
                continue;
            }
            perror("accept");
            return NULL;
        }
        conn_t *c = NULL;
        pthread_mutex_lock(&conns_mu);
        for (int i = 0; i < MAX_CONN && !c; i++) {
            if (!conns[i].used) {
                c = &conns[i];
                pthread_mutex_lock(&c->mu);
                c->used = 1;
                c->gen++;
                c->fd = fd;
                c->closing = 0;
                pthread_mutex_unlock(&c->mu);
            }
        }
        pthread_mutex_unlock(&conns_mu);
        pthread_t t;
        if (!c) { close(fd); continue; }             /* 자리가 없다 */
        if (pthread_create(&t, NULL, reader_main, c) != 0) {
            close(fd);
            pthread_mutex_lock(&conns_mu);
            pthread_mutex_lock(&c->mu);
            c->used = 0;
            pthread_mutex_unlock(&c->mu);
            pthread_mutex_unlock(&conns_mu);
            continue;
        }
        pthread_detach(t);
    }
}

/* ---- 시그널 스레드 ----------------------------------------------------------- */
static void *signal_main(void *arg)
{
    sigset_t *set = arg;
    for (;;) {
        siginfo_t si;
        int sig = sigwaitinfo(set, &si);
        if (sig < 0) continue;
        event_t e = { .type = EV_SIGNAL, .signo = sig };
        ev_push(&e);
    }
    return NULL;
}

/* 감시 스레드가 부른다. 뇌가 멈췄다는 사건만 넣는다. gen 자리에 그때의 재시작 횟수를 싣는다. */
static void brain_stall_event(uint32_t epoch)
{
    event_t e = { .type = EV_BRAIN, .gen = (int)epoch };
    ev_push(&e);
}

/* ---- 스테이지 프로세스 (게임 마스터만 다룬다) ---------------------------------- */
static struct {
    pid_t pid;
    int to, from;                    /* 스테이지로 쓰는 쪽, 스테이지에서 읽는 쪽 */
    int open;                        /* 통로가 열려 있는가 */
    int got_info, got_result;
    info_msg_t info;
    result_msg_t result;
    int ticks_sent, awaiting;        /* awaiting: 틱을 보내고 화면을 기다리는 중 */
    int64_t wait_since;
    msg_part_t part;                 /* 스테이지에서 받는 중인 메시지 */
} st = { .to = -1, .from = -1 };

static void stage_close(void)
{
    if (st.to >= 0) close(st.to);
    if (st.from >= 0) close(st.from);
    st.to = st.from = -1;
    st.open = 0;
}

static void stage_kill(void)
{
    if (st.pid > 0) kill(st.pid, SIGKILL);       /* 거두는 것은 SIGCHLD 사건에서 한다 */
    stage_close();
}

static void reap_children(void)
{
    int status;
    pid_t pid;
    /* SIGCHLD는 여러 개가 하나로 합쳐질 수 있다. 더 없을 때까지 거둔다 */
    while ((pid = waitpid(-1, &status, WNOHANG)) > 0) {
        if (pid == st.pid) st.pid = 0;
        else brain_sup_reaped(pid, status);          /* 뇌였으면 다시 띄운다 */
    }
}

static int stage_spawn(const char *name, uint32_t seed, int n_players)
{
    char path[SELFDIR_MAX + 64], a_pipe[48], a_seed[32], a_players[32], a_brain[32], a_ticks[32], log_path[128];
    int to_child[2], from_child[2];
    pid_t me = getpid();

    if (st.pid > 0) kill(st.pid, SIGKILL);           /* 결과를 내고도 끝나지 않은 앞 스테이지를 남기지 않는다 */
    memset(&st, 0, sizeof st);
    st.to = st.from = -1;
    if (pipe2(to_child, O_CLOEXEC) != 0) return -1;
    if (pipe2(from_child, O_CLOEXEC) != 0) { close(to_child[0]); close(to_child[1]); return -1; }

    /* fork와 exec 사이에는 안전한 함수만 부를 수 있으므로 글자열은 미리 만든다 */
    snprintf(path, sizeof path, "%s/stage_%s", selfdir(), name);     /* 서버 실행 파일 옆의 것을 띄운다 */
    snprintf(a_pipe, sizeof a_pipe, "--pipe=%d,%d", to_child[0], from_child[1]);
    snprintf(a_seed, sizeof a_seed, "--seed=%u", seed);
    snprintf(a_players, sizeof a_players, "--players=%d", n_players);
    snprintf(a_brain, sizeof a_brain, "--brain=%s", brain_sup_model() ? brain_sup_model() : opt.brain);
    snprintf(log_path, sizeof log_path, "build/logs/stage_%s.log", name);
    snprintf(a_ticks, sizeof a_ticks, "--ticks=%d", opt.ticks);
    char *argv[] = { path, a_pipe, a_seed, a_players, a_brain, opt.ticks > 0 ? a_ticks : NULL, NULL };
    mkdir("build/logs", 0755);
    int log_fd = open(log_path, O_WRONLY | O_CREAT | O_APPEND | O_CLOEXEC, 0644);

    pid_t pid = fork();
    if (pid < 0) {
        close(to_child[0]); close(to_child[1]); close(from_child[0]); close(from_child[1]);
        if (log_fd >= 0) close(log_fd);
        return -1;
    }
    if (pid == 0) {
        sigset_t none;
        sigemptyset(&none);
        sigprocmask(SIG_SETMASK, &none, NULL);       /* 부모가 막아 둔 시그널을 푼다 */
        signal(SIGPIPE, SIG_DFL);
        prctl(PR_SET_PDEATHSIG, SIGKILL);            /* 서버가 죽으면 같이 죽는다 */
        if (getppid() != me) _exit(0);               /* 위 설정보다 먼저 서버가 죽었다 */
        fcntl(to_child[0], F_SETFD, 0);              /* 넘겨줄 두 fd만 exec 뒤에도 남긴다 */
        fcntl(from_child[1], F_SETFD, 0);
        if (log_fd >= 0) { dup2(log_fd, 1); dup2(log_fd, 2); }
        execv(path, argv);
        _exit(127);
    }
    close(to_child[0]);                              /* 자식 쪽 끝은 바로 닫는다 */
    close(from_child[1]);
    if (log_fd >= 0) close(log_fd);
    st.pid = pid;
    st.to = to_child[1];
    st.from = from_child[0];
    /* 스테이지가 멈추거나 메시지를 절반만 보내도 게임 마스터는 막히지 않아야 한다 */
    fcntl(st.to, F_SETFL, O_NONBLOCK);
    fcntl(st.from, F_SETFL, O_NONBLOCK);
    st.open = 1;
    return 0;
}

/* ---- 게임 마스터 -------------------------------------------------------------- */
typedef struct {
    int used, conn, gen, connected, ready, lives, total, pane, passed, score;
    long join_no;
    char name[PROTO_NAME_LEN];
} seat_t;

static struct { int in, gen, seat; long join_no; char name[PROTO_NAME_LEN]; } members[MAX_CONN];
static seat_t seats[PROTO_SEATS];
static int phase = PH_LOBBY, round_no, void_count, fly_score;
static boss_state_t boss;                    /* 보스전의 상태(아래 "보스전") */
static int64_t deadline;                     /* 이 단계의 마감(ms). 0이면 마감이 없다 */
static uint32_t game_seed;
static long join_counter;
static int game_over;                        /* 한 판이 랭킹까지 갔다(--once에서 끝낼 때 쓴다) */
static uint8_t stage_buf[PROTO_MAX_LEN];

static void start_round(void);

static int host_seat(void)
{
    int best = -1;
    for (int s = 0; s < PROTO_SEATS; s++)
        if (seats[s].used && seats[s].connected && (best < 0 || seats[s].join_no < seats[best].join_no)) best = s;
    return best;
}

static void send_phase(void)
{
    phase_msg_t m;
    memset(&m, 0, sizeof m);
    int64_t now = now_ms();
    m.phase = (uint8_t)phase;
    m.round = phase == PH_LOBBY ? 0 : (uint8_t)(round_no + 1);
    m.n_rounds = (uint8_t)opt.n_rounds;
    m.ms_left = deadline > now ? (uint32_t)(deadline - now) : 0;
    if (phase == PH_PLAY) m.ms_left = (uint32_t)((st.info.n_ticks - st.ticks_sent) * TICK_MS);
    if (phase == PH_BOSS) m.ms_left = (uint32_t)((boss.cfg.ticks - boss.tick) * TICK_MS);
    m.host = (int8_t)host_seat();
    m.fly_score = fly_score;
    for (int s = 0; s < PROTO_SEATS; s++) {
        m.seated[s] = (uint8_t)(seats[s].used && seats[s].connected);
        if (m.seated[s]) m.n_seats++;
        m.pane_of[s] = (int8_t)(seats[s].used ? seats[s].pane : -1);
        m.ready[s] = (uint8_t)seats[s].ready;
        m.lives[s] = (uint8_t)(seats[s].lives < 0 ? 0 : seats[s].lives);
        m.passed[s] = (uint8_t)seats[s].passed;
        m.score[s] = seats[s].score;
        m.total[s] = seats[s].total;
        if (seats[s].used) memcpy(m.name[s], seats[s].name, PROTO_NAME_LEN);
    }
    memcpy(m.title, st.info.title, sizeof m.title);
    memcpy(m.rule, st.info.rule, sizeof m.rule);
    memcpy(m.controls, st.info.controls, sizeof m.controls);
    for (int i = 0; i < MAX_CONN; i++) {
        if (!members[i].in) continue;
        m.me = (int8_t)members[i].seat;
        conn_post(i, members[i].gen, MSG_PHASE, 0, &m, sizeof m, 0);
    }
}

/* ---- 역대 순위 ---------------------------------------------------------------- */
static const char *ranking_path = RANKING_PATH;
static top_msg_t top_now;                    /* 마지막으로 읽은 역대 순위 */
static int top_loaded;

/* 역대 순위를 보낸다. to가 음수이면 모두에게 보낸다.
 * 파일은 첫 접속 때와 판이 끝날 때만 읽는다. 게임 도중에 들어온 사람 때문에 파일을 읽지는 않는다. */
static void send_top(int to)
{
    if (!top_loaded && toplist_load(&top_now, ranking_path, NULL, 0) != 0)
        fprintf(stderr, "서버: 랭킹 파일 %s 을 읽지 못했습니다\n", ranking_path);
    top_loaded = 1;
    for (int i = 0; i < MAX_CONN; i++)
        if (members[i].in && (to < 0 || i == to)) conn_post(i, members[i].gen, MSG_TOP, 0, &top_now, sizeof top_now, 0);
}

/* 한 판이 끝났다. 자리에 앉았던 사람의 누적 점수를 파일에 붙이고 새 순위를 모두에게 보낸다. */
static void save_ranking(void)
{
    rank_rec_t recs[PROTO_SEATS];
    int n = 0;
    int64_t when = (int64_t)time(NULL);
    for (int s = 0; s < PROTO_SEATS; s++) {
        if (!seats[s].used) continue;
        memcpy(recs[n].name, seats[s].name, PROTO_NAME_LEN);
        recs[n].score = seats[s].total;
        recs[n].when = when;
        n++;
    }
    if (ranking_append(ranking_path, recs, n) != 0)
        fprintf(stderr, "서버: 랭킹 파일 %s 에 쓰지 못했습니다\n", ranking_path);
    if (toplist_load(&top_now, ranking_path, recs, n) != 0)  /* 읽지 못하면 전에 읽은 순위를 그대로 보낸다 */
        fprintf(stderr, "서버: 랭킹 파일 %s 을 읽지 못했습니다\n", ranking_path);
    top_loaded = 1;
    send_top(-1);
    memset(top_now.fresh, 0, sizeof top_now.fresh);      /* 나중에 들어오는 사람에게는 새 기록 표시 없이 보낸다 */
}

/* 빈 자리를 접속한 순서대로 채운다. 로비에서만 부른다. */
static void fill_seats(void)
{
    for (int s = 0; s < PROTO_SEATS; s++) {
        if (seats[s].used) continue;
        int pick = -1;
        for (int i = 0; i < MAX_CONN; i++)
            if (members[i].in && members[i].seat < 0 && (pick < 0 || members[i].join_no < members[pick].join_no)) pick = i;
        if (pick < 0) return;
        memset(&seats[s], 0, sizeof seats[s]);
        seats[s].used = seats[s].connected = 1;
        seats[s].conn = pick;
        seats[s].gen = members[pick].gen;
        seats[s].join_no = members[pick].join_no;
        seats[s].pane = -1;
        memcpy(seats[s].name, members[pick].name, PROTO_NAME_LEN);
        members[pick].seat = s;
    }
}

static void to_lobby(void)
{
    stage_kill();
    if (opt.once && game_over) exit(0);
    memset(&st.info, 0, sizeof st.info);
    phase = PH_LOBBY;
    deadline = 0;
    for (int s = 0; s < PROTO_SEATS; s++) {
        if (seats[s].used && !seats[s].connected) seats[s].used = 0;       /* 나간 사람의 자리를 비운다 */
        seats[s].ready = 0;
        seats[s].pane = -1;
        seats[s].passed = seats[s].score = 0;
    }
    fill_seats();
    send_phase();
}

/* 판이 끝났다. 랭킹으로 간다. */
static void finish_game(void)
{
    stage_kill();
    game_over = 1;
    save_ranking();
    phase = PH_RANK;
    deadline = now_ms() + opt.rank_ms;
    send_phase();
}

/* 스테이지가 결과 없이 죽거나 멈췄다. 그 라운드는 무효로 하고 다른 스테이지로 다시 한다. */
static void stage_failed(const char *why)
{
    fprintf(stderr, "서버: 스테이지가 %s. 이 라운드는 무효입니다\n", why);
    stage_kill();
    if (++void_count > 3) finish_game();
    else start_round();
}

static void start_round(void)
{
    int n = 0;
    for (int s = 0; s < PROTO_SEATS; s++) {
        seats[s].passed = seats[s].score = 0;
        seats[s].pane = (seats[s].used && seats[s].connected && seats[s].lives > 0) ? n++ : -1;
    }
    if (n == 0) { finish_game(); return; }
    const char *name = opt.order[(round_no + void_count) % opt.n_order];
    if (stage_spawn(name, game_seed + (uint32_t)round_no * 7919u + (uint32_t)void_count, n) != 0) {
        stage_failed("뜨지 않았습니다");
        return;
    }
    fly_score = 0;
    phase = PH_CARD;
    deadline = now_ms() + opt.card_ms;
    send_phase();
}

static void start_game(void)
{
    game_seed = opt.seed ? opt.seed : (uint32_t)now_ms();
    round_no = void_count = 0;
    for (int s = 0; s < PROTO_SEATS; s++) {
        seats[s].lives = 3;
        seats[s].total = 0;
    }
    /* 스테이지 순서를 섞는다 */
    stage_rng_t r;
    stage_rng_seed(&r, game_seed);
    for (int i = opt.n_order - 1; i > 0; i--) {
        int j = stage_rng_range(&r, 0, i);
        char tmp[32];
        memcpy(tmp, opt.order[i], 32);
        memcpy(opt.order[i], opt.order[j], 32);
        memcpy(opt.order[j], tmp, 32);
    }
    start_round();
}

static void enter_verdict(void)
{
    /* 초파리 점수도 참가자 점수와 같은 한도로 자른다. 한쪽만 자르면 비교가 뒤집힌다 */
    int fly = st.result.fly_score > SCORE_MAX ? SCORE_MAX
            : (st.result.fly_score < -SCORE_MAX ? -SCORE_MAX : st.result.fly_score);
    for (int s = 0; s < PROTO_SEATS; s++) {
        if (!seats[s].used || seats[s].pane < 0) continue;
        /* 스테이지가 터무니없는 점수를 보내도 누적 점수가 넘치지 않게 범위를 자른다 */
        int32_t sc = st.result.score[seats[s].pane];
        seats[s].score = sc > SCORE_MAX ? SCORE_MAX : (sc < -SCORE_MAX ? -SCORE_MAX : sc);
        /* 같거나 높으면 이긴 것이다. 도중에 나간 사람은 진 것으로 한다 */
        seats[s].passed = seats[s].connected && seats[s].score >= fly;
        if (!seats[s].passed) seats[s].lives--;
        seats[s].total += seats[s].score;
    }
    fly_score = fly;
    stage_close();                               /* 스테이지는 스스로 끝난다. 거두는 것은 SIGCHLD에서 */
    phase = PH_VERDICT;
    deadline = now_ms() + opt.verdict_ms;
    send_phase();
}

/* ---- 보스전 ------------------------------------------------------------------ */
/* 마지막 라운드 뒤에 살아남은 참가자들이 초파리와 싸운다. 계산은 boss/boss.c가 하고, 여기서는 틱마다
 * 자극을 뇌에 넣고 반응을 받아 한 틱을 진행한 뒤 상태를 모두에게 보낸다. 뇌는 스테이지와 같은 창구
 * (brain/brain_api.c)로 쓴다. 뇌 프로세스가 떠 있으면 공유 메모리로, 없으면(--brain-inproc) 서버 안에서 계산한다.
 * 참가자 번호는 자리의 pane에 둔다(미니게임의 칸 번호와 같은 자리다). */
static brain_mode_t boss_brain = BRAIN_FAKE_FAST;        /* 뇌 프로세스가 없을 때 서버 안에서 쓸 모델 */
static int boss_over;                                    /* 승패가 났다. 결과를 보여 주는 동안은 틱을 더 돌리지 않는다 */

static void finish_game(void);

static void finish_boss(void)
{
    int win = boss.outcome == BOSS_PLAYERS_WIN;
    for (int s = 0; s < PROTO_SEATS; s++) {
        int p = seats[s].pane;
        if (!seats[s].used || p < 0) continue;
        /* 점수: 초파리에게 준 피해. 이겼고 끝까지 살아 있었으면 BOSS_WIN_BONUS를 더한다 */
        seats[s].passed = win && boss.p[p].alive && seats[s].connected;
        seats[s].score = boss.dmg_dealt[p] + (seats[s].passed ? BOSS_WIN_BONUS : 0);
        seats[s].total += seats[s].score;
    }
    fly_score = boss.fly.hp;
    fprintf(stderr, "서버: 보스전 끝 (%s, %d틱, 초파리 체력 %d/%d)\n", win ? "참가자 승" : "초파리 승",
            boss.tick, boss.fly.hp, boss.fly.hp_max);
    finish_game();
}

/* 지금의 보스전 상태를 접속한 모두에게 보낸다. 상태 뒤에 이번 틱의 뇌(넣은 자극 stim, 나온 반응 rate, 진짜 뇌이면
 * 뇌 지도)를 붙인다. 클라이언트는 그것으로 미니게임 때와 같은 뇌 표시를 보스전 화면에 그린다.
 * 아직 틱을 돌리지 않았으면 stim과 rate가 NULL이고 뇌 값은 0으로 간다.
 * 한 메시지에 같이 싣는 까닭: 이 메시지는 "최신 것만" 보내는 칸에 두는데(conn_post의 latest) 그 칸은 접속마다
 * 하나라서, 따로 보내면 뒤에 맡긴 것이 앞의 것을 밀어낸다 */
static void boss_send(const float *stim, const float *rate)
{
    /* 뇌 지도는 줄여 담아도 원래의 2배를 넘지 않는다 */
    static uint8_t buf[sizeof(boss_snap_t) + sizeof(boss_brain_msg_t) + 4 + 2 * FRAME_MAP_CELLS * 3];
    boss_snap_t snap;
    boss_brain_msg_t head;
    uint8_t flags = 0;
    boss_snapshot(&boss, &snap);
    memcpy(buf, &snap, sizeof snap);
    memset(&head, 0, sizeof head);
    for (int g = 0; stim && rate && g < BRAIN_GROUP_COUNT && g < PROTO_GROUPS; g++) {
        float s = stim[g] < 0.0f ? 0.0f : (stim[g] > 1.0f ? 1.0f : stim[g]);
        float r = rate[g] < 0.0f ? 0.0f : (rate[g] > 1.0f ? 1.0f : rate[g]);
        head.stim[g] = (uint8_t)(s * 255.0f);
        head.rate[g] = (uint8_t)(r * 255.0f);
    }
    float t = boss.cfg.takeoff_level < 0.0f ? 0.0f : (boss.cfg.takeoff_level > 1.0f ? 1.0f : boss.cfg.takeoff_level);
    head.takeoff = (uint8_t)(t * 255.0f);
    size_t len = boss_brain_pack(buf + sizeof snap, sizeof buf - sizeof snap, &head, stim ? brain_host_map() : NULL, &flags);
    if (len == 0) flags = 0;                         /* 뇌 부분을 만들지 못했으면 상태만 보낸다 */
    for (int i = 0; i < MAX_CONN; i++)
        if (members[i].in) conn_post(i, members[i].gen, MSG_BOSS, flags, buf, (uint32_t)(sizeof snap + len), 1);
}

static void start_boss(void)
{
    boss_cfg_t c;
    int n = 0, lives[BOSS_PLAYERS];
    stage_kill();
    boss_cfg_load(&c, "boss/config.ini");
    if (opt.boss_sec > 0) c.ticks = opt.boss_sec * 20;
    for (int s = 0; s < PROTO_SEATS; s++) {
        seats[s].passed = seats[s].score = 0;
        seats[s].pane = -1;
        if (seats[s].used && seats[s].connected && seats[s].lives > 0 && n < BOSS_PLAYERS) {
            seats[s].pane = n;
            /* 남은 기회를 그대로 체력으로 주면 한두 판 진 사람들로는 이길 수 없었다(봇으로 3명이 2개씩일 때 승률 2.5%,
             * 3개씩일 때 55%). 그래서 기회는 누가 보스전에 가는지만 정하고 체력은 모두 같게 준다 */
            lives[n++] = BOSS_START_HP;
        }
    }
    if (n == 0) { finish_game(); return; }
    boss_over = 0;
    boss_init(&boss, &c, game_seed ^ 0xB055u, n, lives);
    brain_host_reset(game_seed, boss_brain);
    memset(&st.info, 0, sizeof st.info);
    snprintf(st.info.title, sizeof st.info.title, "보스전");
    fly_score = boss.fly.hp;
    fprintf(stderr, "서버: 보스전 시작 (참가자 %d명, %d틱, 초파리 체력 %d)\n", n, c.ticks, boss.fly.hp);
    phase = PH_BOSS;
    /* 첫 틱을 BOSS_INTRO_MS 뒤로 미룬다. 그 전에 처음 상태(tick 0)를 보내 두면 클라이언트가 키 안내를 띄운다 */
    deadline = now_ms() + (opt.fast ? 100 : BOSS_INTRO_MS);
    send_phase();
    boss_send(NULL, NULL);
}

static void boss_step(void)
{
    float stim[BRAIN_GROUP_COUNT], rate[BRAIN_GROUP_COUNT];
    brain_host_begin_tick();
    boss_stimulus(&boss, stim);
    for (int g = 0; g < BRAIN_GROUP_COUNT; g++) if (stim[g] > 0.0f) brain_stimulate((brain_group_t)g, stim[g]);
    for (int g = 0; g < BRAIN_GROUP_COUNT; g++) rate[g] = brain_rate((brain_group_t)g);
    brain_host_end_tick();
    boss_tick(&boss, rate);
    boss_send(stim, rate);
    /* 승패가 났으면 여기서 멈춘다. 방금 보낸 상태에 결과가 들어 있어서 클라이언트가 그것을 그린다.
     * 점수를 매기고 랭킹으로 넘어가는 일(finish_boss)은 BOSS_END_MS 뒤에 한다 */
    if (boss.outcome != BOSS_GOING) boss_over = 1;
}

static void on_stage_readable(void)
{
    msg_hdr_t h;
    int r = msg_recv_some(st.from, &st.part, stage_buf, sizeof stage_buf);
    if (r == 0) return;                          /* 덜 왔다. 나머지는 다음에 이어 받는다 */
    h = st.part.h;
    if (r != 1) {
        if (!st.got_result) stage_failed("결과 없이 끝났습니다");
        return;
    }
    if (h.type == MSG_INFO && h.len == sizeof st.info) {
        memcpy(&st.info, stage_buf, sizeof st.info);
        st.info.title[sizeof st.info.title - 1] = st.info.rule[sizeof st.info.rule - 1] = '\0';
        st.info.controls[sizeof st.info.controls - 1] = '\0';
        st.got_info = 1;
        send_phase();
    } else if (h.type == MSG_FRAME) {
        st.awaiting = 0;
        for (int i = 0; i < MAX_CONN; i++)
            if (members[i].in) conn_post(i, members[i].gen, MSG_FRAME, h.flags, stage_buf, h.len, 1);
        if (opt.fast && !opt.tick_ms && phase == PH_PLAY) deadline = now_ms();   /* 검사용: 기다리지 않고 다음 틱 */
    } else if (h.type == MSG_RESULT && h.len == sizeof st.result) {
        memcpy(&st.result, stage_buf, sizeof st.result);
        st.got_result = 1;
        enter_verdict();
    }
}

static void on_deadline(void)
{
    int64_t now = now_ms();
    switch (phase) {
    case PH_CARD:
        phase = PH_READY;
        deadline = now + opt.ready_ms;
        send_phase();
        break;
    case PH_READY:
        phase = PH_PLAY;
        deadline = now;
        st.wait_since = now;
        send_phase();
        break;
    case PH_PLAY:
        if (!st.got_info || st.awaiting) {
            /* 스테이지가 아직 답하지 않았다. 너무 오래 걸리면 죽인다 */
            if (now - st.wait_since > STAGE_WAIT_MS) { stage_failed("답하지 않습니다"); return; }
        } else if (st.ticks_sent < st.info.n_ticks) {
            /* 못 보냈으면(통로가 가득 찼거나 스테이지가 닫았다) 다음에 다시 보낸다. 스테이지가 끝난 것이면
             * 읽는 쪽에서 알게 되고, 못 보내는 상태가 오래가면 멈춘 것으로 본다 */
            int sent = msg_send_once(st.to, MSG_TICK, 0, 0, NULL, 0);
            if (sent <= 0 && now - st.wait_since > STAGE_WAIT_MS) { stage_failed("답하지 않습니다"); return; }
            if (sent > 0) {
                st.ticks_sent++;
                st.awaiting = 1;
                st.wait_since = now;
            }
        } else if (now - st.wait_since > STAGE_WAIT_MS) {
            stage_failed("결과를 보내지 않습니다");
            return;
        }
        deadline = opt.tick_ms > 0 ? now + opt.tick_ms : (opt.fast ? now + 5 : deadline + TICK_MS);
        if (deadline < now - 500) deadline = now;        /* 많이 밀렸으면 따라잡으려 하지 않는다 */
        break;
    case PH_VERDICT: {
        int alive = 0;
        round_no++;
        for (int s = 0; s < PROTO_SEATS; s++)
            if (seats[s].used && seats[s].connected && seats[s].lives > 0) alive++;
        if (alive == 0) finish_game();
        else if (round_no >= opt.n_rounds) { if (opt.boss_sec != 0) start_boss(); else finish_game(); }
        else start_round();
        break;
    }
    case PH_BOSS:
        if (boss_over) { finish_boss(); break; }         /* 결과를 보여 준 시간이 지났다. 랭킹으로 간다 */
        boss_step();
        if (boss_over) { deadline = now + (opt.fast ? 100 : BOSS_END_MS); break; }
        deadline = opt.tick_ms > 0 ? now + opt.tick_ms : (opt.fast ? now : deadline + TICK_MS);
        if (deadline < now - 500) deadline = now;
        break;
    case PH_RANK:
        to_lobby();
        break;
    default:
        deadline = 0;
        break;
    }
}

static void on_join(const event_t *e)
{
    members[e->slot].in = 1;
    members[e->slot].gen = e->gen;
    members[e->slot].seat = -1;
    members[e->slot].join_no = ++join_counter;
    memcpy(members[e->slot].name, e->name, PROTO_NAME_LEN);
    /* 판이 진행 중일 때 들어온 사람은 관전자다. 로비에서는 빈 자리에 앉는다 */
    if (phase == PH_LOBBY) fill_seats();
    send_top(e->slot);
    send_phase();
}

static void on_leave(const event_t *e)
{
    if (!members[e->slot].in || members[e->slot].gen != e->gen) return;
    int s = members[e->slot].seat, left = 0;
    members[e->slot].in = 0;
    if (s >= 0) {
        if (phase == PH_LOBBY) seats[s].used = 0;
        else seats[s].connected = 0;                 /* 라운드는 계속한다 */
        if (phase == PH_BOSS && seats[s].pane >= 0)      /* 보스전에서 나간 사람은 쓰러진 것으로 한다 */
            boss.p[seats[s].pane].alive = boss.p[seats[s].pane].hp = 0;
    }
    for (int k = 0; k < PROTO_SEATS; k++) left += seats[k].used && seats[k].connected;
    if (phase != PH_LOBBY && left == 0) { to_lobby(); return; }      /* 참가자가 모두 나갔다 */
    if (phase == PH_LOBBY) fill_seats();
    send_phase();
}

static void on_input(const event_t *e)
{
    if (!members[e->slot].in || members[e->slot].gen != e->gen) return;
    int s = members[e->slot].seat;
    if (s < 0) return;                               /* 관전자의 입력은 쓰지 않는다 */
    in_event_t ie = { .kind = e->in.kind, .flags = e->in.flags, .code = e->in.code, .x = e->in.x, .y = e->in.y };

    if (phase == PH_LOBBY && ie.kind == IN_KEY) {
        int key = input_stage_key(&ie);
        if (key == SK_ACT) {
            seats[s].ready = !seats[s].ready;
            send_phase();
        } else if (key == SK_SUB && s == host_seat()) {
            /* 방장이 누르면 시작한다. 앉아 있는 사람이 모두 준비해야 한다 */
            int all = 1;
            for (int k = 0; k < PROTO_SEATS; k++)
                if (seats[k].used && seats[k].connected && !seats[k].ready) all = 0;
            if (all) start_game();
        }
    } else if (phase == PH_PLAY && seats[s].pane >= 0 && st.open) {
        input_msg_t out = { .kind = ie.kind, .flags = ie.flags, .player = (uint8_t)seats[s].pane };
        if (ie.kind == IN_KEY) {
            int key = input_stage_key(&ie);
            if (key == SK_NONE) return;
            out.code = (uint16_t)key;
        } else if (ie.kind == IN_POINTER) {
            int px, py;
            if (!compose_my_pane_xy(ie.x, ie.y, &px, &py)) return;
            out.x = (int16_t)px;
            out.y = (int16_t)py;
        } else {
            return;
        }
        /* 못 보낸 입력은 버린다. 통로가 가득 찼거나 스테이지가 이미 끝낸 것이다. 여기서 라운드를 무효로
         * 하면 스테이지가 끝내면서 보낸 결과까지 버리게 된다. 결과 없이 끝난 것인지는 읽는 쪽이 가린다 */
        (void)msg_send_once(st.to, MSG_INPUT, 0, 0, &out, sizeof out);
    } else if (phase == PH_BOSS && seats[s].pane >= 0 && ie.kind == IN_KEY) {
        /* 보스전의 키. A와 D는 옆걸음이고 방향키 좌우는 도는 키다(마우스가 없을 때와 봇이 쓴다).
         * C는 살금살금을 켜고 끈다. R은 막기, 스페이스는 뛰어오르기, E는 무기 쓰기다(마우스 왼쪽 버튼도 E로 온다).
         * Shift와 함께 누른 이동은 그 걸음만 살금살금이다 */
        int key;
        switch (ie.code) {
        case 'a': case 'A': key = SK_LEFT | BOSS_STRAFE; break;
        case 'd': case 'D': key = SK_RIGHT | BOSS_STRAFE; break;
        case 'c': case 'C': key = BOSS_SNEAK; break;
        case 'r': case 'R': key = BOSS_GUARD; break;
        case ' ':           key = BOSS_JUMP; break;
        case 'e': case 'E': key = SK_ACT; break;
        default:            key = input_stage_key(&ie); break;
        }
        if (key == SK_NONE) return;
        if ((ie.flags & IN_F_SHIFT) && key != BOSS_SNEAK && key != BOSS_GUARD && key != BOSS_JUMP) key |= BOSS_SLOW;
        boss_input(&boss, seats[s].pane, key);
    } else if (phase == PH_BOSS && seats[s].pane >= 0 && ie.kind == INPUT_LOOK) {
        /* 보스전의 시선. 클라이언트가 마우스로 정한 방향을 통째로 알려 온다 */
        boss_look(&boss, seats[s].pane, (float)ie.code * (6.2831853f / 65536.0f));
    }
}

static void game_master(void)
{
    for (;;) {
        struct pollfd pf[2] = { { evfd, POLLIN, 0 }, { st.open ? st.from : -1, POLLIN, 0 } };
        int timeout = -1;
        if (deadline) {
            int64_t left = deadline - now_ms();
            timeout = left < 0 ? 0 : (int)left;
        }
        int brain_left = brain_sup_wait_ms();        /* 뇌를 다시 띄우기로 한 시각이 있으면 그때까지만 */
        if (brain_left >= 0 && (timeout < 0 || brain_left < timeout)) timeout = brain_left;
        if (poll(pf, 2, timeout) < 0 && errno != EINTR) { perror("poll"); exit(1); }

        if (pf[0].revents) {
            uint64_t n;
            event_t e;
            if (read(evfd, &n, sizeof n) < 0) { /* 다른 깨움이 이미 읽었다 */ }
            while (ev_pop(&e)) {
                if (e.type == EV_JOIN) on_join(&e);
                else if (e.type == EV_LEAVE) on_leave(&e);
                else if (e.type == EV_INPUT) on_input(&e);
                else if (e.type == EV_BRAIN) brain_sup_stalled((uint32_t)e.gen);
                else if (e.signo == SIGCHLD) reap_children();
                else if (e.signo == SIGINT || e.signo == SIGTERM) { stage_kill(); exit(0); }
            }
        }
        /* 사건을 처리하다 스테이지가 바뀌었을 수 있으므로 다시 확인하고 읽는다 */
        if (st.open) {
            struct pollfd sp = { st.from, POLLIN, 0 };
            if (poll(&sp, 1, 0) > 0 && sp.revents) on_stage_readable();
        }
        if (deadline && now_ms() >= deadline) on_deadline();
        brain_sup_tick();
    }
}

int main(int argc, char **argv)
{
    int brain_inproc = 0, brain_strict = 0;
    setvbuf(stdout, NULL, _IOLBF, 0);
    for (int i = 1; i < argc; i++) {
        const char *a = argv[i];
        if (strncmp(a, "--port=", 7) == 0)         opt.port = atoi(a + 7);
        else if (strncmp(a, "--rounds=", 9) == 0)  opt.n_rounds = atoi(a + 9);
        else if (strncmp(a, "--seed=", 7) == 0)    opt.seed = (uint32_t)strtoul(a + 7, NULL, 10);
        else if (strncmp(a, "--ranking=", 10) == 0) ranking_path = a + 10;
        else if (strncmp(a, "--brain=", 8) == 0)   opt.brain = a + 8;
        else if (strcmp(a, "--fast") == 0)         opt.fast = 1;
        else if (strcmp(a, "--once") == 0)         opt.once = 1;
        else if (strncmp(a, "--tick-ms=", 10) == 0) opt.tick_ms = atoi(a + 10);
        else if (strncmp(a, "--ticks=", 8) == 0)   opt.ticks = atoi(a + 8);
        else if (strncmp(a, "--boss=", 7) == 0)    opt.boss_sec = atoi(a + 7);
        else if (strncmp(a, "--idle-ms=", 10) == 0) opt.idle_ms = atoi(a + 10);
        else if (strcmp(a, "--brain-inproc") == 0) brain_inproc = 1;
        else if (strcmp(a, "--brain-strict") == 0) brain_strict = 1;
        else if (strncmp(a, "--order=", 8) == 0) {
            const char *p = a + 8;
            while (*p && opt.n_order < MAX_ORDER) {
                size_t len = strcspn(p, ",");
                if (len > 0 && len < 32) { memcpy(opt.order[opt.n_order], p, len); opt.order[opt.n_order++][len] = '\0'; }
                p += len;
                if (*p == ',') p++;
            }
        } else {
            printf("모르는 옵션: %s\n", a);
            return 2;
        }
    }
    if (opt.n_order == 0) { snprintf(opt.order[0], 32, "example"); opt.n_order = 1; }
    if (opt.n_rounds < 1) opt.n_rounds = 1;
    if (opt.fast) opt.card_ms = opt.ready_ms = opt.verdict_ms = opt.rank_ms = 100;

    /* 규칙 1: 스레드를 만들기 전에 시그널을 막는다. 새 스레드는 이 마스크를 물려받는다 */
    static sigset_t set;
    sigemptyset(&set);
    sigaddset(&set, SIGCHLD);
    sigaddset(&set, SIGALRM);
    sigaddset(&set, SIGINT);
    sigaddset(&set, SIGTERM);
    pthread_sigmask(SIG_BLOCK, &set, NULL);
    signal(SIGPIPE, SIG_IGN);

    evfd = eventfd(0, EFD_CLOEXEC | EFD_NONBLOCK);
    for (int i = 0; i < MAX_CONN; i++) {
        pthread_mutex_init(&conns[i].mu, NULL);
        pthread_cond_init(&conns[i].cv, NULL);
    }
    for (int s = 0; s < PROTO_SEATS; s++) seats[s].pane = -1;

    static int lfd;
    lfd = socket(AF_INET, SOCK_STREAM | SOCK_CLOEXEC, 0);
    int one = 1;
    setsockopt(lfd, SOL_SOCKET, SO_REUSEADDR, &one, sizeof one);
    struct sockaddr_in addr = { .sin_family = AF_INET, .sin_port = htons((uint16_t)opt.port),
                                .sin_addr = { htonl(INADDR_ANY) } };
    if (evfd < 0 || lfd < 0 || bind(lfd, (struct sockaddr *)&addr, sizeof addr) != 0 || listen(lfd, 16) != 0) {
        perror("서버를 열 수 없습니다");
        return 1;
    }

    /* 뇌 프로세스와 감시 스레드. 환경 변수를 바꾸므로 다른 스레드를 만들기 전에 한다 */
    if (!brain_inproc && brain_sup_start(opt.brain, opt.fast, brain_strict, brain_stall_event) != 0) return 1;
    if (brain_inproc) {                              /* 보스전의 뇌를 서버 안에서 계산한다 */
        if (strcmp(opt.brain, "slow") == 0) boss_brain = BRAIN_FAKE_SLOW;
        else if (strcmp(opt.brain, "real") == 0 && brain_host_open_real("data") == 0) boss_brain = BRAIN_REAL;
    }

    pthread_t t_sig, t_acc;
    pthread_create(&t_sig, NULL, signal_main, &set);
    pthread_create(&t_acc, NULL, accept_main, &lfd);
    printf("서버: 포트 %d에서 기다립니다 (스테이지 %d종, %d라운드)\n", opt.port, opt.n_order, opt.n_rounds);

    game_master();
    return 0;
}
