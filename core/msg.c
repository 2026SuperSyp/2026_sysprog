/* 메시지를 보내고 받는 공용 코드. 설명은 msg.h에 있다. */
#include "core/msg.h"

#include <errno.h>
#include <string.h>
#include <unistd.h>

/* n바이트를 다 쓴다. write는 일부만 쓰고 돌아올 수 있다. */
static int write_all(int fd, const void *buf, size_t n)
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

/* n바이트를 다 읽는다. 1: 읽었다, 0: 첫 바이트 전에 닫혔다, -1: 오류 또는 도중에 닫혔다. */
static int read_all(int fd, void *buf, size_t n)
{
    char *p = buf;
    size_t got = 0;
    while (got < n) {
        ssize_t r = read(fd, p + got, n - got);
        if (r < 0) {
            if (errno == EINTR) continue;
            return -1;
        }
        if (r == 0) return got == 0 ? 0 : -1;
        got += (size_t)r;
    }
    return 1;
}

int msg_send(int fd, uint8_t type, uint8_t flags, uint16_t slot, const void *body, uint32_t len)
{
    msg_hdr_t h = { len, type, flags, slot };
    if (write_all(fd, &h, sizeof h) != 0) return -1;
    if (len > 0 && write_all(fd, body, len) != 0) return -1;
    return 0;
}

int msg_recv(int fd, msg_hdr_t *h, void *body, uint32_t cap)
{
    int r = read_all(fd, h, sizeof *h);
    if (r <= 0) return r;
    if (h->len > cap) return -1;
    if (h->len > 0 && read_all(fd, body, h->len) != 1) return -1;
    return 1;
}

int msg_recv_some(int fd, msg_part_t *part, void *body, uint32_t cap)
{
    for (;;) {
        ssize_t r;
        if (part->got < sizeof part->h) {
            r = read(fd, (char *)&part->h + part->got, sizeof part->h - part->got);
        } else {
            uint32_t have = part->got - (uint32_t)sizeof part->h;
            if (part->h.len > cap) return -1;
            if (have == part->h.len) { part->got = 0; return 1; }
            r = read(fd, (char *)body + have, part->h.len - have);
        }
        if (r < 0) {
            if (errno == EINTR) continue;
            return errno == EAGAIN ? 0 : -1;
        }
        if (r == 0) return -1;
        part->got += (uint32_t)r;
    }
}

int msg_send_once(int fd, uint8_t type, uint8_t flags, uint16_t slot, const void *body, uint32_t len)
{
    uint8_t buf[sizeof(msg_hdr_t) + MSG_ONCE_MAX];
    msg_hdr_t h = { len, type, flags, slot };
    if (len > MSG_ONCE_MAX) return -1;
    memcpy(buf, &h, sizeof h);
    if (len > 0) memcpy(buf + sizeof h, body, len);
    for (;;) {
        ssize_t w = write(fd, buf, sizeof h + len);
        if (w == (ssize_t)(sizeof h + len)) return 1;
        if (w < 0 && errno == EINTR) continue;
        return (w < 0 && errno == EAGAIN) ? 0 : -1;
    }
}

size_t rle_pack(const uint8_t *px, size_t n, uint8_t *out, size_t cap)
{
    size_t o = 0;
    for (size_t i = 0; i < n; ) {
        size_t run = 1;
        while (i + run < n && run < 255 && px[i + run] == px[i]) run++;
        if (o + 2 > cap) return 0;
        out[o++] = (uint8_t)run;
        out[o++] = px[i];
        i += run;
    }
    return o;
}

int rle_unpack(const uint8_t *in, size_t len, uint8_t *px, size_t n)
{
    size_t o = 0;
    if (len % 2 != 0) return -1;
    for (size_t i = 0; i < len; i += 2) {
        size_t run = in[i];
        if (run == 0 || o + run > n) return -1;
        memset(px + o, in[i + 1], run);
        o += run;
    }
    return o == n ? 0 : -1;
}
