/* 클라이언트. 서버에 접속해 화면을 그리고 입력을 올린다.
 *
 *   build/dev/client [--host=주소] [--port=N] [--name=이름] [--no-title] [--scale=N|auto]
 *       입장 화면(입장, 지난 랭킹 보기, 나가기)을 먼저 보여 준다. --no-title이면 바로 접속한다
 *       --scale=N(1~6)이면 화면을 N배로 키워 그린다. 터미널이 240N칸 67N줄 이상이어야 한다(글자를 더 줄인다).
 *       --scale=auto이면 시작할 때의 터미널 크기에 들어가는 가장 큰 배율을 고른다.
 *       보스전의 3D 화면은 그 크기에 맞춰 촘촘하게 그리고, 나머지 화면은 같은 그림을 크게만 보여 준다
 *       --gfx=auto|sixel|cell: 화면을 픽셀 그림(Sixel)으로 보낼지 글자 칸으로 보낼지. auto(기본값)는 터미널에 물어서
 *       그림을 받으면 그림으로 보낸다. 그림은 창에 들어가는 가장 큰 배율로 그린다(10배 = 2400x1340까지).
 *       그때 --scale=N은 그 상한을 낮춘다
 *   build/dev/client --bot [--wait=N] [...]   화면 없이 접속해 준비하고 무작위로 누른다(검사용).
 *                                             방장이면 참가자가 N명 이상일 때 시작한다
 *
 * 스레드
 *   입력 스레드   키와 마우스를 읽어 서버로 올린다. 소켓에 쓰는 스레드는 이것 하나다
 *   수신 스레드   서버 메시지를 읽어 상태를 바꾼다
 *   렌더(main)    상태가 바뀌면 합성해서 터미널에 쓴다
 * 세 스레드가 같이 보는 상태는 뮤텍스 하나로 지킨다.
 *
 * 보스전의 시선은 여기서 정한다. 마우스가 가로로 움직인 만큼 내 방향(S.look_ang)을 돌려 바로 그 방향으로 그리고,
 * 서버에는 결과인 방향만 알린다(INPUT_LOOK). 서버를 거쳐 돌아온 방향으로 그리면 한두 틱 늦어서 마우스가 무겁다.
 * 터미널은 마우스를 가둘 수 없어서 화면 끝에서 더 못 움직이므로, 마우스가 화면 양 끝에 있는 동안은 계속 돈다. */
#include "core/frame.h"
#include "core/msg.h"
#include "core/scenes.h"
#include "core/toplist.h"
#include "boss/boss.h"
#include "boss/boss_view.h"
#include "brain.h"
#include "protocol.h"
#include "screen.h"
#include "stage.h"
#include "ui/hd.h"
#include "ui/ui_internal.h"

#include <arpa/inet.h>
#include <errno.h>
#include <netdb.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <math.h>
#include <poll.h>
#include <pthread.h>
#include <signal.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <time.h>
#include <unistd.h>

#define OUT_CAP (1 << 20)
#define FRAME_MIN_MS   25            /* 화면을 다시 그리는 간격의 하한(1초에 40번까지). 마우스가 움직이면 사건이 아주 자주 온다 */
/* 보스전의 시선(각도는 65536이 한 바퀴) */
#define LOOK_SPAN      (65536.0 * 200.0 / 360.0)     /* 마우스가 화면 너비만큼 움직이면 도는 각도: 200도 */
#define LOOK_EDGE      0.04                          /* 마우스가 화면 양 끝의 이만큼(너비의 비율) 안에 있으면 계속 돈다 */
#define LOOK_EDGE_RATE (65536.0 * 150.0 / 360.0)     /* 그때 1초에 도는 각도: 150도 */
#define LOOK_SEND_MS   16            /* 시선을 서버에 알리는 간격의 하한 */
#define LOOK_KEY_STEP  (65536 / BOSS_DIRS)           /* 방향키 좌우로 한 번에 도는 각도 */
#define LOOK_SMOOTH_MS 45.0          /* 화면의 시선이 정한 방향을 따라가는 빠르기. 글자 칸이 크면 마우스가 뚝뚝 끊겨 온다 */

static int sock = -1;
static int view_scale = 1;           /* --scale. 화면을 몇 배로 키워 터미널에 그리는가 */
/* 화면을 픽셀 그림(Sixel)으로 보내는가(--gfx). 터미널이 받아 주면 그렇게 하고, 아니면 글자 칸으로 보낸다.
 * 그림의 크기는 240x134의 hd_n배이고 창 크기에 맞춰 hd_cap 이하에서 고른다. 입력 스레드도 hd_n을 읽는다 */
static int gfx_sixel;
static int hd_cap = HD_MAX_SCALE;
static atomic_int hd_n = 1;

/* 스레드들이 같이 보는 상태 */
static struct {
    pthread_mutex_t mu;
    pthread_cond_t cv;
    phase_msg_t ph;
    int have_phase;
    int64_t phase_at;                /* 이 단계 메시지를 받은 시각(ms) */
    frame_head_t head;
    int have_frame;
    canvas_t *fly, *panes[PROTO_PANES];
    frame_map_t fm;
    top_msg_t top;                   /* 서버가 마지막으로 보낸 역대 순위 */
    boss_snap_t boss;                /* 보스전의 마지막 상태 */
    int have_boss, boss_watch;       /* boss_watch: 관전할 때 누구의 눈으로 볼지 */
    int boss_help;                   /* 보스전에서 H로 켠 키 안내 */
    int have_look;                   /* 보스전에서 내가 보는 방향을 정했다(처음 받은 상태의 방향에서 시작한다) */
    uint16_t look_ang;               /* 내가 보는 방향. 마우스와 방향키로 여기서 바꾸고 서버에 알린다 */
    int look_shown_ok;               /* 화면에 그리는 방향(look_shown)을 정했다. look_ang을 부드럽게 따라간다 */
    double look_shown;
    int64_t look_shown_at;
    int have_boss_brain;             /* 보스전의 뇌 표시를 받았다 */
    float boss_stim[BRAIN_GROUP_COUNT], boss_rate[BRAIN_GROUP_COUNT], boss_takeoff;
    frame_map_t boss_fm;
    int64_t boss_hit_at, boss_hurt_at, boss_wave_at;     /* 내 공격이 맞은 때, 내가 맞은 때, 날갯짓이 있던 때(ms) */
    int64_t boss_block_at, boss_dodge_at;        /* 내가 돌진을 막은 때, 뛰어넘은 때 */
    int64_t jump_at, jump_done_at;   /* 내가 뛰어오른 때(뛰는 중이 아니면 0), 그 뜀이 화면에서 끝난 때 */
    int jump_queued;                 /* 내려서기 직전에 또 눌렀다: 내려서자마자 다시 뛴다 */
    int watch;                       /* 오른쪽 위에 볼 칸. WATCH_FLY이면 키보드 치는 초파리 */
    int dirty, quit;
    char why[96];                    /* 끝난 이유 */
} S = { .mu = PTHREAD_MUTEX_INITIALIZER, .cv = PTHREAD_COND_INITIALIZER, .watch = WATCH_FLY };

static int64_t now_ms(void)
{
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return (int64_t)t.tv_sec * 1000 + t.tv_nsec / 1000000;
}

static void quit_with(const char *why)
{
    pthread_mutex_lock(&S.mu);
    if (!S.quit) snprintf(S.why, sizeof S.why, "%s", why);
    S.quit = 1;
    pthread_cond_broadcast(&S.cv);
    pthread_mutex_unlock(&S.mu);
}

