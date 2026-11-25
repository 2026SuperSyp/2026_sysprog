/* 에어팟 프로의 머리 움직임 센서 읽기. 쓰는 법은 airpods.h.
 *
 * 규격의 출처(애플은 공개하지 않았다. 아래는 통로를 관찰해 적은 공개 문서이고, 여기서는 그 문서의 사실만 썼다)
 *   [L] LibrePods의 "AAP Definitions" 문서: 통로 번호, 첫 인사, 기능·알림 요청, 머리 추적 시작 패킷
 *       github.com/librepods-org/librepods (docs/AAP Definitions.md, 에어팟 프로 2에서 잡은 것)
 *   [P] 같은 저장소의 문서 수정 제안 887번: 요청 패킷의 칸 구성, 흐름 14와 16, 표본 58바이트의 자리(에어팟 프로 3)
 *   [M] MousePods의 문서(docs/DISCOVERIES.md, REMOTEHID-PROTOCOL.md): 받는 크기 한도 2582, 회전 빠르기와 중력의 단위
 *       github.com/starman-tech/mousepods
 *
 * 주고받는 순서
 *   1. 첫 인사(16바이트). 이것을 보내기 전에는 에어팟이 아무 대답도 하지 않는다 [L]
 *   2. 기능 알림(0x4D), 알림 요청(0x0F) [L]. 머리 추적에 꼭 필요한지는 어느 문서에도 없지만 다들 보낸다
 *   3. 흐름 14를 켜 달라는 요청, 이어서 흐름 16을 켜 달라는 요청 [P]. 움직임 표본은 16으로 오는데, 14를 한 번은
 *      요청해야 16이 나온다. 옛 펌웨어는 14로 표본을 보냈다. 둘 다 요청하면 어느 쪽이든 된다
 *   4. 에어팟이 정한 간격마다 표본 패킷(80바이트쯤)을 보낸다. 끝의 58바이트가 표본이다
 *
 * 요청 패킷의 구성 [P]
 *   04 00 04 00 | 17 00 | 00 00 10 00 | LL 00 | 08 SS | 42 0B | 08 ST | 10 02 | 1A 05 01 II II II II
 *   머리 4바이트, 명령 0x17, 고정 4바이트, 뒤따르는 길이, 일련번호, 11바이트 묶음, 흐름 번호, 늘 2, 간격(마이크로초)
 *
 * 확인하지 못한 것: 이 코드는 실물 없이 문서만 보고 썼다. 단위(회전 빠르기 0.122도/초)는 에어팟 3세대에서 잰 값이라
 * 프로에서는 15%쯤 다를 수 있고, 좌우의 부호도 실물로 확인하지 못했다. 그래서 클라이언트에 배율 옵션을 두었고
 * 음수를 주면 방향이 뒤집힌다. */
#include "ui/airpods.h"

#include <endian.h>
#include <errno.h>
#include <fcntl.h>
#include <math.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <time.h>
#include <unistd.h>

/* ---- 커널의 블루투스 소켓 인터페이스 ------------------------------------------------------
 * 이 상수와 구조체는 보통 libbluetooth의 헤더에 있다. 그 라이브러리를 깔지 않아도 빌드되게 필요한 것만 여기 적는다.
 * 커널과 주고받는 모양이라 바뀌지 않는다 */
#ifndef AF_BLUETOOTH
#define AF_BLUETOOTH 31
#endif
#define BT_PROTO_L2CAP   0           /* socket()의 셋째 인자 */
#define BT_SOL_BLUETOOTH 274         /* setsockopt의 단계: 블루투스 공통 */
#define BT_OPT_SECURITY  4           /*   그 선택지: 암호화 수준 */
#define BT_LEVEL_MEDIUM  2           /*   짝지은 기기와 암호화된 접속 */
#define BT_SOL_L2CAP     6           /* setsockopt의 단계: L2CAP */
#define BT_OPT_L2CAP     1           /*   그 선택지: 통로의 설정 */

struct bt_addr_l2 {                  /* connect에 넘기는 주소(커널의 struct sockaddr_l2) */
    sa_family_t family;
    uint16_t psm;                    /* 통로 번호. 작은 쪽 바이트가 먼저 */
    uint8_t bdaddr[6];               /* 기기 주소. 글로 쓴 것과 순서가 거꾸로다 */
    uint16_t cid;
    uint8_t bdaddr_type;             /* 0이면 보통의 블루투스(BR/EDR) */
};
struct bt_level { uint8_t level, key_size; };
struct bt_l2cap_opts { uint16_t omtu, imtu, flush_to; uint8_t mode, fcs, max_tx; uint16_t txwin_size; };

#define AAP_PSM 0x1001

