/* 실패 상황 검사에 쓰는 접속자. 서버에 여러 방식으로 접속해 보고 결과를 줄로 찍는다.
 * 잘못 구는 가짜 스테이지 노릇도 한다(아래).
 * tests/fail.sh가 빌드해서 쓴다. 통신 규격(protocol.h)만 쓰고 다른 코드는 링크하지 않는다.
 *
 *   build/dev/tests/fail_peer --port=N --mode=이름 [--name=이름] [--sec=초] [--n=개수] [--rcvbuf=바이트]
 *                             [--until=단계] [--after=ms]
 *
 *   watch     들어가서 준비하고 받기만 한다. 단계가 바뀔 때(PHASE)와 도중에 누가 나갈 때(SEATS) 한 줄씩 찍는다.
 *             --until의 단계(기본 RANK)로 넘어가는 것을 보면 0으로 끝난다
 *   flood     watch와 같되 게임 중에 입력을 쉬지 않고 보낸다
 *   probe     watch와 같되 게임 중에 --after(ms)마다 키를 하나 보낸다. 판정 때 "PROBE sent=보낸 수 got=점수"를
 *             찍는다. stage_t_count와 같이 쓰면 점수가 스테이지에 닿은 내 입력의 수다
 *   drip      HELLO를 --after(ms)마다 한 바이트씩 보낸다. --n개를 한꺼번에 연다. 모두 끊기면
 *             "DRIP n=.. closed=.. max_ms=.." 를 찍고 0, --sec 안에 다 끊기지 않으면 3
 *   slow      HELLO와 준비 키만 보내고 --sec 동안 아무것도 읽지 않는다. 항상 0
 *   stuck     slow처럼 읽지 않다가 --after(기본 1500ms) 뒤에 len이 아주 큰 머리를 보낸다. 항상 0
 *   silent    접속만 하고 아무것도 보내지 않는다. 서버가 끊으면 0
 *   version   틀린 버전으로 HELLO를 보낸다. 거절 메시지를 받고 끊기면 0
 *   hugelen   len이 아주 큰 머리를 보낸다. 끊기면 0
 *   garbage   뜻 없는 바이트를 보낸다. 끊기면 0
 *   late      HELLO 뒤에 len이 아주 큰 머리를 보낸다. 끊기면 0
 *   half      HELLO를 절반만 보내고 닫는다. 항상 0
 *   cut       HELLO 뒤에 INPUT을 절반만 보내고 닫는다. 항상 0
 *   fill      접속을 n개 열어 HELLO를 보내고, 받아 준 수와 거절된 수를 찍은 뒤 --sec 동안 붙들고 있는다
 *
 * 가짜 스테이지: 이 파일을 build/dev/stage_t_<종류>로 복사해 두면 서버가 --pipe=R,W를 주어 띄운다.
 *   crash     뜨자마자 죽는다
 *   partial   INFO 뒤에 다음 메시지의 머리를 절반만 보내고 멈춘다
 *   win       60틱 동안 작은 FRAME으로 답하고, 참가자가 모두 이기는 RESULT를 보낸다
 *   early     1틱 뒤 읽는 쪽을 먼저 닫고, 0.3초 뒤에 RESULT를 보낸다
 *   bigscore  1틱 뒤 점수가 모두 2147483647인 RESULT를 보내고, 끝나지 않고 남는다
 *   count     150틱 동안 작은 FRAME으로 답하면서 참가자별로 받은 INPUT을 세고, 그 수를 점수로 보낸다
 *
 * 끝나는 값: 0 기대한 대로, 1 접속이 끊겼거나 기대와 다름, 2 사용법, 3 시간 초과 */
#include "protocol.h"

#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <poll.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <time.h>
#include <unistd.h>

#define MAX_FILL 64

static int port = PROTO_PORT, n_conn = 20, rcvbuf = 0, after_ms = 1500;
static double limit_sec = 20.0;
static const char *mode = "", *name = "PEER", *until = "RANK";

static int64_t now_ms(void)
{
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return (int64_t)t.tv_sec * 1000 + t.tv_nsec / 1000000;
}