static int connect_to(const char *host, int port)
{
    char service[16];
    struct addrinfo hints = { .ai_family = AF_INET, .ai_socktype = SOCK_STREAM }, *res, *ai;
    snprintf(service, sizeof service, "%d", port);
    if (getaddrinfo(host, service, &hints, &res) != 0) return -1;
    int fd = -1;
    for (ai = res; ai; ai = ai->ai_next) {
        fd = socket(ai->ai_family, ai->ai_socktype | SOCK_CLOEXEC, ai->ai_protocol);
        if (fd < 0) continue;
        if (connect(fd, ai->ai_addr, ai->ai_addrlen) == 0) break;
        close(fd);
        fd = -1;
    }
    freeaddrinfo(res);
    if (fd >= 0) {
        int one = 1;
        setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &one, sizeof one);
    }
    return fd;
}

/* 내가 이번 라운드에 보는 칸. 뛰고 있으면 내 칸, 아니면 0번 칸을 본다. */
static int view_pane(const phase_msg_t *ph)
{
    int mine = (ph->me >= 0 && ph->me < PROTO_SEATS) ? ph->pane_of[ph->me] : -1;
    return mine >= 0 ? mine : 0;
}

/* ---- 수신 스레드 ------------------------------------------------------------- */
static void *recv_main(void *arg)
{
    static uint8_t body[PROTO_MAX_LEN];
    msg_hdr_t h;
    (void)arg;
    for (;;) {
        int r = msg_recv(sock, &h, body, sizeof body);
        if (r != 1) { quit_with("서버와의 접속이 끊겼습니다"); return NULL; }
        if (h.type == MSG_PHASE && h.len == sizeof(phase_msg_t)) {
            pthread_mutex_lock(&S.mu);
            int was = S.have_phase ? S.ph.phase : -1;
            memcpy(&S.ph, body, sizeof S.ph);
            S.have_phase = 1;
            if (S.ph.phase != was) {
                S.phase_at = now_ms();
                if (S.ph.phase != PH_PLAY) S.have_frame = 0;
                if (S.ph.phase != PH_BOSS) {
                    S.have_boss = S.have_look = S.have_boss_brain = S.look_shown_ok = 0;
                    S.boss_hit_at = S.boss_hurt_at = S.boss_wave_at = S.boss_block_at = S.boss_dodge_at = 0;
                    S.jump_at = S.jump_done_at = 0;
                    S.jump_queued = 0;
                }
                S.boss_help = 0;
                if (S.ph.phase == PH_CARD) S.watch = -1;         /* 새 라운드: 볼 칸을 다시 고른다 */
            }
            S.dirty = 1;
            pthread_cond_broadcast(&S.cv);
            pthread_mutex_unlock(&S.mu);
        } else if (h.type == MSG_FRAME) {
            pthread_mutex_lock(&S.mu);
            if (frame_unpack(body, h.len, h.flags, &S.head, S.fly, S.panes, &S.fm) == 0) S.have_frame = 1;
            S.dirty = 1;
            pthread_cond_broadcast(&S.cv);
            pthread_mutex_unlock(&S.mu);
        } else if (h.type == MSG_TOP && h.len == sizeof(top_msg_t)) {
            pthread_mutex_lock(&S.mu);
            memcpy(&S.top, body, sizeof S.top);
            S.dirty = 1;
            pthread_cond_broadcast(&S.cv);
            pthread_mutex_unlock(&S.mu);
        } else if (h.type == MSG_BOSS && h.len >= sizeof(boss_snap_t)) {
            boss_brain_msg_t bb;
            pthread_mutex_lock(&S.mu);
            memcpy(&S.boss, body, sizeof S.boss);
            S.have_boss = 1;
            /* 상태 뒤에 이번 틱의 뇌가 붙어 온다 */
            if (boss_brain_unpack(body + sizeof S.boss, h.len - sizeof S.boss, h.flags, &bb, &S.boss_fm) == 0) {
                for (int g = 0; g < BRAIN_GROUP_COUNT; g++) {
                    S.boss_stim[g] = (float)bb.stim[g] / 255.0f;
                    S.boss_rate[g] = (float)bb.rate[g] / 255.0f;
                }
                S.boss_takeoff = (float)bb.takeoff / 255.0f;
                S.have_boss_brain = 1;
            }
            /* 사건은 그 틱의 상태에만 실려 온다. 화면에 잠깐 남기려고 본 시각을 적어 둔다 */
            int me = (S.ph.me >= 0 && S.ph.me < PROTO_SEATS) ? S.ph.pane_of[S.ph.me] : -1;
            if (!S.have_look && me >= 0 && me < S.boss.n_players && me < BOSS_PLAYERS) {
                S.look_ang = S.boss.p[me].ang;                   /* 내 시선은 서버가 세워 준 방향에서 시작한다 */
                S.have_look = 1;
            }
            for (int k = 0; k < S.boss.n_ev && k < BOSS_EVENTS; k++) {
                int kind = S.boss.ev[k].kind, who = S.boss.ev[k].who;
                if ((kind == BOSS_EV_SWAT_HIT || kind == BOSS_EV_SPRAY_HIT) && who == me) S.boss_hit_at = now_ms();
                if (kind == BOSS_EV_HIT_PLAYER && who == me) S.boss_hurt_at = now_ms();
                if (kind == BOSS_EV_BLOCK && who == me) S.boss_block_at = now_ms();
                if (kind == BOSS_EV_DODGE && who == me) S.boss_dodge_at = now_ms();
                if (kind == BOSS_EV_WAVE) S.boss_wave_at = now_ms();
            }
            S.dirty = 1;
            pthread_cond_broadcast(&S.cv);
            pthread_mutex_unlock(&S.mu);
        } else if (h.type == MSG_REJECT) {
            quit_with("서버가 접속을 거절했습니다(버전이 다릅니다)");
            return NULL;
        }
    }
}

/* ---- 입력 스레드 ------------------------------------------------------------- */
/* 보스전에서 H: 키 안내를 켜고 끈다. 보스전이 아니면 0을 돌려주고 아무것도 하지 않는다 */
static int toggle_boss_help(void)
{
    int boss;
    pthread_mutex_lock(&S.mu);
    boss = S.have_phase && S.ph.phase == PH_BOSS;
    if (boss) {
        S.boss_help = !S.boss_help;
        S.dirty = 1;
        pthread_cond_broadcast(&S.cv);
    }
    pthread_mutex_unlock(&S.mu);
    return boss;
}

static void next_watch(void)
{
    pthread_mutex_lock(&S.mu);
    if (S.have_phase && S.ph.phase == PH_BOSS) {     /* 보스전: 누구의 눈으로 볼지 바꾼다(내가 살아 있으면 쓰이지 않는다) */
        S.boss_watch = (S.boss_watch + 1) % BOSS_PLAYERS;
        S.dirty = 1;
        pthread_cond_broadcast(&S.cv);
        pthread_mutex_unlock(&S.mu);
        return;
    }
    int n = S.have_frame ? S.head.n_panes : 0, view = view_pane(&S.ph), w = S.watch;
    /* 다른 칸들을 차례로 돌고, 마지막 다음은 키보드 치는 초파리다 */
    int next = (w == WATCH_FLY) ? 0 : w + 1;
    while (next < n && next == view) next++;
    S.watch = next < n ? next : WATCH_FLY;
    S.dirty = 1;
    pthread_cond_broadcast(&S.cv);
    pthread_mutex_unlock(&S.mu);
}

