/* 역대 기록 파일: 붙이고 읽기, 정렬, 틀린 줄, 이름, 잠금, 쓰다 막혔을 때 되돌리기,
 * 여러 프로세스가 같이 쓰기, 화면에 그리기 */
#include "core/ranking.h"
#include "core/toplist.h"
#include "ui/ui_internal.h"
#include "t.h"

#include <fcntl.h>
#include <signal.h>
#include <stdlib.h>
#include <string.h>
#include <sys/file.h>
#include <sys/resource.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

#define T0   1795429265LL            /* 2026-11-23T10:21:05Z */
#define TS   "2026-11-23T10:21:05Z"

static char dir[64], sub[96], path[128];
static char raw[1 << 18];

static rank_rec_t rec(const char *name, int score, int64_t when)
{
    rank_rec_t r;
    memset(&r, 0, sizeof r);
    snprintf(r.name, sizeof r.name, "%s", name);
    r.score = score;
    r.when = when;
    return r;
}

/* 파일을 통째로 raw에 읽는다. 길이를 돌려준다. */
static size_t slurp(void)
{
    FILE *f = fopen(path, "rb");
    size_t n = 0;
    if (f) {
        n = fread(raw, 1, sizeof raw - 1, f);
        fclose(f);
    }
    raw[n] = '\0';
    return n;
}

/* 파일을 주어진 바이트로 바꿔 놓는다. */
static void put_raw(const char *bytes, size_t len)
{
    FILE *f = fopen(path, "wb");
    T_CHECK(f != NULL);
    if (!f) return;
    T_EQ_INT(fwrite(bytes, 1, len, f), len);
    fclose(f);
}

static int count_char(const char *s, size_t n, char ch)
{
    int k = 0;
    for (size_t i = 0; i < n; i++) k += s[i] == ch;
    return k;
}

static void test_round_trip(void)
{
    rank_rec_t top[10];
    struct stat sb;
    T_CHECK(stat(sub, &sb) != 0);                            /* 폴더가 아직 없다 */
    T_EQ_INT(ranking_load_top(path, top, 10), 0);            /* 파일이 없으면 빈 목록 */

    rank_rec_t game[3] = { rec("HENRY", 245, T0), rec("YEONWOO", 310, T0), rec("GOEUN", -5, T0 + 1) };
    T_EQ_INT(ranking_append(path, game, 3), 0);
    T_CHECK(stat(sub, &sb) == 0 && S_ISDIR(sb.st_mode));     /* 폴더를 만들었다 */
    slurp();
    T_CHECK(strcmp(raw, "HENRY 245 " TS "\nYEONWOO 310 " TS "\nGOEUN -5 2026-11-23T10:21:06Z\n") == 0);

    T_EQ_INT(ranking_load_top(path, top, 10), 3);
    T_CHECK(strcmp(top[0].name, "YEONWOO") == 0 && top[0].score == 310 && top[0].when == T0);
    T_CHECK(strcmp(top[1].name, "HENRY") == 0 && top[1].score == 245 && top[1].when == T0);
    T_CHECK(strcmp(top[2].name, "GOEUN") == 0 && top[2].score == -5 && top[2].when == T0 + 1);

    T_EQ_INT(ranking_append(path, game, 0), 0);              /* 0개를 붙이면 아무 일도 없다 */
    T_EQ_INT(ranking_load_top(path, top, 10), 3);
    T_EQ_INT(ranking_load_top(path, top, 0), 0);
}

static void test_sorting(void)
{
    /* 점수가 높은 것이 앞, 같으면 먼저 기록한 것이 앞, 그것도 같으면 파일에서 먼저 나온 것이 앞 */
    static const struct { const char *name; int score; int when; } in[] = {
        { "A", 100, 50 }, { "B", 200, 40 }, { "C", 100, 30 }, { "D", 100, 50 }, { "E", 300, 60 }, { "F", 100, 30 },
    };
    static const char *want = "EBCFAD";
    rank_rec_t top[10];
    unlink(path);
    for (int i = 0; i < 6; i++) {
        rank_rec_t r = rec(in[i].name, in[i].score, T0 + in[i].when);
        T_EQ_INT(ranking_append(path, &r, 1), 0);
    }
    T_EQ_INT(ranking_load_top(path, top, 10), 6);
    for (int i = 0; i < 6; i++) T_EQ_INT(top[i].name[0], want[i]);

    for (int cap = 1; cap <= 5; cap++) {                     /* 위에서 cap개만 */
        memset(top, 0, sizeof top);
        T_EQ_INT(ranking_load_top(path, top, cap), cap);
        for (int i = 0; i < cap; i++) T_EQ_INT(top[i].name[0], want[i]);
        T_EQ_INT(top[cap].name[0], 0);                       /* cap 뒤는 건드리지 않는다 */
    }
}