static int dial(void)
{
    int fd = socket(AF_INET, SOCK_STREAM, 0), one = 1;
    struct sockaddr_in addr = { .sin_family = AF_INET, .sin_port = htons((uint16_t)port),
                                .sin_addr = { htonl(INADDR_LOOPBACK) } };
    if (fd < 0) return -1;
    if (rcvbuf > 0) setsockopt(fd, SOL_SOCKET, SO_RCVBUF, &rcvbuf, sizeof rcvbuf);   /* 접속 전에 정해야 한다 */
    if (connect(fd, (struct sockaddr *)&addr, sizeof addr) != 0) { close(fd); return -1; }
    setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &one, sizeof one);
    return fd;
}

/* 다 쓴다. 소켓과 파이프에 같이 쓴다(SIGPIPE는 무시해 두었다). 상대가 끊었으면 -1. */
static int put(int fd, const void *buf, size_t n)
{
    const char *p = buf;
    while (n > 0) {
        ssize_t w = write(fd, p, n);
        if (w < 0) {
            if (errno == EINTR) continue;
            return -1;
        }
        p += w;
        n -= (size_t)w;
    }
    return 0;
}

static int put_msg(int fd, uint8_t type, const void *body, uint32_t len)
{
    uint8_t buf[64];
    msg_hdr_t h = { len, type, 0, 0 };
    if (len > sizeof buf - sizeof h) return -1;
    memcpy(buf, &h, sizeof h);
    if (len > 0) memcpy(buf + sizeof h, body, len);
    return put(fd, buf, sizeof h + len);
}

static int put_hello(int fd, uint16_t version)
{
    hello_msg_t hello = { .version = version };
    snprintf(hello.name, sizeof hello.name, "%s", name);
    return put_msg(fd, MSG_HELLO, &hello, sizeof hello);
}

static int put_key(int fd, uint16_t code)
{
    input_msg_t m = { .kind = 1, .code = code };
    return put_msg(fd, MSG_INPUT, &m, sizeof m);
}

/* ---- 받는 쪽: 온 만큼 모아 두고 메시지 단위로 꺼낸다 ---------------------------- */
static uint8_t rb[PROTO_MAX_LEN + 4096];
static size_t rb_len;

/* 온 것을 더 읽는다. 1: 읽었다, 0: 지금은 없다, -1: 끊겼다. */
static int pull(int fd)
{
    ssize_t r = recv(fd, rb + rb_len, sizeof rb - rb_len, MSG_DONTWAIT);
    if (r > 0) { rb_len += (size_t)r; return 1; }
    if (r < 0 && (errno == EAGAIN || errno == EINTR)) return 0;
    return -1;
}

/* 다 온 메시지가 있으면 꺼낸다. body는 다음 pull 전까지만 쓸 수 있다. */
static int take(msg_hdr_t *h, uint8_t *body, size_t cap)
{
    if (rb_len < sizeof *h) return 0;
    memcpy(h, rb, sizeof *h);
    if (h->len > PROTO_MAX_LEN) { rb_len = 0; return 0; }
    size_t all = sizeof *h + h->len;
    if (rb_len < all) return 0;
    memcpy(body, rb + sizeof *h, h->len < cap ? h->len : cap);
    memmove(rb, rb + all, rb_len - all);
    rb_len -= all;
    return 1;
}

/* ---- watch, flood ------------------------------------------------------------- */
static const char *phase_name(int ph)
{
    static const char *names[] = { "LOBBY", "CARD", "READY", "PLAY", "VERDICT", "BOSS", "RANK" };
    return (ph >= 0 && ph <= PH_RANK) ? names[ph] : "?";
}