/* 화면(그림)이 터미널에서 차지하는 칸 수(가로) */
static int picture_cols(void)
{
    if (!gfx_sixel) return SCREEN_W * view_scale;
    int cw = term_caps()->cell_w;
    return (SCREEN_W * atomic_load(&hd_n) + cw - 1) / cw;
}

/* 그림(240n x 134n)에서 캔버스 화면(240x134)을 놓는 자리. 그림을 꽉 채우면 글자와 그림이 너무 커서, 60%의 배율로
 * 가운데에 놓는다(2400x1340에서 글자 한 줄이 72픽셀). k는 캔버스의 픽셀 하나가 몇 픽셀인가 */
static void canvas_place(int n, int *k, int *ox, int *oy)
{
    *k = (n * 6 + 5) / 10 > 0 ? (n * 6 + 5) / 10 : 1;
    *ox = (SCREEN_W * n - SCREEN_W * *k) / 2;
    *oy = (SCREEN_H * n - SCREEN_H * *k) / 2;
}

/* 터미널 글자 칸을 전체 화면(240x134)의 픽셀로 바꾼다. 화면 밖이면 0 */
static int cell_to_screen(int cols, int rows, int cell_x, int cell_y, int *sx, int *sy)
{
    if (!gfx_sixel) return present_cell_to_screen_scaled(cols, rows, view_scale, cell_x, cell_y, sx, sy);
    int n = atomic_load(&hd_n), px, py;
    image_t size = { SCREEN_W * n, SCREEN_H * n, NULL };
    const term_caps_t *tc = term_caps();
    if (!sixel_cell_to_pixel(&size, cols, rows, tc->cell_w, tc->cell_h, cell_x, cell_y, &px, &py)) return 0;
    int k, ox, oy;
    canvas_place(n, &k, &ox, &oy);
    if (px < ox || py < oy || px >= ox + SCREEN_W * k || py >= oy + SCREEN_H * k) return 0;
    *sx = (px - ox) / k;
    *sy = (py - oy) / k;
    return 1;
}

static void jump_predict(void);      /* 아래 "뛰어오르기"에 있다 */

/* 보스전의 시선. 입력 스레드만 쓴다(방향 자체인 S.look_ang은 S.mu로 지킨다) */
static struct {
    int last_cx;                 /* 마지막으로 본 마우스의 칸(가로). 모르면 -1 */
    int edge;                    /* 마우스가 화면의 왼쪽 끝에 있으면 -1, 오른쪽 끝이면 1, 아니면 0 */
    int64_t edge_at;             /* 끝에 있어서 도는 양을 마지막으로 더한 시각 */
    double frac;                 /* 방향에 아직 더하지 못한 1 미만의 나머지 */
    int unsent;                  /* 서버에 알리지 않은 변화가 있다 */
    int64_t sent_at;
} look = { .last_cx = -1 };

/* 내 시선을 d만큼(65536이 한 바퀴, 양수가 오른쪽) 돌린다. 싸우는 중이고 내가 살아 있을 때만 돈다(키 안내가 떠 있을
 * 때는 돌지 않는다). 돌았으면 1을 돌려준다 */
static int look_turn(double d)
{
    int ok = 0;
    pthread_mutex_lock(&S.mu);
    int me = (S.ph.me >= 0 && S.ph.me < PROTO_SEATS) ? S.ph.pane_of[S.ph.me] : -1;
    if (S.have_phase && S.ph.phase == PH_BOSS && S.have_boss && S.have_look && !S.boss_help && S.boss.tick > 0 &&
        S.boss.outcome == BOSS_GOING && me >= 0 && me < S.boss.n_players && me < BOSS_PLAYERS && S.boss.p[me].alive) {
        double a = (double)S.look_ang + d + look.frac, whole = floor(a);
        look.frac = a - whole;
        S.look_ang = (uint16_t)((long long)whole & 0xffff);
        S.dirty = 1;
        pthread_cond_broadcast(&S.cv);
        look.unsent = 1;
        ok = 1;
    }
    pthread_mutex_unlock(&S.mu);
    return ok;
}

/* 바뀐 시선을 서버에 알린다. 너무 자주 보내지 않되, now가 1이면(키나 클릭을 보내기 직전) 바로 보낸다. 0이면 성공 */
static int look_send(int now)
{
    if (!look.unsent || (!now && now_ms() - look.sent_at < LOOK_SEND_MS)) return 0;
    pthread_mutex_lock(&S.mu);
    input_msg_t m = { .kind = INPUT_LOOK, .code = S.look_ang };
    pthread_mutex_unlock(&S.mu);
    look.unsent = 0;
    look.sent_at = now_ms();
    return msg_send(sock, MSG_INPUT, 0, 0, &m, sizeof m);
}

static int send_boss_key(uint16_t code)
{
    input_msg_t m = { .kind = IN_KEY, .code = code };
    if (look_send(1) != 0) return -1;                /* 무기는 지금 보는 방향으로 써야 한다 */
    return msg_send(sock, MSG_INPUT, 0, 0, &m, sizeof m);
}