static void test_malformed(void)
{
    char text[4096];
    size_t n = 0;
    rank_rec_t top[10];
    static const char *lines[] = {
        "GOOD1 10 " TS "\n",
        "\n",
        "garbage\n",
        "# name score time\n",
        "NOSCORE " TS "\n",
        "EXTRA 10 " TS " more\n",
        "TWO  10 " TS "\n",
        "BADNUM 12x " TS "\n",
        "PLUS +12 " TS "\n",
        "MINUS - " TS "\n",
        "BIG 99999999999 " TS "\n",
        "BIG2 3000000000 " TS "\n",
        "NOZ 10 2026-11-23T10:21:05\n",
        "MONTH 10 2026-13-23T10:21:05Z\n",
        "FEB 10 2026-02-30T10:21:05Z\n",
        "HOUR 10 2026-11-23T24:21:05Z\n",
        "EPOCH 10 1795429265\n",
        "ABCDEFGHIJKLMNOP 10 " TS "\n",                      /* 이름이 16바이트 */
        "BAD%G1 10 " TS "\n",
        "CUT%4 10 " TS "\n",
        "NUL%00 10 " TS "\n",
        " 10 " TS "\n",                                      /* 이름이 없다 */
        "TAB\tX 10 " TS "\n",
        "GOOD2 20 " TS "\r\n",                               /* 윈도우 줄바꿈은 받아 준다 */
        "lower%20case 30 " TS "\n",
        "GOOD3 -40 " TS "\n",
    };
    for (size_t i = 0; i < sizeof lines / sizeof lines[0]; i++)
        n += (size_t)snprintf(text + n, sizeof text - n, "%s", lines[i]);
    memcpy(text + n, "NU\0L 10 " TS "\n", 4 + 4 + 20 + 1);   /* 줄 안에 0바이트 */
    n += 4 + 4 + 20 + 1;
    memset(text + n, 'X', 1000);                             /* 아주 긴 줄 */
    n += 1000;
    n += (size_t)snprintf(text + n, sizeof text - n, " 10 " TS "\n");
    n += (size_t)snprintf(text + n, sizeof text - n, "GOOD4 5 " TS "\n");
    n += (size_t)snprintf(text + n, sizeof text - n, "CUT 999 2026-11-23T10:2");    /* 쓰다 끊긴 마지막 줄 */
    put_raw(text, n);

    T_EQ_INT(ranking_load_top(path, top, 10), 5);
    T_CHECK(strcmp(top[0].name, "lower case") == 0 && top[0].score == 30);
    T_CHECK(strcmp(top[1].name, "GOOD2") == 0 && top[1].score == 20 && top[1].when == T0);
    T_CHECK(strcmp(top[2].name, "GOOD1") == 0 && top[2].score == 10);
    T_CHECK(strcmp(top[3].name, "GOOD4") == 0 && top[3].score == 5);
    T_CHECK(strcmp(top[4].name, "GOOD3") == 0 && top[4].score == -40);

    /* 끊긴 줄 뒤에 붙여도 새 기록은 제 줄에 온전히 들어간다 */
    rank_rec_t r = rec("NEW", 500, T0);
    T_EQ_INT(ranking_append(path, &r, 1), 0);
    size_t len = slurp();
    static const char tail[] = "CUT 999 2026-11-23T10:2\nNEW 500 " TS "\n";
    T_CHECK(len == n + 1 + strlen("NEW 500 " TS "\n"));
    T_CHECK(len >= sizeof tail - 1 && memcmp(raw + len - (sizeof tail - 1), tail, sizeof tail - 1) == 0);
    T_EQ_INT(ranking_load_top(path, top, 10), 6);
    T_CHECK(strcmp(top[0].name, "NEW") == 0 && top[0].score == 500);

    /* 기록 한 줄을 어디에서 끊어도 기록으로 읽히지 않는다 */
    static const char whole[] = "HENRY 245 " TS;
    for (size_t cut = 0; cut < sizeof whole - 1; cut++) {
        n = (size_t)snprintf(text, sizeof text, "OK 1 " TS "\n%.*s", (int)cut, whole);
        put_raw(text, n);
        T_EQ_INT(ranking_load_top(path, top, 10), 1);
        T_EQ_INT(ranking_append(path, &r, 1), 0);
        T_EQ_INT(ranking_load_top(path, top, 10), 2);
        T_CHECK(strcmp(top[0].name, "NEW") == 0 && strcmp(top[1].name, "OK") == 0);
    }
    /* 줄바꿈만 빠진 온전한 기록은 받아 준다. 그 뒤에 붙이면 줄이 나뉜다 */
    put_raw(whole, sizeof whole - 1);
    T_EQ_INT(ranking_load_top(path, top, 10), 1);
    T_EQ_INT(ranking_append(path, &r, 1), 0);
    slurp();
    T_CHECK(strcmp(raw, "HENRY 245 " TS "\nNEW 500 " TS "\n") == 0);
    T_EQ_INT(ranking_load_top(path, top, 10), 2);
}