/* flood: 0 받기만, 1 입력을 쉬지 않고, 2 after_ms마다 하나씩(probe) */
static int run_watch(int flood)
{
    static uint8_t body[sizeof(phase_msg_t)];
    int fd = dial(), last_phase = -1, last_round = -1, last_seated = 0, sent_ready = 0;
    long frames = 0, total_frames = 0, sent = 0, bytes = 0, probed = 0;
    int64_t t0 = now_ms(), last_frame = 0, gap = 0, next_probe = 0;
    if (fd < 0 || put_hello(fd, PROTO_VERSION) != 0) { printf("END 접속 실패\n"); return 1; }

    for (;;) {
        int64_t now = now_ms();
        if ((double)(now - t0) > limit_sec * 1000.0) { printf("END 시간 초과 frames=%ld\n", total_frames); return 3; }
        if (flood == 2 && last_phase == PH_PLAY && now >= next_probe) {
            if (put_key(fd, 'a') != 0) { printf("END 접속이 끊겼습니다 probed=%ld\n", probed); return 1; }
            probed++;
            next_probe = now + after_ms;
        }
        struct pollfd pf = { fd, POLLIN, 0 };
        if (flood == 1 && last_phase == PH_PLAY) pf.events |= POLLOUT;
        int wait_ms = 100;
        if (flood == 2 && last_phase == PH_PLAY) wait_ms = next_probe > now ? (int)(next_probe - now) : 0;
        if (poll(&pf, 1, wait_ms) < 0) continue;

        if (pf.revents & POLLOUT) {
            /* 한 번에 여러 개를 붙여 보낸다. 막히면 그만둔다 */
            uint8_t batch[64 * (sizeof(msg_hdr_t) + sizeof(input_msg_t))];
            size_t o = 0;
            while (o < sizeof batch) {
                msg_hdr_t h = { sizeof(input_msg_t), MSG_INPUT, 0, 0 };
                input_msg_t m = { .kind = 1, .code = (uint16_t)((sent & 1) ? 'a' : 'd') };
                memcpy(batch + o, &h, sizeof h);
                memcpy(batch + o + sizeof h, &m, sizeof m);
                o += sizeof h + sizeof m;
                sent++;
            }
            if (put(fd, batch, o) != 0) { printf("END 접속이 끊겼습니다 sent=%ld\n", sent); return 1; }
        }
        if (!(pf.revents & (POLLIN | POLLHUP | POLLERR))) continue;
        if (pull(fd) < 0) { printf("END 접속이 끊겼습니다 frames=%ld\n", total_frames); return 1; }

        msg_hdr_t h;
        while (take(&h, body, sizeof body)) {
            now = now_ms();
            if (h.type == MSG_FRAME) {
                if (frames > 0 && now - last_frame > gap) gap = now - last_frame;
                last_frame = now;
                frames++;
                total_frames++;
                bytes += (long)h.len;
                continue;
            }
            if (h.type != MSG_PHASE || h.len != sizeof(phase_msg_t)) continue;
            phase_msg_t ph;
            memcpy(&ph, body, sizeof ph);
            if (ph.phase == PH_LOBBY && last_phase != PH_LOBBY) sent_ready = 0;
            if (ph.phase == PH_LOBBY && ph.me >= 0 && ph.me < PROTO_SEATS && !ph.ready[ph.me] && !sent_ready) {
                put_key(fd, ' ');
                sent_ready = 1;
            }
            int seated = 0;
            for (int s = 0; s < PROTO_SEATS; s++) {
                ph.name[s][PROTO_NAME_LEN - 1] = '\0';
                if (ph.seated[s]) seated |= 1 << s;
            }
            if (ph.phase == last_phase && ph.round == last_round) {
                /* 단계는 그대로인데 앉은 사람이 바뀌었다(누가 나갔다) */
                if (seated != last_seated && ph.phase != PH_LOBBY) {
                    printf("SEATS ms=%lld", (long long)(now - t0));
                    for (int s = 0; s < PROTO_SEATS; s++)
                        if (ph.name[s][0]) printf(" %s:seat=%d", ph.name[s], ph.seated[s]);
                    printf("\n");
                }
                last_seated = seated;
                continue;
            }
            last_seated = seated;

            /* 지난 단계에서 받은 화면 수와 가장 길었던 간격을 같이 찍는다 */
            printf("PHASE %s round=%d ms=%lld me=%d frames=%ld gap=%lld bytes=%ld", phase_name(ph.phase), ph.round,
                   (long long)(now - t0), ph.me, frames, (long long)gap, bytes);
            for (int s = 0; s < PROTO_SEATS; s++) {
                if (ph.name[s][0])
                    printf(" %s:seat=%d:lives=%d:pass=%d:score=%d", ph.name[s], ph.seated[s], ph.lives[s], ph.passed[s],
                           (int)ph.score[s]);
            }
            printf("\n");
            if (flood == 2 && ph.phase == PH_VERDICT && ph.me >= 0 && ph.me < PROTO_SEATS) {
                ph.title[sizeof ph.title - 1] = '\0';
                printf("PROBE round=%d sent=%ld got=%d title=%s\n", ph.round, probed, (int)ph.score[ph.me], ph.title);
                probed = 0;
            }
            int was = last_phase;
            frames = 0;
            gap = 0;
            bytes = 0;
            last_phase = ph.phase;
            last_round = ph.round;
            if (was >= 0 && strcmp(phase_name(ph.phase), until) == 0) {
                printf("END %s frames=%ld sent=%ld\n", until, total_frames, sent);
                return 0;
            }
        }
    }
}

