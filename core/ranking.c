/* 역대 기록 파일을 붙이고 읽는다. 파일의 꼴과 잠금 규칙은 ranking.h에 있다. */
#include "core/ranking.h"

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <sys/file.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

#define LINE_MAX_LEN  128        /* 가장 긴 기록 한 줄(이름 45 + 점수 11 + 시각 20 + 빈칸 2)보다 넉넉하다 */
#define TIME_LEN      20         /* 2026-11-23T10:21:05Z */
#define LOCK_NAP_MS   1
#define WHEN_MAX      253402300799LL     /* 9999-12-31T23:59:59Z. 이 뒤로는 연도가 네 자리를 넘는다 */

int ranking_lock_wait_ms = 2000;

/* ---- 잠금 -------------------------------------------------------------------- */
/* 잠금을 잡을 때까지 조금씩 쉬며 다시 해 본다. 0이면 잡았다.
 * 끝없이 기다리지 않는 것은 서버의 게임 마스터가 여기서 멈춰 있으면 안 되기 때문이다. */
static int lock_wait(int fd, int how)
{
    for (int waited = 0; ; waited += LOCK_NAP_MS) {
        if (flock(fd, how | LOCK_NB) == 0) return 0;
        if (errno != EWOULDBLOCK && errno != EINTR) return -1;
        if (waited >= ranking_lock_wait_ms) return -1;
        struct timespec nap = { 0, LOCK_NAP_MS * 1000000L };
        nanosleep(&nap, NULL);
    }
}

/* ---- 이름 -------------------------------------------------------------------- */
/* s에서 시작하는 올바른 UTF-8 글자의 바이트 수(2~4)를 돌려준다. 그대로 적으면 안 되면 0.
 * 범위는 UTF-8 정의(RFC 3629)를 따른다. */
static int utf8_len(const unsigned char *s, size_t left)
{
    int n = (s[0] >= 0xC2 && s[0] <= 0xDF) ? 2 : (s[0] >= 0xE0 && s[0] <= 0xEF) ? 3 :
            (s[0] >= 0xF0 && s[0] <= 0xF4) ? 4 : 0;
    if (n == 0 || (size_t)n > left) return 0;
    for (int i = 1; i < n; i++)
        if ((s[i] & 0xC0) != 0x80) return 0;
    if (s[0] == 0xE0 && s[1] < 0xA0) return 0;       /* 더 짧게 적을 수 있는 꼴 */
    if (s[0] == 0xED && s[1] > 0x9F) return 0;       /* 글자가 아닌 구간(U+D800~DFFF) */
    if (s[0] == 0xF0 && s[1] < 0x90) return 0;
    if (s[0] == 0xF4 && s[1] > 0x8F) return 0;       /* U+10FFFF를 넘는다 */
    if (s[0] == 0xC2 && s[1] < 0xA0) return 0;       /* 제어 글자(U+0080~009F) */
    if (s[0] == 0xE2 && s[1] == 0x80 && (s[2] == 0xA8 || s[2] == 0xA9)) return 0;   /* 줄을 나누는 글자 */
    return n;
}

/* 이름을 파일에 적는 꼴로 바꾼다. out은 RANKING_NAME_LEN * 3바이트 이상이어야 한다. */
static size_t escape_name(const char *name, char *out)
{
    static const char hex[] = "0123456789ABCDEF";
    const unsigned char *s = (const unsigned char *)name;
    size_t len = strnlen(name, RANKING_NAME_LEN - 1), o = 0;
    if (len == 0) {
        s = (const unsigned char *)"PLAYER";
        len = 6;
    }
    for (size_t i = 0; i < len; ) {
        int u = s[i] >= 0x80 ? utf8_len(s + i, len - i) : 0;
        if (u > 0) {
            memcpy(out + o, s + i, (size_t)u);
            o += (size_t)u;
            i += (size_t)u;
        } else if (s[i] > 0x20 && s[i] < 0x7F && s[i] != '%') {
            out[o++] = (char)s[i++];
        } else {
            out[o++] = '%';
            out[o++] = hex[s[i] >> 4];
            out[o++] = hex[s[i] & 15];
            i++;
        }
    }
    out[o] = '\0';
    return o;
}

static int hex_value(int ch)
{
    if (ch >= '0' && ch <= '9') return ch - '0';
    if (ch >= 'A' && ch <= 'F') return ch - 'A' + 10;
    if (ch >= 'a' && ch <= 'f') return ch - 'a' + 10;
    return -1;
}