static void test_names(void)
{
    /* 준 이름, 파일에 적히는 꼴, 읽었을 때의 이름 */
    static const struct { const char *in, *file, *back; } c[] = {
        { "A B",                  "A%20B",                 "A B" },
        { " LEAD",                "%20LEAD",               " LEAD" },
        { "TAB\tX",               "TAB%09X",               "TAB\tX" },
        { "LINE\nBREAK\r",        "LINE%0ABREAK%0D",       "LINE\nBREAK\r" },
        { "100%",                 "100%25",                "100%" },
        { "a~b!c#",               "a~b!c#",                "a~b!c#" },
        { "\x7F" "DEL",           "%7FDEL",                "\x7F" "DEL" },
        { "",                     "PLAYER",                "PLAYER" },
        { "연우",                 "연우",                  "연우" },
        { "가나다라마",           "가나다라마",            "가나다라마" },          /* 15바이트 */
        { "\xEC\x97X",            "%EC%97X",               "\xEC\x97X" },           /* 깨진 한글 */
        { "\xFFZ",                "%FFZ",                  "\xFFZ" },
        { "\xC0\xAF",             "%C0%AF",                "\xC0\xAF" },            /* 틀린 UTF-8 */
        { "N\xC2\x85L",           "N%C2%85L",              "N\xC2\x85L" },          /* 제어 글자 U+0085 */
        { "U\xE2\x80\xA8S",       "U%E2%80%A8S",           "U\xE2\x80\xA8S" },      /* 줄을 나누는 글자 U+2028 */
        { "ABCDEFGHIJKLMNOPQRS",  "ABCDEFGHIJKLMNO",       "ABCDEFGHIJKLMNO" },     /* 15바이트에서 자른다 */
        { "A가나다라마",          "A가나다라%EB%A7",       "A가나다라\xEB\xA7" },   /* 글자 중간에서 잘린다 */
    };
    enum { N = sizeof c / sizeof c[0] };
    rank_rec_t in[N], top[N + 2];
    char want[128];
    unlink(path);
    for (int i = 0; i < N; i++) {
        memset(&in[i], 'x', sizeof in[i]);                   /* 이름이 '\0'으로 끝나지 않아도 된다 */
        size_t len = strlen(c[i].in);
        memcpy(in[i].name, c[i].in, len < sizeof in[i].name ? len + 1 : sizeof in[i].name);
        in[i].score = i;
        in[i].when = T0;
    }
    T_EQ_INT(ranking_append(path, in, N), 0);

    size_t len = slurp();
    T_EQ_INT(count_char(raw, len, '\n'), N);                 /* 기록 하나가 한 줄 */
    T_EQ_INT(count_char(raw, len, ' '), 2 * N);
    T_EQ_INT(count_char(raw, len, '\r') + count_char(raw, len, '\t'), 0);
    const char *p = raw;
    for (int i = 0; i < N; i++) {
        size_t w = (size_t)snprintf(want, sizeof want, "%s %d " TS "\n", c[i].file, i);
        if (strncmp(p, want, w) != 0) {
            t_fail++;
            printf("  실패 이름 %d: 파일에 [%.*s] (기대 [%s])\n", i, (int)strcspn(p, "\n"), p, c[i].file);
        }
        p += strcspn(p, "\n");
        if (*p) p++;
    }

    T_EQ_INT(ranking_load_top(path, top, N + 2), N);
    for (int i = 0; i < N; i++) {                            /* 점수가 큰 순서이므로 뒤에서부터 나온다 */
        const rank_rec_t *r = &top[N - 1 - i];
        T_EQ_INT(r->score, i);
        if (strcmp(r->name, c[i].back) != 0) {
            t_fail++;
            printf("  실패 이름 %d: 읽은 이름이 다르다\n", i);
        }
    }
}