/* ---- 패킷 ------------------------------------------------------------------------------- */
static const uint8_t HELLO[]    = { 0x00, 0x00, 0x04, 0x00, 0x01, 0x00, 0x02, 0x00, 0, 0, 0, 0, 0, 0, 0, 0 };
static const uint8_t FEATURES[] = { 0x04, 0x00, 0x04, 0x00, 0x4d, 0x00, 0xff, 0, 0, 0, 0, 0, 0, 0 };
static const uint8_t NOTIFY[]   = { 0x04, 0x00, 0x04, 0x00, 0x0f, 0x00, 0xff, 0xff, 0xff, 0xff };
static const uint8_t SENSOR[]   = { 0x04, 0x00, 0x04, 0x00, 0x17, 0x00, 0x00, 0x00, 0x10, 0x00 };  /* 센서 패킷의 앞 10바이트 */

#define STREAM_STATE  14
#define STREAM_MOTION 16
#define BLOCK         58             /* 표본 하나의 크기 */

static int16_t le16(const uint8_t *p) { return (int16_t)(uint16_t)(p[0] | (p[1] << 8)); }

int airpods_parse(const uint8_t *pkt, size_t len, airpods_sample_t *out)
{
    /* 앞 10바이트가 센서 패킷이고, 적힌 길이가 실제와 맞고, 끝의 58바이트가 "표본"(첫 바이트 1)이어야 한다.
     * 가운데의 일련번호가 127을 넘으면 한 바이트 늘어나므로 자리는 끝에서부터 센다 [P] */
    if (len < sizeof SENSOR + 2 + BLOCK || memcmp(pkt, SENSOR, sizeof SENSOR) != 0) return 0;
    if ((size_t)(pkt[10] | (pkt[11] << 8)) != len - 12) return 0;
    const uint8_t *b = pkt + len - BLOCK;
    if (b[0] != 0x01) return 0;
    out->t_ns = 0;
    for (int i = 0; i < 8; i++) out->t_ns |= (uint64_t)b[1 + i] << (8 * i);
    for (int i = 0; i < 3; i++) {
        out->att[i]   = le16(b + 20 + 2 * i);
        out->gyro[i]  = le16(b + 26 + 2 * i);
        out->accel[i] = le16(b + 38 + 2 * i);
        out->grav[i]  = le16(b + 44 + 2 * i);
    }
    return 1;
}

size_t airpods_request(uint8_t *buf, uint32_t seq, uint8_t stream, uint32_t interval_us)
{
    size_t n = sizeof SENSOR, body;
    memcpy(buf, SENSOR, n);
    n += 2;                                          /* 길이는 다 쓴 뒤에 채운다 */
    body = n;
    buf[n++] = 0x08;                                 /* 일련번호. 7비트씩 끊어 적고, 뒤에 더 있으면 맨 윗비트를 켠다 */
    do {
        uint8_t low = seq & 0x7f;
        seq >>= 7;
        buf[n++] = (uint8_t)(low | (seq ? 0x80 : 0));
    } while (seq);
    buf[n++] = 0x42; buf[n++] = 0x0b;                /* 11바이트 묶음 */
    buf[n++] = 0x08; buf[n++] = stream;
    buf[n++] = 0x10; buf[n++] = 0x02;
    buf[n++] = 0x1a; buf[n++] = 0x05; buf[n++] = 0x01;
    for (int i = 0; i < 4; i++) buf[n++] = (uint8_t)(interval_us >> (8 * i));
    buf[10] = (uint8_t)(n - body);
    buf[11] = 0;
    return n;
}

/* ---- 통로 ------------------------------------------------------------------------------- */
static int fail(int fd, char *err, size_t cap, const char *what)
{
    snprintf(err, cap, "%s(%s)", what, strerror(errno));
    if (fd >= 0) close(fd);
    return -1;
}