static void *input_main(void *arg)
{
    uint8_t buf[512];
    size_t len = 0;
    int64_t pinged = 0;
    (void)arg;
    for (;;) {
        struct pollfd pf = { STDIN_FILENO, POLLIN, 0 };
        /* 가만히 있어도 서버가 끊지 않게 2초마다 알린다. 소켓에 쓰는 것은 이 스레드뿐이다 */
        if (now_ms() - pinged >= 2000) {
            pinged = now_ms();
            if (msg_send(sock, MSG_PING, 0, 0, NULL, 0) != 0) { quit_with("서버와의 접속이 끊겼습니다"); return NULL; }
        }
        pthread_mutex_lock(&S.mu);
        int quit = S.quit, boss = S.have_phase && S.ph.phase == PH_BOSS;
        pthread_mutex_unlock(&S.mu);
        if (quit) return NULL;
        if (!boss) {
            look.last_cx = -1;
            look.edge = 0;
        } else {
            /* 마우스가 화면 끝에 있는 동안은 계속 돈다. 그리고 바뀐 시선을 서버에 알린다 */
            if (look.edge) {
                int64_t now = now_ms();
                double dt = (double)(now - look.edge_at) / 1000.0;
                look.edge_at = now;
                if (dt > 0.0) look_turn((double)look.edge * LOOK_EDGE_RATE * (dt > 0.1 ? 0.1 : dt));
            }
            if (look_send(0) != 0) { quit_with("서버와의 접속이 끊겼습니다"); return NULL; }
        }
        if (poll(&pf, 1, boss ? 15 : 100) <= 0) continue;
        ssize_t r = read(STDIN_FILENO, buf + len, sizeof buf - len);
        if (r <= 0) continue;
        len += (size_t)r;

        in_event_t ev;
        size_t used;
        while (len > 0 && input_parse(buf, len, &ev, &used)) {
            memmove(buf, buf + used, len - used);
            len -= used;
            if (ev.kind == IN_KEY && (ev.code == 'q' || ev.code == 'Q' || ev.code == 3)) { quit_with(""); return NULL; }
            if (ev.kind == IN_KEY && ev.code == '\t') { next_watch(); continue; }
            if (ev.kind == IN_KEY && (ev.code == 'h' || ev.code == 'H') && toggle_boss_help()) continue;
            if (boss && ev.kind == IN_POINTER) {
                /* 보스전의 마우스는 서버로 올리지 않고 여기서 쓴다. 가로로 움직인 칸 수가 시선이 도는 양이고,
                 * 왼쪽 버튼은 무기 쓰기(E), 오른쪽 버튼은 막기(R), 휠은 다음 무기(F)다 */
                int cols = 0, rows = 0;
                term_size(&cols, &rows);
                int cw = picture_cols(), x = ev.x - (cols - cw) / 2, zone = (int)((double)cw * LOOK_EDGE);
                if (zone < 2) zone = 2;
                if (look.last_cx >= 0 && ev.x != look.last_cx)
                    look_turn((double)(ev.x - look.last_cx) * LOOK_SPAN / (double)cw);
                look.last_cx = ev.x;
                int edge = x < zone ? -1 : (x >= cw - zone ? 1 : 0);
                if (edge != look.edge) { look.edge = edge; look.edge_at = now_ms(); }
                int bad = 0;
                if (ev.flags & IN_F_CLICK) bad = send_boss_key('e');
                else if (ev.flags & IN_F_RCLICK) bad = send_boss_key('r');
                else if (ev.flags & (IN_F_WHEEL_UP | IN_F_WHEEL_DOWN)) bad = send_boss_key('f');
                if (bad != 0) { quit_with("서버와의 접속이 끊겼습니다"); return NULL; }
                continue;
            }
            if (boss && ev.kind == IN_KEY && (ev.code == IN_K_LEFT || ev.code == IN_K_RIGHT)) {
                /* 보스전의 방향키 좌우: 마우스 없이 시선을 한 칸씩 돌린다 */
                look_turn(ev.code == IN_K_LEFT ? -(double)LOOK_KEY_STEP : (double)LOOK_KEY_STEP);
                if (look_send(1) != 0) { quit_with("서버와의 접속이 끊겼습니다"); return NULL; }
                continue;
            }
            if (boss && look_send(1) != 0) { quit_with("서버와의 접속이 끊겼습니다"); return NULL; }
            if (boss && ev.kind == IN_KEY && ev.code == ' ') jump_predict();     /* 뛰어오르기는 누른 순간부터 그린다 */
            input_msg_t m = { .kind = ev.kind, .flags = ev.flags, .code = ev.code };
            if (ev.kind == IN_POINTER) {
                /* 터미널 글자 칸을 전체 화면의 픽셀로 바꿔서 올린다 */
                int cols = 0, rows = 0, sx, sy;
                term_size(&cols, &rows);
                if (!cell_to_screen(cols, rows, ev.x, ev.y, &sx, &sy)) continue;
                m.x = (int16_t)sx;
                m.y = (int16_t)sy;
            } else if (ev.kind != IN_KEY) {
                continue;
            }
            if (msg_send(sock, MSG_INPUT, 0, 0, &m, sizeof m) != 0) { quit_with("서버와의 접속이 끊겼습니다"); return NULL; }
        }
        if (len == sizeof buf) len = 0;              /* 해석할 수 없는 바이트가 가득 찼다 */
    }
}

/* ---- 그리기 ------------------------------------------------------------------ */
/* 보스전 화면. 그리는 일은 boss/boss_view.c가 한다. S.mu를 잡은 채로 부른다. */
static int ms_since(int64_t at) { return at ? (int)(now_ms() - at) : -1; }

/* ---- 뛰어오르기 --------------------------------------------------------------------- */
/* 뛰어오르기는 서버의 답을 기다리지 않고 키를 누른 순간부터 화면에 보인다. 서버는 다음 틱에야 뛰기 시작하고 그 상태가
 * 돌아오기까지 한두 틱이 더 걸려서, 기다렸다 그리면 누른 뒤 0.1초쯤 늦게 뛰는 것처럼 느껴진다. 높이도 상태가 오는
 * 1초에 20번이 아니라 프레임마다 시간에서 구해서 매끈하게 움직인다. 맞았는지의 판정은 서버가 한다.
 * 규칙의 수치(떠 있는 시간, 높이, 키를 기억하는 시간)는 서버와 같은 boss/config.ini에서 읽는다 */
#define LAND_MS   120                /* 내려선 뒤 눈이 살짝 주저앉았다 돌아오는 시간 */
#define LAND_DIP  0.06f              /* 주저앉는 깊이(칸) */
static boss_cfg_t boss_cfg;

/* 뛰어오르는 키를 눌렀다. 서 있으면 바로 뛰고, 아직 떠 있으면 내려서기 직전일 때만 기억해 둔다(서버와 같은 규칙).
 * 입력 스레드가 부른다 */
static void jump_predict(void)
{
    pthread_mutex_lock(&S.mu);
    int me = (S.have_phase && S.ph.me >= 0 && S.ph.me < PROTO_SEATS) ? S.ph.pane_of[S.ph.me] : -1;
    int64_t now = now_ms(), dur = (int64_t)boss_cfg.jump_ticks * 50;
    if (S.have_boss && !S.boss_help && S.boss.tick > 0 && S.boss.outcome == BOSS_GOING && me >= 0 && me < S.boss.n_players &&
        me < BOSS_PLAYERS && S.boss.p[me].alive && dur > 0) {
        if (!S.jump_at) S.jump_at = now;
        else if (now - S.jump_at >= dur - (int64_t)boss_cfg.jump_buffer * 50) S.jump_queued = 1;
        S.dirty = 1;
        pthread_cond_broadcast(&S.cv);
    }
    pthread_mutex_unlock(&S.mu);
}

/* 화면에 그릴 내 눈의 높이(칸). 뛰는 중이면 누른 때부터 흐른 시간으로 구하고, 내려선 뒤 잠깐은 살짝 주저앉았다 돌아온다
 * (내려선 느낌). 그리는 동안은 *on이 1이다. 뛰는 중이 아니면 *on이 0이고 그리는 쪽이 서버가 알려 준 높이를 쓴다.
 * 다만 뛴 직후 잠깐은 *on을 1로 둔다: 서버는 한두 틱 늦게 뛰었으므로 그 상태에는 아직 떠 있는 것으로 온다.
 * S.mu를 잡은 채로 부른다 */
static float shown_jump(int *on)
{
    int64_t now = now_ms(), dur = (int64_t)boss_cfg.jump_ticks * 50;
    *on = 0;
    if (S.jump_at) {
        int64_t age = now - S.jump_at;
        *on = 1;
        S.dirty = 1;                                 /* 움직이는 중이다. 곧바로 다시 그린다 */
        if (age < dur) return boss_cfg.jump_height * boss_jump_curve((float)age / (float)dur);
        if (S.jump_queued) { S.jump_queued = 0; S.jump_at = now; return 0.0f; }
        if (age < dur + LAND_MS) return -LAND_DIP * sinf(3.14159265f * (float)(age - dur) / (float)LAND_MS);
        S.jump_at = 0;
        S.jump_done_at = now;
        return 0.0f;
    }
    if (S.jump_done_at && now - S.jump_done_at < 250) *on = 1;
    return 0.0f;
}

/* 화면에 그릴 내 시선. 정한 방향(S.look_ang)을 조금 늦게 부드럽게 따라간다. 터미널은 마우스 자리를 글자 칸으로만
 * 알려 줘서, 칸이 크면 방향이 한 번에 1~2도씩 뛴다. 다 따라가지 못했으면 곧바로 다시 그리게 한다 */
static uint16_t shown_look(void)
{
    int64_t now = now_ms();
    if (!S.look_shown_ok) {
        S.look_shown = (double)S.look_ang;
        S.look_shown_ok = 1;
    } else {
        int diff = (int16_t)(uint16_t)(S.look_ang - (uint16_t)(long long)S.look_shown);
        if (diff > -24 && diff < 24) {
            S.look_shown = (double)S.look_ang;
        } else {
            S.look_shown += (double)diff * (1.0 - exp(-(double)(now - S.look_shown_at) / LOOK_SMOOTH_MS));
            S.look_shown = fmod(S.look_shown + 65536.0, 65536.0);
            S.dirty = 1;
        }
    }
    S.look_shown_at = now;
    return (uint16_t)(long long)S.look_shown;
}

