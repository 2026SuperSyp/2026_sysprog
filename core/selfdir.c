/* 지금 도는 실행 파일이 있는 폴더를 알아낸다. 리눅스의 /proc/self/exe 를 읽는다. */
#include "core/selfdir.h"

#include <pthread.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#define FALLBACK "build/dev"

static char dir[SELFDIR_MAX];
static pthread_once_t once = PTHREAD_ONCE_INIT;

static void resolve(void)
{
    static char exe[SELFDIR_MAX], cwd[SELFDIR_MAX];
    ssize_t n = readlink("/proc/self/exe", exe, sizeof exe - 1);
    char *slash;

    /* 꽉 찼으면 잘렸을 수 있다. 그 경로는 믿지 않는다 */
    if (n <= 0 || n >= (ssize_t)sizeof exe - 1) n = 0;
    exe[n] = '\0';
    slash = strrchr(exe, '/');
    if (exe[0] != '/' || !slash) {
        fprintf(stderr, "실행 파일의 위치를 알 수 없습니다. %s 에서 찾습니다\n", FALLBACK);
        snprintf(dir, sizeof dir, "%s", FALLBACK);
        return;
    }
    *slash = '\0';                                   /* 파일 이름을 떼어 폴더만 남긴다 */

    /* 현재 폴더 아래에 있으면 그 부분을 뗀다. 저장소 폴더에서 띄우면 build/dev 처럼 짧게 남는다 */
    const char *p = exe;
    if (getcwd(cwd, sizeof cwd)) {
        size_t len = strlen(cwd);
        if (len > 1 && strncmp(exe, cwd, len) == 0 && exe[len] == '/' && exe[len + 1] != '\0') p = exe + len + 1;
    }
    snprintf(dir, sizeof dir, "%s", p);
}

const char *selfdir(void)
{
    pthread_once(&once, resolve);
    return dir;
}