int airpods_open(const char *addr, char *err, size_t cap)
{
    unsigned b[6];
    char tail;
    struct bt_addr_l2 to = { .family = AF_BLUETOOTH, .psm = htole16(AAP_PSM) };
    if (sscanf(addr, "%2x:%2x:%2x:%2x:%2x:%2x%c", &b[0], &b[1], &b[2], &b[3], &b[4], &b[5], &tail) != 6) {
        snprintf(err, cap, "주소는 00:11:22:33:44:55 꼴입니다");
        return -1;
    }
    for (int i = 0; i < 6; i++) to.bdaddr[i] = (uint8_t)b[5 - i];

    int fd = socket(AF_BLUETOOTH, SOCK_SEQPACKET | SOCK_CLOEXEC, BT_PROTO_L2CAP);
    if (fd < 0) return fail(-1, err, cap, "이 컴퓨터의 리눅스에 블루투스가 없습니다");

    /* 암호화된 접속을 요구한다. 기본 수준이면 에어팟이 접속은 받아 주고 첫 인사에도 대답하지 않는다 [P] */
    struct bt_level lv = { .level = BT_LEVEL_MEDIUM };
    if (setsockopt(fd, BT_SOL_BLUETOOTH, BT_OPT_SECURITY, &lv, sizeof lv) != 0) return fail(fd, err, cap, "암호화 수준을 정하지 못했습니다");

    /* 받는 크기 한도를 올린다. 지금 설정을 읽어 그 칸만 바꿔 다시 쓴다. 못 바꿔도 프로 계열은 될 수 있어 넘어간다 [M] */
    struct bt_l2cap_opts o;
    socklen_t ol = sizeof o;
    if (getsockopt(fd, BT_SOL_L2CAP, BT_OPT_L2CAP, &o, &ol) == 0) {
        o.imtu = AIRPODS_MTU;
        setsockopt(fd, BT_SOL_L2CAP, BT_OPT_L2CAP, &o, sizeof o);
    }

    /* 꺼져 있거나 멀리 있으면 connect가 오래 기다린다. 블루투스 소켓의 connect는 보내기 한도 시간을 따른다 */
    struct timeval tv = { .tv_sec = 8 };
    setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof tv);
    if (connect(fd, (struct sockaddr *)&to, sizeof to) != 0)
        return fail(fd, err, cap, "에어팟에 접속하지 못했습니다. 짝지어져 있고 켜져 있는지 확인하세요");

    int fl = fcntl(fd, F_GETFL);
    if (fl < 0 || fcntl(fd, F_SETFL, fl | O_NONBLOCK) != 0) return fail(fd, err, cap, "소켓 설정 실패");
    return fd;
}

static int put(int fd, const uint8_t *p, size_t n, long wait_ms)
{
    ssize_t w;
    do w = send(fd, p, n, MSG_NOSIGNAL); while (w < 0 && errno == EINTR);
    if (w != (ssize_t)n) return -1;
    struct timespec ts = { 0, wait_ms * 1000000L };
    while (nanosleep(&ts, &ts) != 0 && errno == EINTR) { }
    return 0;
}

int airpods_start(int fd, uint32_t interval_us)
{
    uint8_t req[AIRPODS_REQ_MAX];
    /* 첫 세 패킷 사이는 0.1초 넘게 띄우는 것이 낫다고 한다(같은 저장소의 제안 797번) */
    if (put(fd, HELLO, sizeof HELLO, 150) != 0 || put(fd, FEATURES, sizeof FEATURES, 150) != 0 ||
        put(fd, NOTIFY, sizeof NOTIFY, 150) != 0) return -1;
    if (put(fd, req, airpods_request(req, 1, STREAM_STATE, interval_us), 50) != 0) return -1;
    return put(fd, req, airpods_request(req, 2, STREAM_MOTION, interval_us), 0);
}

void airpods_stop(int fd)
{
    uint8_t req[AIRPODS_REQ_MAX];                    /* 간격 0이 끄라는 뜻이다 [P] */
    put(fd, req, airpods_request(req, 3, STREAM_MOTION, 0), 0);
    put(fd, req, airpods_request(req, 4, STREAM_STATE, 0), 0);
}

/* ---- 고개가 좌우로 돈 각도 --------------------------------------------------------------- */
#define GYRO_DEG_S 0.122             /* 회전 빠르기의 단위(도/초) [M] */
#define STILL_DEG_S 1.0              /* 이보다 느린 회전은 센서의 떨림으로 보고 버린다(가만히 있어도 시선이 흐르지 않게) */
#define GAP_S 0.25                   /* 표본 사이가 이보다 벌어졌으면(귀에서 뺐다 꽂음) 그 사이는 세지 않는다 */

double airpods_yaw(airpods_yaw_t *y, const airpods_sample_t *s)
{
    double gx = s->grav[0], gy = s->grav[1], gz = s->grav[2], g = sqrt(gx * gx + gy * gy + gz * gz);
    double dt = y->have ? (double)(int64_t)(s->t_ns - y->last_ns) / 1e9 : 0.0;
    y->last_ns = s->t_ns;
    y->have = 1;
    if (g < 300.0 || dt <= 0.0 || dt > GAP_S) return 0.0;        /* 켠 직후의 빈 표본은 중력이 0이다 */
    /* 회전 빠르기와 중력은 같은 좌표(에어팟의 몸)에 적혀 있다. 중력은 아래를 가리키므로, 아래 방향 축을 도는 성분이
     * 양수이면 위에서 볼 때 시계 방향, 곧 오른쪽으로 도는 것이다. 좌표가 어디를 보든 이 값은 같다 */
    double rate = (s->gyro[0] * gx + s->gyro[1] * gy + s->gyro[2] * gz) / g * GYRO_DEG_S;
    return fabs(rate) < STILL_DEG_S ? 0.0 : rate * dt;
}