/* 보스전을 그릴 때 그리는 쪽에 넘길 것들을 채운다. S.mu를 잡은 채로 부른다 */
static boss_view_t boss_view(const phase_msg_t *ph)
{
    int z_on;
    float z = shown_jump(&z_on);
    boss_view_t v = { .me = (ph->me >= 0 && ph->me < PROTO_SEATS) ? ph->pane_of[ph->me] : -1,
                      .z_on = z_on, .z = z,
                      .hit_ms = ms_since(S.boss_hit_at), .hurt_ms = ms_since(S.boss_hurt_at),
                      .wave_ms = ms_since(S.boss_wave_at), .block_ms = ms_since(S.boss_block_at),
                      .dodge_ms = ms_since(S.boss_dodge_at),
                      .watch = S.boss_watch, .help = S.boss_help,
                      .age_ms = (int)(now_ms() - S.phase_at),
                      .look_on = S.have_look, .look_ang = S.have_look ? shown_look() : 0 };
    if (S.have_boss_brain) {
        v.brain_stim = S.boss_stim;
        v.brain_rate = S.boss_rate;
        v.brain_map = S.boss_fm.has_map ? &S.boss_fm.map : NULL;
        v.takeoff = S.boss_takeoff;
    }
    for (int s = 0; s < PROTO_SEATS; s++)
        if (ph->pane_of[s] >= 0 && ph->pane_of[s] < BOSS_PLAYERS) v.name[ph->pane_of[s]] = ph->name[s];
    return v;
}

static void draw_boss(canvas_t *full, const phase_msg_t *ph)
{
    if (!S.have_boss) { scene_message(full, "BOSS", C_YELLOW); return; }
    boss_view_t v = boss_view(ph);
    boss_view_draw(full, &S.boss, &v);
}

/* 지금 상태를 전체 화면에 그린다. S.mu를 잡은 채로 부른다. */
static void draw(canvas_t *full)
{
    const phase_msg_t *ph = &S.ph;
    double t = (double)(now_ms() - S.phase_at) / 1000.0;
    if (!S.have_phase) { scene_message(full, "CONNECTING...", C_TEXT); return; }

    switch (ph->phase) {
    case PH_LOBBY: {
        lobby_state_t ls = { .me = -1, .t = t };
        for (int s = 0; s < PROTO_SEATS; s++) {
            if (!ph->seated[s]) continue;
            if (s == ph->me) ls.me = ls.n;
            snprintf(ls.p[ls.n].name, sizeof ls.p[ls.n].name, "%s", ph->name[s]);
            ls.p[ls.n].ready = ph->ready[s];
            ls.p[ls.n].is_host = (s == ph->host);
            ls.n++;
        }
        ui_clear(full, C_BG);
        default_screen_drew();
        screen_lobby(full, &ls);
        /* 역대 순위는 코어의 기본 화면일 때만 덧그린다. 팀원이 만든 화면이면 그 배치를 모른다 */
        if (default_screen_drew()) toplist_draw(full, &S.top, TOPLIST_LOBBY_Y0, TOPLIST_LOBBY_Y1, 0);
        break;
    }
    case PH_CARD:
        scene_card(full, ph->title, ph->rule, ph->controls, (int)(t * 20.0));
        break;
    case PH_READY:
        scene_count(full, (int)(t * 20.0));
        break;
    case PH_PLAY: {
        if (!S.have_frame) { scene_count(full, 39); break; }
        int n = S.head.n_panes, view = view_pane(ph);
        if (view >= n) view = 0;
        if (S.watch != WATCH_FLY && (S.watch < 0 || S.watch >= n || S.watch == view)) {
            S.watch = WATCH_FLY;                                     /* 볼 칸을 처음 고른다 */
            for (int p = 0; p < n; p++) if (p != view) { S.watch = p; break; }
        }
        float stim[BRAIN_GROUP_COUNT], rate[BRAIN_GROUP_COUNT];
        for (int g = 0; g < BRAIN_GROUP_COUNT; g++) {
            stim[g] = (float)S.head.stim[g] / 255.0f;
            rate[g] = (float)S.head.rate[g] / 255.0f;
        }
        hud_t hud = {
            .title = ph->title,
            .ticks_left = S.head.n_ticks - S.head.tick - 1,
            .round = ph->round,
            .my_score = S.head.score[view],
            .fly_score = S.head.fly_score,
            .lives = (ph->me >= 0) ? ph->lives[ph->me] : 0,
            .watch = S.watch,
            .brain_stim = stim,
            .brain_rate = rate,
            .brain_map = S.fm.has_map ? &S.fm.map : NULL,
        };
        compose_stage_frame(full, S.fly, S.panes, n, view, &hud);
        if (ph->me < 0 || ph->pane_of[ph->me] < 0) {                 /* 관전 중 */
            spectate_state_t sp = { .t = t };
            for (int s = 0; s < PROTO_SEATS; s++)
                if (ph->pane_of[s] == view) snprintf(sp.watching, sizeof sp.watching, "%s", ph->name[s]);
            screen_spectate(full, &sp);
        }
        break;
    }
    case PH_VERDICT: {
        round_result_t rr = { .fly_score = (float)ph->fly_score, .stage_name = ph->title, .me = -1, .t = t };
        for (int pane = 0; pane < PROTO_SEATS; pane++) {
            for (int s = 0; s < PROTO_SEATS; s++) {
                if (ph->pane_of[s] != pane) continue;
                if (s == ph->me) rr.me = rr.n;
                snprintf(rr.name[rr.n], sizeof rr.name[rr.n], "%s", ph->name[s]);
                rr.score[rr.n] = (float)ph->score[s];
                rr.passed[rr.n] = ph->passed[s];
                rr.lives[rr.n] = ph->lives[s];
                rr.next[rr.n] = ph->lives[s] > 0 ? NEXT_ROUND : NEXT_SPECTATE;
                rr.n++;
            }
        }
        ui_clear(full, C_BG);
        screen_result(full, &rr);
        break;
    }
    case PH_RANK: {
        ranking_t rk = { .me = -1, .t = t };
        int order[PROTO_SEATS], n = 0;
        for (int s = 0; s < PROTO_SEATS; s++) if (ph->name[s][0]) order[n++] = s;
        for (int i = 1; i < n; i++)                                  /* 점수가 큰 순서로 */
            for (int j = i; j > 0 && ph->total[order[j]] > ph->total[order[j - 1]]; j--) {
                int tmp = order[j]; order[j] = order[j - 1]; order[j - 1] = tmp;
            }
        for (int i = 0; i < n; i++) {
            if (order[i] == ph->me) rk.me = i;
            snprintf(rk.name[i], sizeof rk.name[i], "%s", ph->name[order[i]]);
            rk.total[i] = ph->total[order[i]];
        }
        rk.n = n;
        ui_clear(full, C_BG);
        default_screen_drew();
        screen_ranking(full, &rk);
        if (default_screen_drew()) toplist_draw(full, &S.top, TOPLIST_RANK_Y0, TOPLIST_RANK_Y1, 1);
        break;
    }
    case PH_BOSS:
        draw_boss(full, ph);
        break;
    default:
        scene_message(full, "...", C_YELLOW);
        break;
    }
}