/* ---- 서버가 끊기를 기다리는 것들 ------------------------------------------------ */
/* 읽지 않고 --sec 동안 붙들고만 있는다. 읽지 않으면 서버가 끊은 것을 이쪽에서는 알 수 없으므로
 * (끝났다는 표시가 못 받은 것들 뒤에 줄 서 있다) 끊었는지는 다른 접속으로 본다. */
static void hold(int64_t t0)
{
    while ((double)(now_ms() - t0) < limit_sec * 1000.0) usleep(50 * 1000);
}

/* 읽어 버리면서 끊김을 기다린다. 거절 메시지를 보았으면 *reject를 1로 한다. */
static long wait_close(int fd, int64_t t0, int *reject)
{
    for (;;) {
        int64_t now = now_ms();
        if ((double)(now - t0) > limit_sec * 1000.0) return -1;
        struct pollfd pf = { fd, POLLIN, 0 };
        if (poll(&pf, 1, 50) <= 0) continue;
        if (pull(fd) < 0) return (long)(now_ms() - t0);
        msg_hdr_t h;
        uint8_t body[64];
        while (take(&h, body, sizeof body))
            if (h.type == MSG_REJECT && reject) *reject = 1;
    }
}

static int run_bad(void)
{
    int fd = dial(), reject = 0;
    int64_t t0 = now_ms();
    if (fd < 0) { printf("BAD %s 접속 실패\n", mode); return 1; }

    if (strcmp(mode, "slow") == 0 || strcmp(mode, "stuck") == 0) {
        put_hello(fd, PROTO_VERSION);
        put_key(fd, ' ');
        if (mode[1] == 't') {
            msg_hdr_t h = { 0x7fffffffu, MSG_INPUT, 0, 0 };
            usleep((useconds_t)after_ms * 1000);
            put(fd, &h, sizeof h);
        }
        hold(t0);
        printf("BAD %s %.1f초 동안 읽지 않았습니다\n", mode, limit_sec);
        return 0;
    } else if (strcmp(mode, "silent") == 0) {
        /* 아무것도 보내지 않는다 */
    } else if (strcmp(mode, "version") == 0) {
        put_hello(fd, PROTO_VERSION + 41);
    } else if (strcmp(mode, "hugelen") == 0) {
        msg_hdr_t h = { 0xfffffff0u, MSG_HELLO, 0, 0 };
        put(fd, &h, sizeof h);
    } else if (strcmp(mode, "garbage") == 0) {
        uint8_t junk[997];
        for (size_t i = 0; i < sizeof junk; i++) junk[i] = (uint8_t)(i * 37 + 11);
        put(fd, junk, sizeof junk);
    } else if (strcmp(mode, "late") == 0) {
        msg_hdr_t h = { 0x7fffffffu, MSG_INPUT, 0, 0 };
        put_hello(fd, PROTO_VERSION);
        put(fd, &h, sizeof h);
    } else if (strcmp(mode, "half") == 0 || strcmp(mode, "cut") == 0) {
        msg_hdr_t h = { sizeof(hello_msg_t), MSG_HELLO, 0, 0 };
        if (mode[0] == 'c') {
            put_hello(fd, PROTO_VERSION);
            h.len = sizeof(input_msg_t);
            h.type = MSG_INPUT;
        }
        put(fd, &h, sizeof h);
        put(fd, "abcd", 4);
        usleep(100 * 1000);
        close(fd);
        printf("BAD %s 닫았습니다\n", mode);
        return 0;
    } else {
        return 2;
    }

    long ms = wait_close(fd, t0, &reject);
    if (ms < 0) { printf("BAD %s 서버가 끊지 않았습니다 (%.1f초)\n", mode, limit_sec); return 3; }
    printf("BAD %s closed_ms=%ld reject=%d\n", mode, ms, reject);
    if (strcmp(mode, "version") == 0 && !reject) return 1;
    return 0;
}

