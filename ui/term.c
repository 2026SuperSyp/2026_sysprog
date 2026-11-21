/* 터미널을 켜고 끄는 코드는 여기 한 곳에만 둔다.
 * 켜기: 원시 입력, 대체 화면, 커서 숨김, 마우스 보고(1003, 1006)
 * 끄기: 전부 되돌린다. 프로그램이 죽을 때도 되돌린다. */
#include "ui/ui_internal.h"

#include <signal.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <termios.h>
#include <unistd.h>

static struct termios saved;
static int active;

void term_leave(void)
{
    if (!active) return;
    active = 0;
    static const char off[] = "\033[?1003l\033[?1006l\033[0m\033[?25h\033[?1049l";
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

    static const char on[] = "\033[?1049h\033[?25l\033[?1003h\033[?1006h\033[2J";
    ssize_t r = write(STDOUT_FILENO, on, sizeof on - 1);
    (void)r;
    active = 1;

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