static void write_all(const char *buf, size_t n)
{
    while (n > 0) {
        ssize_t w = write(STDOUT_FILENO, buf, n);
        if (w <= 0) return;
        buf += w;
        n -= (size_t)w;
    }
}

/* 터미널에 그리는 데 쓰는 것들. 입장 화면과 게임 화면이 같이 쓴다 */
static canvas_t *full_canvas;
static canvas_t *big_canvas;         /* --scale이 2 이상일 때 터미널에 보내는 화면(240N x 134N). 아니면 NULL */
static present_state_t *ps;
static char *out;
static size_t out_cap;
static int notice;

/* 화면을 터미널에 보낸다. 터미널이 작으면 안내만 보인다.
 * c가 240x134이고 배율이 2 이상이면 그대로 키워서 보낸다. 이미 키운 화면(big_canvas)이면 그대로 보낸다. */
static void show(canvas_t *c)
{
    int cols = 0, rows = 0;
    if (big_canvas && c != big_canvas) {
        canvas_blit_scaled(big_canvas, 0, 0, c, view_scale);
        c = big_canvas;
    }
    int need_cols = ui_width(c), need_rows = ui_height(c) / 2;
    term_size(&cols, &rows);
    size_t n = present_encode(ps, c, cols, rows, out, out_cap);
    if (n > 0) {
        write_all(out, n);
        notice = 0;
    } else if ((cols < need_cols || rows < need_rows) && !notice) {
        char msg[256];
        int m = snprintf(msg, sizeof msg,
            "\033[0m\033[2J\033[H지금 %d칸 %d줄입니다. %d칸 %d줄 이상이 필요합니다.\r\n"
            "Ctrl 과 - 를 눌러 글자를 줄이거나 창을 키워 주세요. (q: 나가기)\r\n", cols, rows, need_cols, need_rows);
        write_all(msg, (size_t)m);
        present_invalidate(ps);
        notice = 1;
    }
}

/* ---- 픽셀 그림으로 보내기 (Sixel) ------------------------------------------------ */
/* 그림 한 장은 2MB를 넘기도 해서 터미널이 받는 데 수십 ms가 걸린다. 그동안 다음 장을 그릴 수 있게 쓰는 일은
 * 쓰기 스레드에 맡긴다. 맡길 자리는 한 장뿐이다. 앞의 장을 아직 쓰고 있으면 다 쓸 때까지 기다린다(장을 버리면
 * "바뀐 줄만 보내기"가 어긋난다). 그래서 한 장에 걸리는 시간은 그리는 시간과 쓰는 시간 가운데 긴 쪽이 된다. */
static struct {
    pthread_mutex_t mu;
    pthread_cond_t cv;
    pthread_t th;
    char *buf;
    size_t len, cap;
    int started, busy, stop;
} W = { .mu = PTHREAD_MUTEX_INITIALIZER, .cv = PTHREAD_COND_INITIALIZER };

static void *writer_main(void *arg)
{
    (void)arg;
    pthread_mutex_lock(&W.mu);
    for (;;) {
        while (!W.busy && !W.stop) pthread_cond_wait(&W.cv, &W.mu);
        if (!W.busy) break;
        pthread_mutex_unlock(&W.mu);
        write_all(W.buf, W.len);
        pthread_mutex_lock(&W.mu);
        W.busy = 0;
        pthread_cond_broadcast(&W.cv);
    }
    pthread_mutex_unlock(&W.mu);
    return NULL;
}

/* 맡긴 것을 다 쓸 때까지 기다린다. 이 스레드가 터미널에 직접 쓰기 전과 끝낼 때 부른다 */
static void write_wait(void)
{
    pthread_mutex_lock(&W.mu);
    while (W.busy) pthread_cond_wait(&W.cv, &W.mu);
    pthread_mutex_unlock(&W.mu);
}

/* 글자열을 쓰기 스레드에 맡긴다. 스레드를 못 만들었으면 여기서 쓴다 */
static void write_later(const char *p, size_t n)
{
    if (!W.started && pthread_create(&W.th, NULL, writer_main, NULL) == 0) W.started = 1;
    if (!W.started) { write_all(p, n); return; }
    pthread_mutex_lock(&W.mu);
    while (W.busy) pthread_cond_wait(&W.cv, &W.mu);
    if (W.cap < n) {
        char *nb = realloc(W.buf, n * 2);
        if (!nb) { pthread_mutex_unlock(&W.mu); write_all(p, n); return; }
        W.buf = nb;
        W.cap = n * 2;
    }
    memcpy(W.buf, p, n);
    W.len = n;
    W.busy = 1;
    pthread_cond_broadcast(&W.cv);
    pthread_mutex_unlock(&W.mu);
}

static image_t *hd_img;              /* 터미널에 보내는 그림(240n x 134n) */
static sixel_state_t *sx;

/* 지금 창에 들어가는 가장 큰 그림을 마련한다. 창 크기가 바뀌면 다시 만든다. 못 만들면 0 */
static int hd_prepare(void)
{
    const term_caps_t *tc = term_caps();
    int cols = 0, rows = 0;
    term_size(&cols, &rows);
    int n = cols * tc->cell_w / SCREEN_W, m = rows * tc->cell_h / SCREEN_H;
    if (m < n) n = m;
    if (n > hd_cap) n = hd_cap;
    if (n < 1) n = 1;
    if (!hd_img || hd_img->w != SCREEN_W * n) {
        image_free(hd_img);
        hd_img = image_new(SCREEN_W * n, SCREEN_H * n);
        if (!hd_img) return 0;
    }
    atomic_store(&hd_n, n);
    return 1;
}

/* 240x134 캔버스에 그린 화면을 그림으로 옮긴다. 캔버스는 그대로 키우고, 받아 둔 것(글자, 말풍선 상자)은 실제
 * 해상도로 다시 그린다 */
static void hd_from_canvas(const canvas_t *full, const ui_text_item_t *items, int n_items)
{
    int k, ox, oy;
    canvas_place(hd_img->w / SCREEN_W, &k, &ox, &oy);
    hd_fill(hd_img, hd_color(C_BG));
    hd_canvas(hd_img, ox, oy, full, k);
    hd_items(hd_img, items, n_items, k, ox, oy);
}

/* 로비를 픽셀 그림에 바로 그린다(ui/art.c). 글자 칸으로 보낼 때는 screen_lobby가 캔버스에 그린다.
 * S.mu를 잡은 채로 부른다 */
static void draw_lobby_hd(void)
{
    static char top_line[6][PROTO_NAME_LEN + 24];
    const phase_msg_t *ph = &S.ph;
    art_seat_t seat[PROTO_SEATS];
    const char *top[6];
    int n = 0, n_top = 0;
    for (int s = 0; s < PROTO_SEATS; s++)
        if (ph->seated[s]) seat[n++] = (art_seat_t){ ph->name[s], ph->ready[s], s == ph->host, s == ph->me };
    for (int i = 0; i < S.top.n && i < 6 && i < PROTO_TOP_N; i++) {
        snprintf(top_line[i], sizeof top_line[i], "%d   %.*s   %d", i + 1, PROTO_NAME_LEN, S.top.name[i], (int)S.top.score[i]);
        top[n_top++] = top_line[i];
    }
    art_lobby(hd_img, seat, n, top, n_top, (double)(now_ms() - S.phase_at) / 1000.0);
}