/* 파일에 적힌 이름을 원래 바이트로 되돌린다. 0이면 성공. */
static int unescape_name(const char *s, size_t len, char out[RANKING_NAME_LEN])
{
    size_t o = 0;
    for (size_t i = 0; i < len; i++) {
        int ch = (unsigned char)s[i];
        if (ch == '%') {
            if (i + 2 >= len) return -1;
            int hi = hex_value((unsigned char)s[i + 1]), lo = hex_value((unsigned char)s[i + 2]);
            if (hi < 0 || lo < 0) return -1;
            ch = hi * 16 + lo;
            i += 2;
            if (ch == 0) return -1;
        } else if (ch <= 0x20 || ch == 0x7F) {
            return -1;
        }
        if (o >= RANKING_NAME_LEN - 1) return -1;
        out[o++] = (char)ch;
    }
    if (o == 0) return -1;
    memset(out + o, 0, RANKING_NAME_LEN - o);
    return 0;
}

/* ---- 시각 -------------------------------------------------------------------- */
/* 0 이상인 수를 정해진 자릿수로 적는다. */
static void put_digits(char *out, int v, int digits)
{
    for (int i = digits - 1; i >= 0; i--) {
        out[i] = (char)('0' + v % 10);
        v /= 10;
    }
}

static void format_time(int64_t when, char out[TIME_LEN + 1])
{
    struct tm tm;
    time_t t = (time_t)(when < 0 ? 0 : (when > WHEN_MAX ? WHEN_MAX : when));
    gmtime_r(&t, &tm);
    memcpy(out, "0000-00-00T00:00:00Z", TIME_LEN + 1);
    put_digits(out, tm.tm_year + 1900, 4);
    put_digits(out + 5, tm.tm_mon + 1, 2);
    put_digits(out + 8, tm.tm_mday, 2);
    put_digits(out + 11, tm.tm_hour, 2);
    put_digits(out + 14, tm.tm_min, 2);
    put_digits(out + 17, tm.tm_sec, 2);
}

static int parse_time(const char *s, size_t len, int64_t *when)
{
    static const char shape[] = "dddd-dd-ddTdd:dd:ddZ";
    char again[TIME_LEN + 1];
    int v[6] = {0}, k = 0;
    if (len != TIME_LEN) return -1;
    for (int i = 0; i < TIME_LEN; i++) {
        if (shape[i] != 'd') {
            if (s[i] != shape[i]) return -1;
            k++;
        } else if (s[i] < '0' || s[i] > '9') {
            return -1;
        } else {
            v[k] = v[k] * 10 + (s[i] - '0');
        }
    }
    struct tm tm = { .tm_year = v[0] - 1900, .tm_mon = v[1] - 1, .tm_mday = v[2],
                     .tm_hour = v[3], .tm_min = v[4], .tm_sec = v[5] };
    time_t t = timegm(&tm);
    /* 13월이나 2월 30일 같은 없는 날짜는 다시 적어 보면 달라진다 */
    format_time((int64_t)t, again);
    if (t < 0 || memcmp(again, s, TIME_LEN) != 0) return -1;
    *when = (int64_t)t;
    return 0;
}

/* ---- 줄 ---------------------------------------------------------------------- */
/* 기록 하나를 줄바꿈까지 한 줄로 적는다. 쓴 길이를 돌려준다. out은 LINE_MAX_LEN 이상. */
static size_t format_line(const rank_rec_t *r, char *out)
{
    char name[RANKING_NAME_LEN * 3], when[TIME_LEN + 1];
    escape_name(r->name, name);
    format_time(r->when, when);
    return (size_t)snprintf(out, LINE_MAX_LEN, "%s %d %s\n", name, (int)r->score, when);
}

/* 줄바꿈을 뺀 한 줄을 읽는다. 0이면 올바른 기록이다. */
static int parse_line(const char *s, size_t len, rank_rec_t *r)
{
    if (len > 0 && s[len - 1] == '\r') len--;        /* 윈도우 편집기로 고친 파일 */
    const char *sp1 = memchr(s, ' ', len);
    if (!sp1) return -1;
    const char *num = sp1 + 1;
    const char *sp2 = memchr(num, ' ', len - (size_t)(num - s));
    if (!sp2) return -1;
    const char *tim = sp2 + 1;

    if (unescape_name(s, (size_t)(sp1 - s), r->name) != 0) return -1;

    /* 점수: 부호와 숫자 열 자리까지 */
    int neg = (*num == '-');
    const char *d = num + neg;
    int64_t v = 0;
    if (d == sp2 || sp2 - d > 10) return -1;
    for (; d < sp2; d++) {
        if (*d < '0' || *d > '9') return -1;
        v = v * 10 + (*d - '0');
    }
    if (neg) v = -v;
    if (v < INT32_MIN || v > INT32_MAX) return -1;
    r->score = (int32_t)v;

    return parse_time(tim, len - (size_t)(tim - s), &r->when);
}