/* ---- drip ---------------------------------------------------------------------- */
/* HELLO를 한 바이트씩 아주 느리게 보낸다. 서버가 HELLO 전체에 시간 한도를 두면 그 한도쯤에 끊긴다. */
static int run_drip(void)
{
    uint8_t msg[sizeof(msg_hdr_t) + sizeof(hello_msg_t)];
    msg_hdr_t h = { sizeof(hello_msg_t), MSG_HELLO, 0, 0 };
    hello_msg_t hello = { .version = PROTO_VERSION };
    int fds[MAX_FILL], open_n = 0, closed = 0;
    size_t pos = 0;
    int64_t t0 = now_ms(), max_ms = 0, next = t0;
    snprintf(hello.name, sizeof hello.name, "%s", name);
    memcpy(msg, &h, sizeof h);
    memcpy(msg + sizeof h, &hello, sizeof hello);
    if (n_conn > MAX_FILL) n_conn = MAX_FILL;
    if (n_conn < 1) n_conn = 1;
    for (int i = 0; i < n_conn; i++) {
        fds[i] = dial();
        if (fds[i] >= 0) open_n++;
    }
    if (open_n == 0) { printf("DRIP 접속 실패\n"); return 1; }

    while (closed < open_n) {
        int64_t now = now_ms();
        if ((double)(now - t0) > limit_sec * 1000.0) {
            printf("DRIP n=%d closed=%d max_ms=%lld 시간 초과(보낸 바이트 %zu/%zu)\n",
                   open_n, closed, (long long)max_ms, pos, sizeof msg);
            return 3;
        }
        if (now >= next && pos < sizeof msg) {           /* 열려 있는 접속마다 다음 한 바이트 */
            for (int i = 0; i < n_conn; i++)
                if (fds[i] >= 0) (void)send(fds[i], msg + pos, 1, MSG_NOSIGNAL);
            pos++;
            next = now + after_ms;
        }
        struct pollfd pf[MAX_FILL];
        for (int i = 0; i < n_conn; i++) pf[i] = (struct pollfd){ fds[i], POLLIN, 0 };
        if (poll(pf, (nfds_t)n_conn, 20) <= 0) continue;
        for (int i = 0; i < n_conn; i++) {
            uint8_t junk[4096];
            if (fds[i] < 0 || !pf[i].revents) continue;
            ssize_t r = recv(fds[i], junk, sizeof junk, MSG_DONTWAIT);
            if (r > 0 || (r < 0 && (errno == EAGAIN || errno == EINTR))) continue;   /* 받아 준 것이면 읽어 버린다 */
            close(fds[i]);
            fds[i] = -1;
            closed++;
            if (now_ms() - t0 > max_ms) max_ms = now_ms() - t0;
        }
    }
    printf("DRIP n=%d closed=%d max_ms=%lld sent_bytes=%zu/%zu\n", open_n, closed, (long long)max_ms, pos, sizeof msg);
    return 0;
}