static void show_hd(void)
{
    const term_caps_t *tc = term_caps();
    int cols = 0, rows = 0;
    size_t len = 0;
    term_size(&cols, &rows);
    const char *p = sixel_encode(sx, hd_img, cols, rows, tc->cell_w, tc->cell_h, &len);
    if (p) {
        if (len > 0) write_later(p, len);
        notice = 0;
    } else if (!notice) {
        static const char msg[] = "\033[0m\033[2J\033[H창이 너무 작습니다. 창을 키워 주세요. (q: 나가기)\r\n";
        write_wait();
        write_all(msg, sizeof msg - 1);
        notice = 1;
    }
}

/* ---- 입장 화면 ---------------------------------------------------------------- */
/* 서버에 잠깐 붙어 역대 순위만 받아 온다. 로비에는 나타나지 않는다. 1이면 받았다. */
static int peek_top(const char *host, int port, top_msg_t *top)
{
    static uint8_t body[PROTO_MAX_LEN];
    int fd = connect_to(host, port), got = 0;
    if (fd < 0) return 0;
    struct timeval tv = { 2, 0 };
    setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof tv);
    hello_msg_t hello = { .version = PROTO_VERSION, .peek = 1 };
    msg_hdr_t h;
    if (msg_send(fd, MSG_HELLO, 0, 0, &hello, sizeof hello) == 0) {
        while (!got && msg_recv(fd, &h, body, sizeof body) == 1) {
            if (h.type == MSG_TOP && h.len == sizeof *top) {
                memcpy(top, body, sizeof *top);
                got = 1;
            }
        }
    }
    close(fd);
    return got;
}

/* 입장 화면을 돌린다. 1: 입장을 골랐다, 0: 나가기를 골랐다. 아직 서버에는 붙지 않은 상태다. */
static int run_title(const char *host, int port)
{
    canvas_t *full = full_canvas;
    static top_msg_t top;
    uint8_t buf[256];
    size_t len = 0;
    int sel = 0, past = 0, past_ok = 0;

    for (int tick = 0; ; tick++) {
        int hd = gfx_sixel && hd_prepare();
        if (hd) ui_text_capture_begin(full);
        if (past) {
            /* 지난 랭킹 */
            ui_clear(full, C_BG);
            const char *head = "지난 랭킹", *foot = "아무 키나 누르면 돌아가요";
            ui_text(full, (SCREEN_W - ui_text_w(head)) / 2, 8, head, C_YELLOW);
            if (past_ok) toplist_draw(full, &top, 26, 112, 0);
            else scene_bubble(full, 60, 50, "서버에 닿지 않아요\n서버가 켜져 있는지 봐 주세요", C_TEXT, -1);
            ui_text(full, (SCREEN_W - ui_text_w(foot)) / 2, 119, foot, C_CYAN);
        } else {
            scene_title(full, tick, sel);
        }
        if (hd) {
            const ui_text_item_t *items;
            int n_items = ui_text_capture_end(&items);
            hd_from_canvas(full, items, n_items);
            show_hd();
        } else {
            show(full);
        }

        struct pollfd pf = { STDIN_FILENO, POLLIN, 0 };
        if (poll(&pf, 1, 50) > 0) {
            ssize_t r = read(STDIN_FILENO, buf + len, sizeof buf - len);
            if (r > 0) len += (size_t)r;
        }
        in_event_t ev;
        size_t used;
        while (len > 0 && input_parse(buf, len, &ev, &used)) {
            memmove(buf, buf + used, len - used);
            len -= used;
            if (ev.kind != IN_KEY) continue;
            if (ev.code == 'q' || ev.code == 'Q' || ev.code == 3) return 0;
            if (past) { past = 0; continue; }
            int go = 0;
            if (ev.code == IN_K_UP || ev.code == IN_K_LEFT) sel = (sel + 2) % 3;
            else if (ev.code == IN_K_DOWN || ev.code == IN_K_RIGHT) sel = (sel + 1) % 3;
            else if (ev.code >= '1' && ev.code <= '3') { sel = ev.code - '1'; go = 1; }
            else if (ev.code == ' ' || ev.code == '\r' || ev.code == '\n') go = 1;
            if (!go) continue;
            if (sel == 0) return 1;
            if (sel == 2) return 0;
            past_ok = peek_top(host, port, &top);
            past = 1;
        }
        if (len == sizeof buf) len = 0;
    }
}

static int run_screen(void)
{
    canvas_t *full = full_canvas;
    pthread_t t_recv, t_input;
    pthread_create(&t_recv, NULL, recv_main, NULL);
    pthread_create(&t_input, NULL, input_main, NULL);

    int64_t shown_at = 0;
    for (;;) {
        /* 방금 그렸으면 조금 쉰다. 마우스가 움직이는 동안은 상태가 쉬지 않고 바뀌어서, 그때마다 그리면 터미널이 밀린다 */
        int64_t rest = shown_at + FRAME_MIN_MS - now_ms();
        if (rest > 0) usleep((useconds_t)rest * 1000);
        shown_at = now_ms();
        /* 상태가 바뀌거나 50ms가 지나면 다시 그린다(카운트다운처럼 시간으로 움직이는 화면이 있다) */
        struct timespec until;
        clock_gettime(CLOCK_REALTIME, &until);
        until.tv_nsec += 50 * 1000000L;
        if (until.tv_nsec >= 1000000000L) { until.tv_nsec -= 1000000000L; until.tv_sec++; }
        pthread_mutex_lock(&S.mu);
        if (!S.dirty && !S.quit) pthread_cond_timedwait(&S.cv, &S.mu, &until);
        if (S.quit) { pthread_mutex_unlock(&S.mu); break; }
        S.dirty = 0;
        canvas_t *shown = full;
        const ui_text_item_t *items = NULL;
        int hd = gfx_sixel && hd_prepare(), n_items = 0, boss = S.have_phase && S.ph.phase == PH_BOSS && S.have_boss;
        /* 보스전에서 싸우는 화면인가(시프봇의 키 안내가 떠 있지 않다). 그 화면은 큰 그림에 바로 그린다 */
        int fight = boss && !(S.boss.outcome == BOSS_GOING && (S.boss_help || S.boss.tick == 0));
        int lobby = S.have_phase && S.ph.phase == PH_LOBBY;      /* 로비도 큰 그림에 바로 그린다 */
        if (hd && fight) {
            boss_view_t v = boss_view(&S.ph);
            boss_view_draw_hd(hd_img, &S.boss, &v);
        } else if (hd && lobby) {
            draw_lobby_hd();
        } else if (hd) {
            ui_text_capture_begin(full);
            draw(full);
            n_items = ui_text_capture_end(&items);
        } else if (big_canvas && boss) {
            draw_boss(big_canvas, &S.ph);            /* 보스전의 3D 화면은 키운 화면에 바로 그린다 */
            shown = big_canvas;
        } else {
            draw(full);
        }
        pthread_mutex_unlock(&S.mu);

        if (hd) {
            if (!fight && !lobby) hd_from_canvas(full, items, n_items);
            show_hd();
        } else {
            show(shown);
        }
    }

    write_wait();                                    /* 쓰기 스레드에 맡긴 마지막 장까지 나간 뒤에 터미널을 되돌린다 */
    shutdown(sock, SHUT_RDWR);                       /* 수신 스레드를 깨운다 */
    pthread_join(t_input, NULL);
    pthread_join(t_recv, NULL);
    term_leave();
    if (S.why[0]) printf("%s\n", S.why);
    canvas_free(full);
    canvas_free(big_canvas);
    present_free(ps);
    free(out);
    return 0;
}

/* ---- 봇: 화면 없이 한 판을 끝까지 간다 ----------------------------------------- */
static void send_key(uint16_t code)
{
    input_msg_t m = { .kind = IN_KEY, .code = code };
    msg_send(sock, MSG_INPUT, 0, 0, &m, sizeof m);
}

