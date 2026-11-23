/* 서버. 접속을 받고, 게임의 단계를 진행하고, 라운드마다 스테이지 프로세스를 띄운다.
 *
 *   build/dev/server [--port=N] [--order=이름,이름,...] [--rounds=N] [--seed=N]
 *                    [--brain=fast|slow|real] [--fast] [--once]
 *
 * 스레드
 *   시그널 스레드        시그널을 기다렸다가 사건으로 넘긴다
 *   accept 스레드        접속을 받는다
 *   접속자 읽기 스레드   메시지를 읽어 사건으로 넘긴다 (접속마다)
 *   접속자 쓰기 스레드   보낼 것을 꺼내 쓴다 (접속마다). 소켓 하나에 쓰는 스레드는 이것 하나다
 *   게임 마스터(main)    사건을 하나씩 처리한다. 게임 상태, 자식 프로세스, 파이프는 이 스레드만 만진다
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
 * 아직 없는 것: 보스전(4라운드 뒤 바로 랭킹으로 간다), 뇌 프로세스, 랭킹 저장. */
#include "core/msg.h"
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
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

#define MAX_CONN     16
#define MAX_QUEUED   256         /* 접속자 한 명에게 밀린 메시지가 이보다 많으면 그 접속만 끊는다 */
#define MAX_ORDER    8
#define TICK_MS      50
#define STAGE_WAIT_MS 2000       /* 스테이지가 이만큼 답하지 않으면 죽인다 */

/* ---- 옵션 ------------------------------------------------------------------ */
static struct {
    int port, n_rounds, fast, once;
    uint32_t seed;
    const char *brain;
    char order[MAX_ORDER][32];
    int n_order;
    int card_ms, ready_ms, verdict_ms, rank_ms;
} opt = { PROTO_PORT, 4, 0, 0, 0, "fast", {{0}}, 0, 3000, 2000, 2000, 6000 };

static int64_t now_ms(void)
{
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return (int64_t)t.tv_sec * 1000 + t.tv_nsec / 1000000;
}