/* ---- fill ---------------------------------------------------------------------- */
static int run_fill(void)
{
    int fds[MAX_FILL], state[MAX_FILL];          /* 0: 아직 모른다, 1: 받아 주었다, -1: 끊겼다 */
    int welcomed = 0, refused = 0, lost = 0, printed = 0;
    int64_t t0 = now_ms();
    if (n_conn > MAX_FILL) n_conn = MAX_FILL;
    for (int i = 0; i < n_conn; i++) {
        fds[i] = dial();
        state[i] = 0;
        if (fds[i] < 0) { state[i] = -1; refused++; continue; }
        if (put_hello(fds[i], PROTO_VERSION) != 0) { /* 이미 끊겼다. 아래에서 읽다가 알게 된다 */ }
        usleep(5 * 1000);
    }
    for (;;) {
        int64_t now = now_ms();
        struct pollfd pf[MAX_FILL];
        if ((double)(now - t0) > limit_sec * 1000.0) break;
        for (int i = 0; i < n_conn; i++) pf[i] = (struct pollfd){ state[i] < 0 ? -1 : fds[i], POLLIN, 0 };
        if (poll(pf, (nfds_t)n_conn, 50) < 0) continue;
        for (int i = 0; i < n_conn; i++) {
            static uint8_t junk[65536];
            if (state[i] < 0 || !pf[i].revents) continue;
            ssize_t r = recv(fds[i], junk, sizeof junk, MSG_DONTWAIT);
            if (r > 0) {
                /* 처음 오는 것이 WELCOME이다. 그 뒤로는 읽어 버리기만 한다 */
                if (state[i] == 0 && r >= (ssize_t)sizeof(msg_hdr_t) && junk[4] == MSG_WELCOME) { state[i] = 1; welcomed++; }
            } else if (r == 0 || (errno != EAGAIN && errno != EINTR)) {
                if (state[i] == 1) lost++; else refused++;
                state[i] = -1;
                close(fds[i]);
            }
        }
        if (!printed && (welcomed + refused == n_conn || now - t0 > 1500)) {
            printf("FILL welcomed=%d refused=%d pending=%d\n", welcomed, refused, n_conn - welcomed - refused);
            printed = 1;
        }
    }
    printf("FILL_END alive=%d lost=%d\n", welcomed - lost, lost);
    return 0;
}

/* ---- 가짜 스테이지 ---------------------------------------------------------------- */
/* n바이트를 다 읽는다. 서버가 닫았으면 0. */
static int get(int fd, void *buf, size_t n)
{
    size_t got = 0;
    while (got < n) {
        ssize_t r = read(fd, (char *)buf + got, n - got);
        if (r < 0 && errno == EINTR) continue;
        if (r <= 0) return 0;
        got += (size_t)r;
    }
    return 1;
}

/* 메시지 하나를 받는다. 내용의 앞 64바이트까지는 head에 남기고 나머지는 버린다. 서버가 닫았으면 0. */
static int stage_get(int rfd, msg_hdr_t *h, uint8_t head[64])
{
    uint8_t junk[64];
    if (!get(rfd, h, sizeof *h)) return 0;
    for (uint32_t left = h->len, first = 1; left > 0; first = 0) {
        uint32_t n = left < sizeof junk ? left : (uint32_t)sizeof junk;
        if (!get(rfd, first ? head : junk, n)) return 0;
        left -= n;
    }
    return 1;
}

static void stage_put(int wfd, uint8_t type, const void *body, uint32_t len)
{
    msg_hdr_t h = { len, type, 0, 0 };
    put(wfd, &h, sizeof h);
    put(wfd, body, len);
}