/* ---- 붙이기 ------------------------------------------------------------------- */
/* 파일이 들어갈 폴더를 만든다. 이미 있으면 그대로 둔다. */
static void make_parent_dirs(const char *path)
{
    char dir[512];
    size_t len = strlen(path);
    if (len >= sizeof dir) return;
    memcpy(dir, path, len + 1);
    for (size_t i = 1; i < len; i++) {
        if (dir[i] != '/') continue;
        dir[i] = '\0';
        mkdir(dir, 0755);
        dir[i] = '/';
    }
}

/* 파일을 열고 잠근다. 잠금을 기다리는 사이에 다른 프로세스가 파일을 정리해서 바꿔 놓았으면(rename) 내가 잠근 것은
 * 이미 이름이 없는 옛 파일이다. 그래서 잠근 뒤에 "지금 그 이름의 파일"과 같은 파일인지 확인하고, 다르면 새로 연다 */
static int open_locked(const char *path, int flags, int how)
{
    for (int tries = 0; tries < 50; tries++) {
        int fd = open(path, flags | O_CLOEXEC, 0644);
        if (fd < 0) return -1;
        if (lock_wait(fd, how) != 0) {
            close(fd);
            errno = EAGAIN;
            return -1;
        }
        struct stat mine, named;
        if (fstat(fd, &mine) == 0 && stat(path, &named) == 0 && mine.st_ino == named.st_ino && mine.st_dev == named.st_dev)
            return fd;
        close(fd);
    }
    errno = EAGAIN;
    return -1;
}

/* path가 들어 있는 폴더를 디스크에 적는다. 새로 만든 파일과 rename은 폴더의 내용이 바뀐 것이라, 파일만 fsync해서는
 * 전원이 나갔을 때 그 이름이 남는다는 보장이 없다 */
static void sync_parent(const char *path)
{
    char dir[512];
    const char *slash = strrchr(path, '/');
    size_t len = slash ? (size_t)(slash - path) : 0;
    if (len >= sizeof dir) return;
    if (len == 0) { dir[0] = slash ? '/' : '.'; len = 1; }
    else memcpy(dir, path, len);
    dir[len] = '\0';
    int fd = open(dir, O_RDONLY | O_DIRECTORY | O_CLOEXEC);
    if (fd < 0) return;
    if (fsync(fd) != 0) { /* 폴더를 fsync할 수 없는 파일 시스템도 있다. 파일은 이미 적혔다 */ }
    close(fd);
}

static int write_all(int fd, const char *buf, size_t len)
{
    while (len > 0) {
        ssize_t w = write(fd, buf, len);
        if (w < 0 && errno == EINTR) continue;
        if (w <= 0) return -1;
        buf += w;
        len -= (size_t)w;
    }
    return 0;
}

static int insert_top(rank_rec_t *out, int n, int cap, const rank_rec_t *r);
static int read_top(int fd, rank_rec_t *out, int cap);

int ranking_compact_bytes = 256 * 1024;

/* 파일이 커졌으면 높은 순위 RANKING_KEEP개만 남긴다. 잠근 채로 부른다(fd가 그 파일이다).
 * 새 내용을 옆의 임시 파일에 다 쓰고 디스크에 적은 뒤(fsync) 이름을 바꿔 치운다(rename). rename은 한 번에 일어나므로
 * 도중에 죽어도 랭킹 파일은 옛 것 아니면 새 것이고, 반만 쓰인 파일이 되는 일은 없다. 실패하면 옛 파일이 그대로 남는다 */
static void compact(const char *path, int fd)
{
    static rank_rec_t keep[RANKING_KEEP];
    char tmp[512];
    if (snprintf(tmp, sizeof tmp, "%s.tmp", path) >= (int)sizeof tmp) return;
    if (lseek(fd, 0, SEEK_SET) != 0) return;
    int n = read_top(fd, keep, RANKING_KEEP);
    if (n <= 0) return;
    int out = open(tmp, O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, 0644);
    if (out < 0) return;
    int ok = 1;
    for (int i = 0; i < n && ok; i++) {
        char line[LINE_MAX_LEN + 1];
        size_t len = format_line(&keep[i], line);
        ok = write_all(out, line, len) == 0;
    }
    if (ok && fsync(out) != 0) ok = 0;
    close(out);
    if (ok && rename(tmp, path) == 0) sync_parent(path);
    else unlink(tmp);
}