/* ---- 사건 큐: 다른 스레드가 넣고 게임 마스터가 꺼낸다 ------------------------- */
enum { EV_JOIN, EV_LEAVE, EV_INPUT, EV_SIGNAL };
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
    if (next != evq_head) {                      /* 꽉 차면 버린다 */
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

static void *reader_main(void *arg)
{
    conn_t *c = arg;
    int slot = (int)(c - conns), gen = c->gen, one = 1, writer_up = 0;
    pthread_t writer;
    msg_hdr_t h;
    static __thread uint8_t body[4096];
    setsockopt(c->fd, IPPROTO_TCP, TCP_NODELAY, &one, sizeof one);

    /* 첫 메시지는 HELLO여야 한다 */
    hello_msg_t hello;
    if (msg_recv(c->fd, &h, body, sizeof body) != 1 || h.type != MSG_HELLO || h.len != sizeof hello) goto out;
    memcpy(&hello, body, sizeof hello);
    if (hello.version != PROTO_VERSION) {
        const char *why = "version mismatch";
        msg_send(c->fd, MSG_REJECT, 0, 0, why, (uint32_t)strlen(why));
        goto out;
    }
    hello.name[PROTO_NAME_LEN - 1] = '\0';
    if (pthread_create(&writer, NULL, writer_main, c) != 0) goto out;
    writer_up = 1;

    welcome_msg_t wm = { PROTO_VERSION, (uint16_t)slot };
    conn_post(slot, gen, MSG_WELCOME, 0, &wm, sizeof wm, 0);
    event_t join = { .type = EV_JOIN, .slot = slot, .gen = gen };
    snprintf(join.name, sizeof join.name, "%s", hello.name[0] ? hello.name : "PLAYER");
    ev_push(&join);

    while (msg_recv(c->fd, &h, body, sizeof body) == 1) {
        if (h.type == MSG_INPUT && h.len == sizeof(input_msg_t)) {
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
    c->used = 0;
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
            c->used = 0;
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
    while ((pid = waitpid(-1, &status, WNOHANG)) > 0)
        if (pid == st.pid) st.pid = 0;
}

static int stage_spawn(const char *name, uint32_t seed, int n_players)
{
    char path[128], a_pipe[48], a_seed[32], a_players[32], a_brain[32], log_path[128];
    int to_child[2], from_child[2];

    memset(&st, 0, sizeof st);
    st.to = st.from = -1;
    if (pipe2(to_child, O_CLOEXEC) != 0) return -1;
    if (pipe2(from_child, O_CLOEXEC) != 0) { close(to_child[0]); close(to_child[1]); return -1; }

    /* fork와 exec 사이에는 안전한 함수만 부를 수 있으므로 글자열은 미리 만든다 */
    snprintf(path, sizeof path, "build/dev/stage_%s", name);
    snprintf(a_pipe, sizeof a_pipe, "--pipe=%d,%d", to_child[0], from_child[1]);
    snprintf(a_seed, sizeof a_seed, "--seed=%u", seed);
    snprintf(a_players, sizeof a_players, "--players=%d", n_players);
    snprintf(a_brain, sizeof a_brain, "--brain=%s", opt.brain);
    snprintf(log_path, sizeof log_path, "build/logs/stage_%s.log", name);
    char *argv[] = { path, a_pipe, a_seed, a_players, a_brain, NULL };
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

/* 4라운드가 끝났다. 보스전은 아직 없어서 바로 랭킹으로 간다. */
static void finish_game(void)
{
    stage_kill();
    game_over = 1;
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
    for (int s = 0; s < PROTO_SEATS; s++) {
        if (!seats[s].used || seats[s].pane < 0) continue;
        seats[s].score = st.result.score[seats[s].pane];
        /* 같거나 높으면 이긴 것이다. 도중에 나간 사람은 진 것으로 한다 */
        seats[s].passed = seats[s].connected && seats[s].score >= st.result.fly_score;
        if (!seats[s].passed) seats[s].lives--;
        seats[s].total += seats[s].score;
    }
    fly_score = st.result.fly_score;
    stage_close();                               /* 스테이지는 스스로 끝난다. 거두는 것은 SIGCHLD에서 */
    phase = PH_VERDICT;
    deadline = now_ms() + opt.verdict_ms;
    send_phase();
}

static void on_stage_readable(void)
{
    msg_hdr_t h;
    int r = msg_recv(st.from, &h, stage_buf, sizeof stage_buf);
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
        if (opt.fast && phase == PH_PLAY) deadline = now_ms();       /* 검사용: 기다리지 않고 다음 틱 */
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
            if (msg_send(st.to, MSG_TICK, 0, 0, NULL, 0) != 0) { stage_failed("통로를 닫았습니다"); return; }
            st.ticks_sent++;
            st.awaiting = 1;
            st.wait_since = now;
        } else if (now - st.wait_since > STAGE_WAIT_MS) {
            stage_failed("결과를 보내지 않습니다");
            return;
        }
        deadline = (opt.fast ? now + 5 : deadline + TICK_MS);
        if (deadline < now - 500) deadline = now;        /* 많이 밀렸으면 따라잡으려 하지 않는다 */
        break;
    case PH_VERDICT: {
        int alive = 0;
        round_no++;
        for (int s = 0; s < PROTO_SEATS; s++)
            if (seats[s].used && seats[s].connected && seats[s].lives > 0) alive++;
        if (round_no >= opt.n_rounds || alive == 0) finish_game();
        else start_round();
        break;
    }
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
        if (msg_send(st.to, MSG_INPUT, 0, 0, &out, sizeof out) != 0) stage_failed("통로를 닫았습니다");
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
        if (poll(pf, 2, timeout) < 0 && errno != EINTR) { perror("poll"); exit(1); }

        if (pf[0].revents) {
            uint64_t n;
            event_t e;
            if (read(evfd, &n, sizeof n) < 0) { /* 다른 깨움이 이미 읽었다 */ }
            while (ev_pop(&e)) {
                if (e.type == EV_JOIN) on_join(&e);
                else if (e.type == EV_LEAVE) on_leave(&e);
                else if (e.type == EV_INPUT) on_input(&e);
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
    }
}

int main(int argc, char **argv)
{
    setvbuf(stdout, NULL, _IOLBF, 0);
    for (int i = 1; i < argc; i++) {
        const char *a = argv[i];
        if (strncmp(a, "--port=", 7) == 0)         opt.port = atoi(a + 7);
        else if (strncmp(a, "--rounds=", 9) == 0)  opt.n_rounds = atoi(a + 9);
        else if (strncmp(a, "--seed=", 7) == 0)    opt.seed = (uint32_t)strtoul(a + 7, NULL, 10);
        else if (strncmp(a, "--brain=", 8) == 0)   opt.brain = a + 8;
        else if (strcmp(a, "--fast") == 0)         opt.fast = 1;
        else if (strcmp(a, "--once") == 0)         opt.once = 1;
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

    pthread_t t_sig, t_acc;
    pthread_create(&t_sig, NULL, signal_main, &set);
    pthread_create(&t_acc, NULL, accept_main, &lfd);
    printf("서버: 포트 %d에서 기다립니다 (스테이지 %d종, %d라운드)\n", opt.port, opt.n_order, opt.n_rounds);

    game_master();
    return 0;
}