/* 서버가 build/dev/stage_t_<종류>라는 이름으로 띄운다. 종류마다 한 가지씩 잘못 군다. */
static int run_stage(const char *kind, int rfd, int wfd)
{
    int count = strcmp(kind, "count") == 0;
    int ticks = strcmp(kind, "win") == 0 ? 60 : (count ? 150 : 1), big = strcmp(kind, "bigscore") == 0;
    int32_t got[PROTO_PANES] = { 0 };
    uint8_t head[64];
    info_msg_t info;
    result_msg_t res;
    msg_hdr_t h;
    if (strcmp(kind, "crash") == 0) return 3;                    /* 뜨자마자 죽는다 */

    memset(&info, 0, sizeof info);
    info.n_ticks = (uint16_t)ticks;
    snprintf(info.title, sizeof info.title, "FAKE %s", kind);
    stage_put(wfd, MSG_INFO, &info, sizeof info);
    if (strcmp(kind, "partial") == 0) {                          /* 다음 메시지를 머리 절반만 보내고 멈춘다 */
        put(wfd, "\010\0\0\0", 4);
        sleep(30);
        return 0;
    }
    for (int t = 0; t < ticks; ) {                               /* 틱마다 작은 FRAME으로 답한다 */
        if (!stage_get(rfd, &h, head)) return 0;
        if (h.type == MSG_INPUT && h.len == sizeof(input_msg_t)) {
            input_msg_t in;
            memcpy(&in, head, sizeof in);
            if (in.player < PROTO_PANES && got[in.player] < 999999) got[in.player]++;
        }
        if (h.type != MSG_TICK) continue;
        stage_put(wfd, MSG_FRAME, "abcd", 4);
        t++;
    }
    memset(&res, 0, sizeof res);
    for (int p = 0; p < PROTO_PANES; p++) res.score[p] = big ? INT32_MAX : (count ? got[p] : 100);   /* 초파리는 0점 */
    if (strcmp(kind, "early") == 0) {                            /* 결과를 내기 전에 읽는 쪽부터 닫는다 */
        close(rfd);
        usleep(300 * 1000);
    }
    stage_put(wfd, MSG_RESULT, &res, sizeof res);
    if (big) sleep(30);                                          /* 결과를 내고도 끝나지 않는다 */
    return 0;
}

int main(int argc, char **argv)
{
    int pipe_r = -1, pipe_w = -1;
    setvbuf(stdout, NULL, _IOLBF, 0);
    signal(SIGPIPE, SIG_IGN);
    for (int i = 1; i < argc; i++) {
        /* 서버가 스테이지로 띄웠다. 종류는 실행 파일 이름에서 안다. 다른 옵션은 보지 않는다 */
        if (sscanf(argv[i], "--pipe=%d,%d", &pipe_r, &pipe_w) == 2) {
            const char *base = strrchr(argv[0], '/');
            base = base ? base + 1 : argv[0];
            return run_stage(strncmp(base, "stage_t_", 8) == 0 ? base + 8 : base, pipe_r, pipe_w);
        }
    }
    for (int i = 1; i < argc; i++) {
        const char *a = argv[i];
        if (strncmp(a, "--port=", 7) == 0)         port = atoi(a + 7);
        else if (strncmp(a, "--mode=", 7) == 0)    mode = a + 7;
        else if (strncmp(a, "--name=", 7) == 0)    name = a + 7;
        else if (strncmp(a, "--sec=", 6) == 0)     limit_sec = atof(a + 6);
        else if (strncmp(a, "--n=", 4) == 0)       n_conn = atoi(a + 4);
        else if (strncmp(a, "--rcvbuf=", 9) == 0)  rcvbuf = atoi(a + 9);
        else if (strncmp(a, "--until=", 8) == 0)   until = a + 8;
        else if (strncmp(a, "--after=", 8) == 0)   after_ms = atoi(a + 8);
        else { printf("모르는 옵션: %s\n", a); return 2; }
    }
    if (strcmp(mode, "watch") == 0) return run_watch(0);
    if (strcmp(mode, "flood") == 0) return run_watch(1);
    if (strcmp(mode, "probe") == 0) return run_watch(2);
    if (strcmp(mode, "fill") == 0)  return run_fill();
    if (strcmp(mode, "drip") == 0)  return run_drip();
    int r = run_bad();
    if (r == 2) printf("--mode=watch|flood|probe|slow|stuck|silent|version|hugelen|garbage|late|half|cut|fill|drip 을 주세요\n");
    return r;
}