static void test_lock(void)
{
    rank_rec_t top[10], r = rec("LOCK", 1, T0);
    unlink(path);
    T_EQ_INT(ranking_append(path, &r, 1), 0);
    size_t before = slurp();

    /* 남이 배타 잠금을 쥐고 있으면 기다리다 실패하고, 파일은 그대로다 */
    int fd = open(path, O_RDWR | O_CLOEXEC), usual = ranking_lock_wait_ms;
    top_msg_t m, kept;
    ranking_lock_wait_ms = 100;
    T_CHECK(fd >= 0);
    T_EQ_INT(flock(fd, LOCK_EX), 0);
    T_EQ_INT(ranking_append(path, &r, 1), -1);
    T_EQ_INT(ranking_load_top(path, top, 10), -1);
    memset(&m, 7, sizeof m);
    kept = m;
    T_EQ_INT(toplist_load(&m, path, NULL, 0), -1);           /* 읽지 못하면 메시지를 그대로 둔다 */
    T_CHECK(memcmp(&m, &kept, sizeof m) == 0);
    T_EQ_INT(slurp(), before);

    /* 공유 잠금끼리는 같이 읽는다. 쓰기는 기다린다 */
    T_EQ_INT(flock(fd, LOCK_SH), 0);
    T_EQ_INT(ranking_load_top(path, top, 10), 1);
    T_EQ_INT(ranking_append(path, &r, 1), -1);
    T_EQ_INT(slurp(), before);

    close(fd);                                               /* 닫으면 풀린다 */
    ranking_lock_wait_ms = usual;
    T_EQ_INT(ranking_append(path, &r, 1), 0);
    T_EQ_INT(ranking_load_top(path, top, 10), 2);
}

/* 쓰다가 막히면(디스크가 찬 경우를 파일 크기 한도로 흉내 낸다) 그 판의 기록을 모두 되돌린다. */
static void test_rollback(void)
{
    rank_rec_t top[10], one = rec("FIRST", 7, T0);
    rank_rec_t game[3] = { rec("AAAA", 1, T0), rec("BBBB", 2, T0), rec("CCCC", 3, T0) };
    struct rlimit old, lim;
    unlink(path);
    T_EQ_INT(ranking_append(path, &one, 1), 0);
    size_t before = slurp();

    signal(SIGXFSZ, SIG_IGN);
    T_EQ_INT(getrlimit(RLIMIT_FSIZE, &old), 0);
    lim = old;
    lim.rlim_cur = before + 40;                              /* 첫 줄은 들어가고 둘째 줄에서 끊긴다 */
    T_EQ_INT(setrlimit(RLIMIT_FSIZE, &lim), 0);
    int rc = ranking_append(path, game, 3);
    T_EQ_INT(setrlimit(RLIMIT_FSIZE, &old), 0);
    signal(SIGXFSZ, SIG_DFL);

    T_EQ_INT(rc, -1);
    T_EQ_INT(slurp(), before);                               /* 부르기 전과 같은 파일 */
    T_CHECK(strcmp(raw, "FIRST 7 " TS "\n") == 0);
    T_EQ_INT(ranking_load_top(path, top, 10), 1);
    T_EQ_INT(ranking_append(path, game, 3), 0);              /* 한도가 풀리면 다시 붙는다 */
    T_EQ_INT(ranking_load_top(path, top, 10), 4);
}

