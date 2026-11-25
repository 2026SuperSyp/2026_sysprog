/* 터미널 크기: 창 크기가 바뀌면(SIGWINCH) 다음에 물을 때 새 크기가 나오고, 바뀌지 않은 동안은 커널에 다시 묻지 않는다.
 * 진짜 터미널 없이 보려고 가짜 터미널(pty)을 만들어 자식 프로세스의 터미널로 붙인다. 부모가 그 터미널의 크기를 바꾸면
 * 커널이 자식에게 SIGWINCH를 보낸다. 자식은 본 것을 파이프로 알려 준다. */
#define _XOPEN_SOURCE 600
#include "ui/ui_internal.h"
#include "t.h"

#include <fcntl.h>
#include <poll.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

typedef struct { int rc, cols, rows, asked; } seen_t;

static void set_size(int master, int cols, int rows)
{
    struct winsize ws = { (unsigned short)rows, (unsigned short)cols, 0, 0 };
    T_CHECK(ioctl(master, TIOCSWINSZ, &ws) == 0);
}

/* 자식: 부모가 한 글자를 보낼 때마다 터미널 크기를 열 번 묻고, 마지막 답과 커널에 물은 횟수를 돌려준다 */
static void child(const char *slave_name, int ask_fd, int tell_fd)
{
    setsid();                                        /* 새 세션: 아래에서 여는 터미널이 이 프로세스의 터미널이 된다 */
    int slave = open(slave_name, O_RDWR);
    if (slave < 0) _exit(2);
    dup2(slave, STDOUT_FILENO);
    char go;
    while (read(ask_fd, &go, 1) == 1) {
        seen_t s = { 0, 0, 0, 0 };
        for (int k = 0; k < 10; k++) s.rc = term_size(&s.cols, &s.rows);
        s.asked = term_size_asked();
        if (write(tell_fd, &s, sizeof s) != (ssize_t)sizeof s) _exit(3);
    }
    _exit(0);
}

static seen_t ask(int ask_fd, int tell_fd)
{
    seen_t s = { -9, 0, 0, 0 };
    struct pollfd pf = { tell_fd, POLLIN, 0 };
    T_CHECK(write(ask_fd, "?", 1) == 1);
    if (poll(&pf, 1, 3000) > 0) T_CHECK(read(tell_fd, &s, sizeof s) == (ssize_t)sizeof s);
    return s;
}

int main(void)
{
    int cols, rows, asked0 = term_size_asked();
    if (!isatty(STDOUT_FILENO)) T_EQ_INT(term_size(&cols, &rows), -1);      /* 터미널이 아니면 실패를 돌려준다 */
    T_CHECK(term_size_asked() >= asked0);

    int master = posix_openpt(O_RDWR | O_NOCTTY);
    T_CHECK(master >= 0 && grantpt(master) == 0 && unlockpt(master) == 0);
    const char *name = ptsname(master);
    T_CHECK(name != NULL);
    int to_child[2], from_child[2];
    T_CHECK(pipe(to_child) == 0 && pipe(from_child) == 0);
    set_size(master, 100, 30);
    fflush(stdout);
    pid_t pid = fork();
    T_CHECK(pid >= 0);
    if (pid == 0) {
        close(master); close(to_child[1]); close(from_child[0]);
        child(name, to_child[0], from_child[1]);
    }
    close(to_child[0]); close(from_child[1]);

    seen_t a = ask(to_child[1], from_child[0]);
    T_EQ_INT(a.rc, 0);
    T_EQ_INT(a.cols, 100);
    T_EQ_INT(a.rows, 30);
    seen_t b = ask(to_child[1], from_child[0]);              /* 바뀌지 않았다: 스무 번을 물어도 커널에는 다시 묻지 않는다 */
    T_EQ_INT(b.cols, 100);
    T_EQ_INT(b.asked, a.asked);

    set_size(master, 240, 67);                               /* 창 크기를 바꾼다. 커널이 자식에게 SIGWINCH를 보낸다 */
    seen_t c = { 0, 0, 0, 0 };
    for (int k = 0; k < 50 && c.cols != 240; k++) {          /* 신호가 닿을 때까지 잠깐 기다린다 */
        struct timespec nap = { 0, 10 * 1000000L };
        nanosleep(&nap, NULL);
        c = ask(to_child[1], from_child[0]);
    }
    T_EQ_INT(c.cols, 240);
    T_EQ_INT(c.rows, 67);
    T_EQ_INT(c.asked, b.asked + 1);                          /* 한 번만 다시 물었다 */

    close(to_child[1]);
    int status = -1;
    waitpid(pid, &status, 0);
    T_CHECK(WIFEXITED(status) && WEXITSTATUS(status) == 0);
    close(master);
    T_DONE();
}