static int run_bot(int wait_players)
{
    static uint8_t body[PROTO_MAX_LEN];
    msg_hdr_t h;
    static const uint16_t boss_keys[] = { 'w', IN_K_LEFT, 'e', 'w', '2', 'e', IN_K_RIGHT, 'W', '3', 'e', '1', ' ', 's', 'r' };
    int frames = 0, sent_ready = 0, sent_start = 0, rounds = 0, last_phase = -1, top_n = -1, boss_seen = 0, boss_n = 0;
    alarm(120);                                      /* 멈춰 버리면 검사가 끝나지 않으므로 */
    for (;;) {
        /* 올 것이 없으면 1초마다 PING을 보낸다(서버가 말 없는 접속을 끊는다) */
        struct pollfd pf = { sock, POLLIN, 0 };
        if (poll(&pf, 1, 1000) == 0) { msg_send(sock, MSG_PING, 0, 0, NULL, 0); continue; }
        if (msg_recv(sock, &h, body, sizeof body) != 1) { printf("BOT 접속이 끊겼습니다\n"); return 1; }
        if (h.type == MSG_TOP && h.len == sizeof(top_msg_t)) top_n = body[offsetof(top_msg_t, n)];
        if (h.type == MSG_FRAME) {
            if (++frames % 4 == 0) send_key(frames % 8 == 0 ? IN_K_LEFT : IN_K_RIGHT);
            continue;
        }
        if (h.type == MSG_BOSS) {                    /* 보스전: 받은 상태 3개마다 키를 하나 누른다 */
            if (++boss_n % 3 == 0) send_key(boss_keys[(boss_n / 3) % (int)(sizeof boss_keys / sizeof boss_keys[0])]);
            continue;
        }
        if (h.type != MSG_PHASE || h.len != sizeof(phase_msg_t)) continue;
        phase_msg_t ph;
        memcpy(&ph, body, sizeof ph);
        if (ph.phase != last_phase) {
            if (ph.phase == PH_LOBBY) sent_ready = sent_start = 0;
            if (ph.phase == PH_VERDICT) rounds++;
            if (ph.phase == PH_BOSS) boss_seen = 1;
            last_phase = ph.phase;
        }
        if (ph.phase == PH_LOBBY && ph.me >= 0) {
            if (!ph.ready[ph.me] && !sent_ready) { send_key(' '); sent_ready = 1; }
            int all = 1;
            for (int s = 0; s < PROTO_SEATS; s++) if (ph.seated[s] && !ph.ready[s]) all = 0;
            if (ph.me == ph.host && all && ph.n_seats >= wait_players && !sent_start) { send_key('f'); sent_start = 1; }
        } else if (ph.phase == PH_RANK) {
            printf("BOT rounds=%d frames=%d me=%d total=%d top=%d boss=%d\n", rounds, frames, ph.me,
                   ph.me >= 0 ? ph.total[ph.me] : 0, top_n, boss_seen);
            return (rounds > 0 && frames > 0) ? 0 : 1;
        }
    }
}

int main(int argc, char **argv)
{
    const char *host = "127.0.0.1", *name = "PLAYER";
    int port = PROTO_PORT, bot = 0, wait_players = 1, no_title = 0, auto_scale = 0, scale_given = 0;
    const char *gfx = "auto";
    setvbuf(stdout, NULL, _IOLBF, 0);
    for (int i = 1; i < argc; i++) {
        const char *a = argv[i];
        if (strncmp(a, "--host=", 7) == 0)        host = a + 7;
        else if (strncmp(a, "--port=", 7) == 0)   port = atoi(a + 7);
        else if (strncmp(a, "--name=", 7) == 0)   name = a + 7;
        else if (strncmp(a, "--wait=", 7) == 0)   wait_players = atoi(a + 7);
        else if (strcmp(a, "--bot") == 0)         bot = 1;
        else if (strcmp(a, "--no-title") == 0)    no_title = 1;
        else if (strcmp(a, "--scale=auto") == 0)  auto_scale = 1;
        else if (strncmp(a, "--scale=", 8) == 0)  { view_scale = atoi(a + 8); scale_given = 1; }
        else if (strcmp(a, "--gfx=auto") == 0 || strcmp(a, "--gfx=cell") == 0 || strcmp(a, "--gfx=sixel") == 0) gfx = a + 6;
        else { printf("모르는 옵션: %s\n", a); return 2; }
    }
    if (view_scale < 1 || view_scale > PRESENT_MAX_SCALE) {
        printf("--scale은 1부터 %d까지, 또는 auto입니다\n", PRESENT_MAX_SCALE);
        return 2;
    }
    signal(SIGPIPE, SIG_IGN);

    if (!bot) {
        /* 입장 화면부터 보여 준다. 여기서는 아직 서버에 붙지 않는다 */
        if (term_enter() != 0) {
            printf("터미널에서 실행해 주세요\n");
            return 2;
        }
        /* 터미널이 그림을 받아 주면 화면을 픽셀 그림으로 보낸다. 그때 --scale은 그림 배율의 상한이다 */
        gfx_sixel = strcmp(gfx, "sixel") == 0 || (strcmp(gfx, "auto") == 0 && term_caps()->sixel);
        if (gfx_sixel) {
            if (scale_given && view_scale < hd_cap) hd_cap = view_scale;
            view_scale = 1;
            auto_scale = 0;
            sx = sixel_new();
            if (!sx) gfx_sixel = 0;
        }
        if (auto_scale) {
            /* 지금 터미널에 들어가는 가장 큰 배율. 글자가 작을수록(칸이 많을수록) 높아진다 */
            int cols = 0, rows = 0;
            term_size(&cols, &rows);
            view_scale = cols / SCREEN_W < rows / (SCREEN_H / 2) ? cols / SCREEN_W : rows / (SCREEN_H / 2);
            if (view_scale < 1) view_scale = 1;
            if (view_scale > PRESENT_MAX_SCALE) view_scale = PRESENT_MAX_SCALE;
        }
        full_canvas = canvas_new(SCREEN_W, SCREEN_H);
        if (view_scale > 1) big_canvas = canvas_new(SCREEN_W * view_scale, SCREEN_H * view_scale);
        ps = present_new();
        out_cap = (size_t)OUT_CAP * (size_t)view_scale * (size_t)view_scale;     /* 칸 수가 배율의 제곱으로 는다 */
        out = malloc(out_cap);
        if (!full_canvas || !ps || !out || (view_scale > 1 && !big_canvas)) {
            term_leave();
            printf("메모리가 모자랍니다\n");
            return 1;
        }
        if (!no_title && !run_title(host, port)) { write_wait(); term_leave(); return 0; }
    }

    sock = connect_to(host, port);
    if (sock < 0) {
        if (!bot) term_leave();
        printf("%s:%d 에 접속할 수 없습니다\n", host, port);
        return 1;
    }
    boss_cfg_load(&boss_cfg, "boss/config.ini");     /* 뛰어오르기를 서버와 같은 수치로 그린다 */
    hello_msg_t hello = { .version = PROTO_VERSION };
    utf8_copy(hello.name, sizeof hello.name, name);
    if (msg_send(sock, MSG_HELLO, 0, 0, &hello, sizeof hello) != 0) { if (!bot) term_leave(); return 1; }

    if (bot) return run_bot(wait_players);

    S.fly = canvas_new(PANE_W, PANE_H);
    for (int p = 0; p < PROTO_PANES; p++) S.panes[p] = canvas_new(PANE_W, PANE_H);
    return run_screen();
}