/* 프로세스 여러 개가 같은 파일에 한꺼번에 붙인다. 줄이 섞이거나 사라지면 안 되고,
 * 한 판의 기록 세 줄은 서로 붙어 있어야 한다. */
#define PROCS 4
#define GAMES 150
static void test_processes(void)
{
    pid_t pid[PROCS];
    unlink(path);
    fflush(stdout);
    for (int p = 0; p < PROCS; p++) {
        pid[p] = fork();
        T_CHECK(pid[p] >= 0);
        if (pid[p] != 0) continue;
        int bad = 0;
        for (int g = 0; g < GAMES; g++) {
            char name[16];
            snprintf(name, sizeof name, "P%d", p);
            rank_rec_t game[3] = { rec(name, g * 3, T0 + g), rec(name, g * 3 + 1, T0 + g), rec(name, g * 3 + 2, T0 + g) };
            if (ranking_append(path, game, 3) != 0) bad = 1;
        }
        _exit(bad);
    }
    /* 그동안 읽는 쪽도 계속 돈다 */
    rank_rec_t top[10];
    int read_bad = 0;
    for (int i = 0; i < 200; i++) {
        int n = ranking_load_top(path, top, 10);
        if (n < 0) read_bad++;
        for (int k = 1; k < n; k++)
            if (top[k].score > top[k - 1].score) read_bad++;
    }
    T_EQ_INT(read_bad, 0);
    for (int p = 0; p < PROCS; p++) {
        int status = -1;
        if (pid[p] > 0) waitpid(pid[p], &status, 0);
        T_CHECK(WIFEXITED(status) && WEXITSTATUS(status) == 0);
    }

    size_t len = slurp();
    T_EQ_INT(count_char(raw, len, '\n'), PROCS * GAMES * 3);
    int next_game[PROCS] = {0}, lines = 0, broken = 0;
    const char *s = raw;
    while (*s) {
        int p[3], sc[3];
        for (int j = 0; j < 3; j++) {
            char when[32];
            p[j] = sc[j] = -1;
            if (sscanf(s, "P%d %d %20s", &p[j], &sc[j], when) != 3 || strlen(when) != 20) broken++;
            s += strcspn(s, "\n");
            if (*s) s++, lines++;
        }
        /* 세 줄이 같은 프로세스의 같은 판이고, 그 프로세스가 쓴 순서대로다 */
        if (p[0] < 0 || p[0] >= PROCS || p[1] != p[0] || p[2] != p[0]) { broken++; continue; }
        if (sc[0] != next_game[p[0]] * 3 || sc[1] != sc[0] + 1 || sc[2] != sc[0] + 2) broken++;
        next_game[p[0]]++;
    }
    T_EQ_INT(broken, 0);
    T_EQ_INT(lines, PROCS * GAMES * 3);
    for (int p = 0; p < PROCS; p++) T_EQ_INT(next_game[p], GAMES);

    static rank_rec_t all[PROCS * GAMES * 3 + 8];
    T_EQ_INT(ranking_load_top(path, all, PROCS * GAMES * 3 + 8), PROCS * GAMES * 3);
}

/* 띠 밖의 픽셀 가운데 바탕색이 아닌 것의 수 */
static int outside_band(const canvas_t *c, int y0, int y1)
{
    int k = 0;
    for (int y = 0; y < c->h; y++)
        for (int x = 0; x < c->w; x++)
            if ((y < y0 || y >= y1) && c->px[y * c->w + x] != C_BG) k++;
    return k;
}

static int inside_rect(const canvas_t *c, int x0, int y0, int x1, int y1)
{
    int k = 0;
    for (int y = y0; y < y1; y++)
        for (int x = x0; x < x1; x++)
            if (c->px[y * c->w + x] != C_BG) k++;
    return k;
}