int ranking_append(const char *path, const rank_rec_t *recs, int n)
{
    if (!path || !recs || n <= 0) return n == 0 ? 0 : -1;
    make_parent_dirs(path);
    struct stat before;
    int was_new = stat(path, &before) != 0;
    int fd = open_locked(path, O_RDWR | O_APPEND | O_CREAT, LOCK_EX);
    if (fd < 0) return -1;

    int rc = -1;
    struct stat sb;
    char last = '\n';
    if (fstat(fd, &sb) != 0) goto out;
    if (sb.st_size > 0 && pread(fd, &last, 1, sb.st_size - 1) != 1) goto out;

    for (int i = 0; i < n; i++) {
        char line[LINE_MAX_LEN + 1];
        size_t len = 0;
        /* 앞사람이 쓰다 끊긴 줄이 남아 있으면 줄을 바꿔서 새 기록과 붙지 않게 한다 */
        if (i == 0 && last != '\n') line[len++] = '\n';
        len += format_line(&recs[i], line + len);
        ssize_t w;
        do w = write(fd, line, len); while (w < 0 && errno == EINTR);
        if (w != (ssize_t)len) {
            /* 디스크가 찼을 때 등. 이 판에서 쓴 것을 지운다. 배타 잠금 안이라 남의 기록은 섞여 있지 않다 */
            if (ftruncate(fd, sb.st_size) != 0) { /* 남은 조각은 읽을 때 틀린 줄로 걸러진다 */ }
            goto out;
        }
    }
    /* 디스크에 적힐 때까지 기다린다. write는 커널의 메모리에 넣었을 뿐이라, 여기서 돌아간 뒤에 전원이 나가도
     * 이 판의 기록이 남으려면 fsync가 필요하다. 못 적었으면 이 판의 기록을 지우고 실패로 돌려준다 */
    if (fsync(fd) != 0) {
        if (ftruncate(fd, sb.st_size) != 0) { /* 위와 같다 */ }
        goto out;
    }
    if (was_new) sync_parent(path);
    rc = 0;
    if (fstat(fd, &sb) == 0 && sb.st_size > ranking_compact_bytes) compact(path, fd);
out:
    close(fd);                                       /* 닫으면 잠금도 풀린다 */
    return rc;
}

/* ---- 읽기 -------------------------------------------------------------------- */
/* a가 b보다 앞 순위인가 */
static int ranks_before(const rank_rec_t *a, const rank_rec_t *b)
{
    if (a->score != b->score) return a->score > b->score;
    return a->when < b->when;
}

/* 순서를 지키며 끼워 넣는다. 같은 점수, 같은 시각이면 파일에서 먼저 나온 것이 앞이다. */
static int insert_top(rank_rec_t *out, int n, int cap, const rank_rec_t *r)
{
    int at = n;
    while (at > 0 && ranks_before(r, &out[at - 1])) at--;
    if (at >= cap) return n;
    if (n < cap) n++;
    memmove(&out[at + 1], &out[at], (size_t)(n - 1 - at) * sizeof *out);
    out[at] = *r;
    return n;
}

/* 열려 있는 파일을 지금 자리부터 끝까지 읽어 높은 순위 cap개를 고른다. 읽지 못하면 -1 */
static int read_top(int fd, rank_rec_t *out, int cap)
{
    char buf[4096], line[LINE_MAX_LEN];
    size_t len = 0;
    int n = 0, too_long = 0;
    rank_rec_t r;
    for (;;) {
        ssize_t got = read(fd, buf, sizeof buf);
        if (got < 0 && errno == EINTR) continue;
        if (got < 0) { n = -1; break; }
        if (got == 0) {
            /* 줄바꿈 없이 끝난 마지막 줄. 온전한 기록일 때만 쓴다 */
            if (len > 0 && !too_long && parse_line(line, len, &r) == 0) n = insert_top(out, n, cap, &r);
            break;
        }
        for (ssize_t i = 0; i < got; i++) {
            if (buf[i] != '\n') {
                if (len < sizeof line) line[len++] = buf[i];
                else too_long = 1;                   /* 기록일 수 없는 긴 줄은 줄 끝까지 버린다 */
                continue;
            }
            if (!too_long && parse_line(line, len, &r) == 0) n = insert_top(out, n, cap, &r);
            len = 0;
            too_long = 0;
        }
    }
    return n;
}

int ranking_load_top(const char *path, rank_rec_t *out, int cap)
{
    if (!path || !out || cap <= 0) return 0;
    int fd = open_locked(path, O_RDONLY, LOCK_SH);
    if (fd < 0) return errno == ENOENT ? 0 : -1;
    int n = read_top(fd, out, cap);
    close(fd);
    return n;
}
