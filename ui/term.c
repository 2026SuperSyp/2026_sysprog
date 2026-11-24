/* 터미널을 켜고 끄는 코드는 여기 한 곳에만 둔다.
 * 켜기: 원시 입력, 대체 화면, 커서 숨김, 마우스 보고(1003, 1006), 창이 뒤로 감 알림(1004), 키의 누름과 뗌(9001)
 * 끄기: 전부 되돌린다. 프로그램이 죽을 때도 되돌린다.
 * 켤 때 터미널에 무엇을 할 수 있는지도 한 번 물어 둔다(term_caps). */
#include "ui/ui_internal.h"

#include <poll.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <termios.h>
#include <time.h>
#include <unistd.h>

#define PROBE_MS 300             /* 터미널의 답을 기다리는 시간. 답하지 않는 터미널에서는 이만큼 늦게 뜬다 */

static struct termios saved;
static int active;
static term_caps_t caps = { 0, 10, 20 };

const term_caps_t *term_caps(void) { return &caps; }

void term_leave(void)
{
    if (!active) return;
    active = 0;
    static const char off[] = "\033[?9001l\033[?1004l\033[?1003l\033[?1006l\033[0m\033[?25h\033[?1049l";
    ssize_t r = write(STDOUT_FILENO, off, sizeof off - 1);
    (void)r;
    tcsetattr(STDIN_FILENO, TCSANOW, &saved);
}

/* 죽는 시그널을 받으면 터미널을 되돌린 뒤 원래 동작대로 죽는다.
 * 여기서는 write와 tcsetattr만 쓴다. */
static void on_fatal(int sig)
{
    term_leave();
    signal(sig, SIG_DFL);
    raise(sig);
}

/* 터미널에 묻는다.
 *   ESC [ 16 t  글자 한 칸의 픽셀 크기. 답은 ESC [ 6 ; 높이 ; 너비 t
 *   ESC [ c     기능 목록. 답은 ESC [ ? 번호 ; 번호 ; ... c 이고 4가 있으면 Sixel 그림을 받는다
 * 기능 목록은 거의 모든 터미널이 답하므로 뒤에 묻고, 그 답이 오면 더 기다리지 않는다. 답이 없으면 PROBE_MS 뒤에
 * 그만둔다(그러면 Sixel은 못 쓰는 것으로 본다). 기다리는 동안 들어온 다른 입력은 버린다. */
static void probe(void)
{
    static const char ask[] = "\033[16t\033[c";
    char buf[256];
    size_t n = 0;
    struct timespec t0, t;
    if (write(STDOUT_FILENO, ask, sizeof ask - 1) < 0) return;
    clock_gettime(CLOCK_MONOTONIC, &t0);
    buf[0] = '\0';
    for (;;) {
        clock_gettime(CLOCK_MONOTONIC, &t);
        int left = PROBE_MS - (int)((t.tv_sec - t0.tv_sec) * 1000 + (t.tv_nsec - t0.tv_nsec) / 1000000);
        struct pollfd pf = { STDIN_FILENO, POLLIN, 0 };
        if (left <= 0 || n >= sizeof buf - 1 || poll(&pf, 1, left) <= 0) break;
        ssize_t r = read(STDIN_FILENO, buf + n, sizeof buf - 1 - n);
        if (r <= 0) break;
        for (ssize_t i = 0; i < r; i++) if (buf[n + (size_t)i] == '\0') buf[n + (size_t)i] = ' ';
        n += (size_t)r;
        buf[n] = '\0';
        const char *da = strstr(buf, "\033[?");
        if (da && strchr(da, 'c')) break;
    }

    int h = 0, w = 0, used = 0;
    const char *size = strstr(buf, "\033[6;");
    if (size && sscanf(size + 4, "%d;%d%n", &h, &w, &used) == 2 && size[4 + used] == 't' && w >= 2 && w <= 200 && h >= 2 && h <= 400) {
        caps.cell_w = w;
        caps.cell_h = h;
    }
    const char *da = strstr(buf, "\033[?");
    for (const char *p = da ? da + 3 : NULL; p && *p && *p != 'c'; ) {
        if (atoi(p) == 4) caps.sixel = 1;
        while (*p >= '0' && *p <= '9') p++;
        if (*p == ';') p++;
        else if (*p != 'c') break;                   /* 숫자와 ; 말고 다른 것이 나왔다: 기능 목록이 아니다 */
    }
}

int term_enter(void)
{
    if (active) return 0;
    if (!isatty(STDIN_FILENO) || !isatty(STDOUT_FILENO)) return -1;
    if (tcgetattr(STDIN_FILENO, &saved) != 0) return -1;

    struct termios raw = saved;
    cfmakeraw(&raw);
    raw.c_cc[VMIN] = 0;
    raw.c_cc[VTIME] = 0;
    if (tcsetattr(STDIN_FILENO, TCSANOW, &raw) != 0) return -1;

    /* 마우스 보고는 묻고 난 뒤에 켠다. 먼저 켜면 마우스가 움직일 때 답 사이에 마우스 사건이 끼어든다 */
    static const char on[] = "\033[?1049h\033[?25l\033[2J", mouse[] = "\033[?1003h\033[?1006h"
        /* 키의 누름과 뗌을 따로 알려 달라고 한다(9001, Windows Terminal). 알아듣지 못하는 터미널은 무시한다.
         * 창이 뒤로 가는 것도 알려 달라고 한다(1004). 그때는 누르고 있던 키를 뗀 것으로 쳐야 한다 */
        "\033[?1004h\033[?9001h";
    ssize_t r = write(STDOUT_FILENO, on, sizeof on - 1);
    active = 1;
    probe();
    r = write(STDOUT_FILENO, mouse, sizeof mouse - 1);
    (void)r;

    atexit(term_leave);
    signal(SIGINT, on_fatal);
    signal(SIGTERM, on_fatal);
    signal(SIGSEGV, on_fatal);
    signal(SIGABRT, on_fatal);
    return 0;
}

int term_size(int *cols, int *rows)
{
    struct winsize ws;
    if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) != 0 || ws.ws_col == 0) return -1;
    *cols = ws.ws_col;
    *rows = ws.ws_row;
    return 0;
}