static void test_toplist(void)
{
    top_msg_t m;
    unlink(path);
    toplist_load(&m, path, NULL, 0);
    T_EQ_INT(m.n, 0);

    rank_rec_t old[2] = { rec("OLD1", 300, T0), rec("OLD2", 100, T0) };
    rank_rec_t now[2] = { rec("ME", 200, T0 + 9), rec("OLD2", 100, T0 + 9) };
    T_EQ_INT(ranking_append(path, old, 2), 0);
    T_EQ_INT(ranking_append(path, now, 2), 0);
    T_EQ_INT(toplist_load(&m, path, now, 2), 0);
    T_EQ_INT(m.n, 4);
    T_CHECK(strcmp(m.name[0], "OLD1") == 0 && m.score[0] == 300 && !m.fresh[0]);
    T_CHECK(strcmp(m.name[1], "ME") == 0 && m.score[1] == 200 && m.fresh[1]);
    T_CHECK(strcmp(m.name[2], "OLD2") == 0 && m.score[2] == 100 && !m.fresh[2]);     /* 예전 판의 같은 이름 */
    T_CHECK(strcmp(m.name[3], "OLD2") == 0 && m.score[3] == 100 && m.fresh[3]);
    toplist_load(&m, path, NULL, 0);
    T_EQ_INT(m.fresh[1] + m.fresh[3], 0);

    /* 그리기: 받은 띠 밖은 건드리지 않는다. 이름이 '\0'으로 끝나지 않거나 n이 틀려도 죽지 않는다 */
    canvas_t *c = canvas_new(SCREEN_W, SCREEN_H);
    for (int n = 0; n <= 12; n++) {
        memset(&m, 0, sizeof m);
        m.n = (uint8_t)(n == 12 ? 255 : n);
        for (int i = 0; i < PROTO_TOP_N; i++) {
            memset(m.name[i], 'W', PROTO_NAME_LEN);
            m.score[i] = i % 2 ? 2000000000 : -2000000000;
            m.fresh[i] = (uint8_t)(i % 3 == 0);
        }
        ui_clear(c, C_BG);
        toplist_draw(c, &m, TOPLIST_LOBBY_Y0, TOPLIST_LOBBY_Y1, 0);
        T_EQ_INT(outside_band(c, TOPLIST_LOBBY_Y0, TOPLIST_LOBBY_Y1), 0);
        T_CHECK(inside_rect(c, 0, TOPLIST_LOBBY_Y0, SCREEN_W, TOPLIST_LOBBY_Y1) > 0);
        /* 두 단으로 나뉘어도 단 사이가 비어 있다 */
        if (n > 5) T_EQ_INT(inside_rect(c, 117, TOPLIST_LOBBY_Y0 + 11, 123, TOPLIST_LOBBY_Y1), 0);

        ui_clear(c, C_BG);
        toplist_draw(c, &m, TOPLIST_RANK_Y0, TOPLIST_RANK_Y1, 1);
        T_EQ_INT(outside_band(c, TOPLIST_RANK_Y0, TOPLIST_RANK_Y1), 0);
        T_CHECK(inside_rect(c, 0, TOPLIST_RANK_Y0, SCREEN_W, TOPLIST_RANK_Y1) > 0);
    }
    /* 너무 낮은 띠에는 아무것도 그리지 않는다 */
    ui_clear(c, C_BG);
    toplist_draw(c, &m, 60, 70, 1);
    T_EQ_INT(inside_rect(c, 0, 0, SCREEN_W, SCREEN_H), 0);
    canvas_free(c);
}

int main(void)
{
    snprintf(dir, sizeof dir, "/tmp/ssp_ranking_XXXXXX");
    if (!mkdtemp(dir)) {
        printf("  실패: 임시 폴더를 만들 수 없다\n");
        return 1;
    }
    snprintf(sub, sizeof sub, "%s/sub", dir);
    snprintf(path, sizeof path, "%s/ranking.txt", sub);

    test_round_trip();
    test_sorting();
    test_malformed();
    test_names();
    test_lock();
    test_rollback();
    test_processes();
    test_toplist();

    unlink(path);
    rmdir(sub);
    rmdir(dir);
    T_DONE();
}
