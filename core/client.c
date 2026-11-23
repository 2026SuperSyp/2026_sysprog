/* 클라이언트. 서버에 접속해 화면을 그리고 입력을 올린다.
 *
 *   build/dev/client [--host=주소] [--port=N] [--name=이름] [--no-title]
 *       입장 화면(입장, 지난 랭킹 보기, 나가기)을 먼저 보여 준다. --no-title이면 바로 접속한다
 *   build/dev/client --bot [--wait=N] [...]   화면 없이 접속해 준비하고 무작위로 누른다(검사용).
 *                                             방장이면 참가자가 N명 이상일 때 시작한다
 *
 * 스레드
 *   입력 스레드   키와 마우스를 읽어 서버로 올린다. 소켓에 쓰는 스레드는 이것 하나다
 *   수신 스레드   서버 메시지를 읽어 상태를 바꾼다
 *   렌더(main)    상태가 바뀌면 합성해서 터미널에 쓴다
 * 세 스레드가 같이 보는 상태는 뮤텍스 하나로 지킨다. */
#include "core/frame.h"
#include "core/msg.h"
#include "core/scenes.h"
#include "core/toplist.h"
#include "boss/boss.h"
#include "brain.h"
#include "protocol.h"
#include "screen.h"
#include "stage.h"
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
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <time.h>
#include <unistd.h>

#define OUT_CAP (1 << 20)

static int sock = -1;

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
    int have_boss;
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
                if (S.ph.phase != PH_BOSS) S.have_boss = 0;
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
        } else if (h.type == MSG_BOSS && h.len == sizeof(boss_snap_t)) {
            pthread_mutex_lock(&S.mu);
            memcpy(&S.boss, body, sizeof S.boss);
            S.have_boss = 1;
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
static void next_watch(void)
{
    pthread_mutex_lock(&S.mu);
    int n = S.have_frame ? S.head.n_panes : 0, view = view_pane(&S.ph), w = S.watch;
    /* 다른 칸들을 차례로 돌고, 마지막 다음은 키보드 치는 초파리다 */
    int next = (w == WATCH_FLY) ? 0 : w + 1;
    while (next < n && next == view) next++;
    S.watch = next < n ? next : WATCH_FLY;
    S.dirty = 1;
    pthread_cond_broadcast(&S.cv);
    pthread_mutex_unlock(&S.mu);
}

static void *input_main(void *arg)
{
    uint8_t buf[512];
    size_t len = 0;
    (void)arg;
    for (;;) {
        struct pollfd pf = { STDIN_FILENO, POLLIN, 0 };
        pthread_mutex_lock(&S.mu);
        int quit = S.quit;
        pthread_mutex_unlock(&S.mu);
        if (quit) return NULL;
        if (poll(&pf, 1, 100) <= 0) continue;
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
            input_msg_t m = { .kind = ev.kind, .flags = ev.flags, .code = ev.code };
            if (ev.kind == IN_POINTER) {
                /* 터미널 글자 칸을 전체 화면의 픽셀로 바꿔서 올린다 */
                int cols = 0, rows = 0, sx, sy;
                term_size(&cols, &rows);
                if (!present_cell_to_screen(cols, rows, ev.x, ev.y, &sx, &sy)) continue;
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
/* 보스전 화면(임시): 위에서 내려다본 지도와 상태. 1인칭 3D 화면이 생기면 바꾼다. S.mu를 잡은 채로 부른다. */
static void draw_boss(canvas_t *full, const phase_msg_t *ph)
{
    static boss_state_t map;                 /* 벽의 배치만 쓴다 */
    static int have_map;
    static const color_t pcol[BOSS_PLAYERS] = { C_CYAN, C_GREEN, C_PINK };
    static const char *item_name[BOSS_ITEMS] = { "파리채", "스프레이", "끈끈이" };
    const boss_snap_t *b = &S.boss;
    const int cell = 5, ox = 4, oy = 7, tx = 132;
    char line[80];
    if (!have_map) {
        boss_cfg_t c;
        boss_cfg_default(&c);
        boss_init(&map, &c, 0, 1, NULL);
        have_map = 1;
    }
    ui_clear(full, C_BG);
    if (!S.have_boss) { scene_message(full, "BOSS", C_YELLOW); return; }
    for (int cy = 0; cy < BOSS_MAP; cy++)
        for (int cx = 0; cx < BOSS_MAP; cx++)
            if (boss_solid(&map, cx, cy)) ui_rect(full, ox + cx * cell, oy + cy * cell, cell, cell, C_GRAY2);

    /* 초파리: 붙어 있으면 회색, 떠 있으면 노랑, 돌진하려 하면 빨강 */
    int fx = ox + b->fly.x * cell / BOSS_FIX, fy = oy + b->fly.y * cell / BOSS_FIX;
    float fa = (float)b->fly.ang * 6.2831853f / 65536.0f;
    color_t fc = b->fly.stun ? C_GRAY3 : b->fly.air ? C_YELLOW : (b->fly.windup || b->fly.charging) ? C_RED : C_FLY_BODY;
    ui_rect(full, fx - 3, fy - 3, 6, 6, fc);
    ui_line(full, fx, fy, fx + (int)(cosf(fa) * 6.0f), fy + (int)(sinf(fa) * 6.0f), C_FLY_EYE);
    for (int k = 0; k < b->n_proj && k < BOSS_PROJ; k++)
        ui_rect(full, ox + b->proj[k].x * cell / BOSS_FIX - 1, oy + b->proj[k].y * cell / BOSS_FIX - 1, 2, 2, C_ORANGE);

    int me = (ph->me >= 0 && ph->me < PROTO_SEATS) ? ph->pane_of[ph->me] : -1;
    for (int i = 0; i < b->n_players && i < BOSS_PLAYERS; i++) {
        int px = ox + b->p[i].x * cell / BOSS_FIX, py = oy + b->p[i].y * cell / BOSS_FIX;
        float pa = (float)b->p[i].dir * 6.2831853f / BOSS_DIRS;
        if (!b->p[i].alive) { ui_rect(full, px - 1, py - 1, 2, 2, C_GRAY1); continue; }
        ui_rect(full, px - 1, py - 1, 3, 3, pcol[i]);
        ui_line(full, px, py, px + (int)(cosf(pa) * 4.0f), py + (int)(sinf(pa) * 4.0f), i == me ? C_WHITE : pcol[i]);
    }

    /* 오른쪽: 초파리 체력, 남은 시간, 참가자 */
    snprintf(line, sizeof line, "초파리 %d / %d", b->fly.hp, b->fly.hp_max);
    ui_text(full, tx, 6, line, C_TEXT);
    ui_bar(full, tx, 20, 100, b->fly.hp_max > 0 ? (float)b->fly.hp / (float)b->fly.hp_max : 0.0f, C_RED);
    snprintf(line, sizeof line, "남은 시간 %d", b->ticks_left / 20);
    ui_text(full, tx, 28, line, C_TEXT);
    for (int i = 0; i < b->n_players && i < BOSS_PLAYERS; i++) {
        const char *name = "?";
        for (int s = 0; s < PROTO_SEATS; s++) if (ph->pane_of[s] == i) name = ph->name[s];
        snprintf(line, sizeof line, "%s%.8s 체력 %d", i == me ? ">" : " ", name, b->p[i].hp);
        ui_text(full, tx, 46 + i * 14, line, b->p[i].alive ? pcol[i] : C_GRAY1);
    }
    if (me >= 0 && me < b->n_players) {
        snprintf(line, sizeof line, "%s  스프레이 %d  끈끈이 %d", item_name[b->p[me].item % BOSS_ITEMS], b->p[me].ammo, b->p[me].traps);
        ui_text(full, 4, 0, line, C_YELLOW);
        ui_text(full, tx, 94, "WASD 이동  Shift 살금살금", C_GRAY3);
        ui_text(full, tx, 108, "1 2 3 무기  Space 쓰기", C_GRAY3);
    } else {
        ui_text(full, tx, 100, "관전 중", C_GRAY3);
    }
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
static present_state_t *ps;
static char *out;
static int notice;

/* 전체 화면을 터미널에 보낸다. 터미널이 작으면 안내만 보인다. */
static void show(canvas_t *full)
{
    int cols = 0, rows = 0;
    term_size(&cols, &rows);
    size_t n = present_encode(ps, full, cols, rows, out, OUT_CAP);
    if (n > 0) {
        write_all(out, n);
        notice = 0;
    } else if ((cols < SCREEN_W || rows < SCREEN_H / 2) && !notice) {
        char msg[256];
        int m = snprintf(msg, sizeof msg,
            "\033[0m\033[2J\033[H지금 %d칸 %d줄입니다. 240칸 67줄 이상이 필요합니다.\r\n"
            "Ctrl 과 - 를 눌러 글자를 줄이거나 창을 키워 주세요. (q: 나가기)\r\n", cols, rows);
        write_all(msg, (size_t)m);
        present_invalidate(ps);
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
        show(full);

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

    for (;;) {
        /* 상태가 바뀌거나 50ms가 지나면 다시 그린다(카운트다운처럼 시간으로 움직이는 화면이 있다) */
        struct timespec until;
        clock_gettime(CLOCK_REALTIME, &until);
        until.tv_nsec += 50 * 1000000L;
        if (until.tv_nsec >= 1000000000L) { until.tv_nsec -= 1000000000L; until.tv_sec++; }
        pthread_mutex_lock(&S.mu);
        if (!S.dirty && !S.quit) pthread_cond_timedwait(&S.cv, &S.mu, &until);
        if (S.quit) { pthread_mutex_unlock(&S.mu); break; }
        S.dirty = 0;
        draw(full);
        pthread_mutex_unlock(&S.mu);

        show(full);
    }

    shutdown(sock, SHUT_RDWR);                       /* 수신 스레드를 깨운다 */
    pthread_join(t_input, NULL);
    pthread_join(t_recv, NULL);
    term_leave();
    if (S.why[0]) printf("%s\n", S.why);
    canvas_free(full);
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
    static const uint16_t boss_keys[] = { 'w', IN_K_LEFT, ' ', 'w', '2', ' ', IN_K_RIGHT, 'W', '3', ' ', '1', 's' };
    int frames = 0, sent_ready = 0, sent_start = 0, rounds = 0, last_phase = -1, top_n = -1, boss_seen = 0, boss_n = 0;
    alarm(120);                                      /* 멈춰 버리면 검사가 끝나지 않으므로 */
    for (;;) {
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
    int port = PROTO_PORT, bot = 0, wait_players = 1, no_title = 0;
    setvbuf(stdout, NULL, _IOLBF, 0);
    for (int i = 1; i < argc; i++) {
        const char *a = argv[i];
        if (strncmp(a, "--host=", 7) == 0)        host = a + 7;
        else if (strncmp(a, "--port=", 7) == 0)   port = atoi(a + 7);
        else if (strncmp(a, "--name=", 7) == 0)   name = a + 7;
        else if (strncmp(a, "--wait=", 7) == 0)   wait_players = atoi(a + 7);
        else if (strcmp(a, "--bot") == 0)         bot = 1;
        else if (strcmp(a, "--no-title") == 0)    no_title = 1;
        else { printf("모르는 옵션: %s\n", a); return 2; }
    }
    signal(SIGPIPE, SIG_IGN);

    if (!bot) {
        /* 입장 화면부터 보여 준다. 여기서는 아직 서버에 붙지 않는다 */
        if (term_enter() != 0) {
            printf("터미널에서 실행해 주세요\n");
            return 2;
        }
        full_canvas = canvas_new(SCREEN_W, SCREEN_H);
        ps = present_new();
        out = malloc(OUT_CAP);
        if (!no_title && !run_title(host, port)) { term_leave(); return 0; }
    }

    sock = connect_to(host, port);
    if (sock < 0) {
        if (!bot) term_leave();
        printf("%s:%d 에 접속할 수 없습니다\n", host, port);
        return 1;
    }
    hello_msg_t hello = { .version = PROTO_VERSION };
    utf8_copy(hello.name, sizeof hello.name, name);
    if (msg_send(sock, MSG_HELLO, 0, 0, &hello, sizeof hello) != 0) { if (!bot) term_leave(); return 1; }

    if (bot) return run_bot(wait_players);

    S.fly = canvas_new(PANE_W, PANE_H);
    for (int p = 0; p < PROTO_PANES; p++) S.panes[p] = canvas_new(PANE_W, PANE_H);
    return run_screen();
}
