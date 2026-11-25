/* 에어팟 통로: 실물 없이 볼 수 있는 것만 본다.
 *   요청 패킷이 공개 문서에 실린 실제 패킷과 바이트까지 같은가, 켤 때 다섯 패킷을 순서대로 보내는가,
 *   실제로 잡힌 표본 패킷에서 값을 맞게 꺼내는가, 표본이 아닌 패킷을 거르는가,
 *   고개가 돈 각도: 오른쪽이 양수인가, 고개를 기울여도 같은가, 가만히 있으면 0인가, 끊겼다 이어지면 건너뛰는가.
 * 통로 대신 socketpair(패킷 단위)를 쓴다. 에어팟 쪽 끝에서 읽어 보면 보낸 패킷이 하나씩 나온다 */
#include "ui/airpods.h"
#include "t.h"

#include <math.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

/* 문서에 실린 패킷(출처는 ui/airpods.c의 맨 위) */
static const uint8_t DOC_START14[] = { 0x04, 0x00, 0x04, 0x00, 0x17, 0x00, 0x00, 0x00, 0x10, 0x00, 0x10, 0x00, 0x08, 0xA1,
                                       0x02, 0x42, 0x0B, 0x08, 0x0E, 0x10, 0x02, 0x1A, 0x05, 0x01, 0x40, 0x9C, 0x00, 0x00 };
static const uint8_t DOC_START16[] = { 0x04, 0x00, 0x04, 0x00, 0x17, 0x00, 0x00, 0x00, 0x10, 0x00, 0x0F, 0x00, 0x08, 0x73,
                                       0x42, 0x0B, 0x08, 0x10, 0x10, 0x02, 0x1A, 0x05, 0x01, 0x40, 0x9C, 0x00, 0x00 };
static const uint8_t DOC_HELLO[]   = { 0x00, 0x00, 0x04, 0x00, 0x01, 0x00, 0x02, 0x00, 0, 0, 0, 0, 0, 0, 0, 0 };

static void test_request(void)
{
    uint8_t buf[AIRPODS_REQ_MAX];
    size_t n = airpods_request(buf, 289, 14, 40000);         /* 일련번호가 두 바이트로 적히는 경우 */
    T_EQ_INT(n, sizeof DOC_START14);
    T_CHECK(memcmp(buf, DOC_START14, sizeof DOC_START14) == 0);
    n = airpods_request(buf, 0x73, 16, 40000);
    T_EQ_INT(n, sizeof DOC_START16);
    T_CHECK(memcmp(buf, DOC_START16, sizeof DOC_START16) == 0);
    n = airpods_request(buf, 5, 16, 0);                      /* 끄기: 간격 0 */
    T_EQ_INT(n, 27);
    T_CHECK(buf[23] == 0 && buf[24] == 0 && buf[25] == 0 && buf[26] == 0);
}

static void test_start_order(void)
{
    int sv[2];
    uint8_t got[64];
    T_CHECK(socketpair(AF_UNIX, SOCK_SEQPACKET, 0, sv) == 0);
    T_EQ_INT(airpods_start(sv[0], 20000), 0);
    T_EQ_INT(recv(sv[1], got, sizeof got, 0), 16);           /* 첫 인사 */
    T_CHECK(memcmp(got, DOC_HELLO, 16) == 0);
    T_EQ_INT(recv(sv[1], got, sizeof got, 0), 14);           /* 기능 */
    T_CHECK(got[4] == 0x4d);
    T_EQ_INT(recv(sv[1], got, sizeof got, 0), 10);           /* 알림 */
    T_CHECK(got[4] == 0x0f);
    T_EQ_INT(recv(sv[1], got, sizeof got, 0), 27);           /* 흐름 14 먼저 */
    T_CHECK(got[4] == 0x17 && got[17] == 14 && got[23] == 0x20 && got[24] == 0x4e);      /* 20000 = 0x4e20 */
    T_EQ_INT(recv(sv[1], got, sizeof got, 0), 27);           /* 그다음 흐름 16 */
    T_CHECK(got[17] == 16 && got[13] != 0);
    airpods_stop(sv[0]);
    T_EQ_INT(recv(sv[1], got, sizeof got, 0), 27);
    T_CHECK(got[17] == 16 && got[23] == 0 && got[24] == 0);
    close(sv[0]);
    close(sv[1]);
}

static void put16(uint8_t *p, int v) { p[0] = (uint8_t)v; p[1] = (uint8_t)((unsigned)v >> 8); }

/* 표본 패킷을 만든다. wide이면 일련번호가 두 바이트(81바이트 패킷) */
static size_t make_sample(uint8_t *p, int wide, uint64_t t_ns, const int *att, const int *gyro, const int *grav)
{
    static const uint8_t head[] = { 0x04, 0x00, 0x04, 0x00, 0x17, 0x00, 0x00, 0x00, 0x10, 0x00 };
    size_t n = sizeof head;
    memcpy(p, head, n);
    n += 2;
    p[n++] = 0x08; p[n++] = wide ? 0x8f : 0x0f;
    if (wide) p[n++] = 0x01;
    p[n++] = 0x10; p[n++] = 0x01; p[n++] = 0x3a; p[n++] = 0x3e; p[n++] = 0x08; p[n++] = 0x0e; p[n++] = 0x1a; p[n++] = 0x3a;
    uint8_t *b = p + n;
    memset(b, 0, 58);
    b[0] = 0x01;
    for (int i = 0; i < 8; i++) b[1 + i] = (uint8_t)(t_ns >> (8 * i));
    b[9] = 0x03;
    for (int i = 0; i < 3; i++) { put16(b + 20 + 2 * i, att[i]); put16(b + 26 + 2 * i, gyro[i]); put16(b + 44 + 2 * i, grav[i]); }
    n += 58;
    p[10] = (uint8_t)(n - 12);
    p[11] = 0;
    return n;
}

static void test_parse(void)
{
    /* 공개된 실제 표본(LibrePods 133번 글)의 값: 자세, 회전 빠르기, 중력 */
    static const int att[3] = { -28046, -3492, -7531 }, gyro[3] = { 20, 294, -161 }, grav[3] = { -478, 751, 521 };
    uint8_t p[96];
    airpods_sample_t s;
    size_t n = make_sample(p, 0, 123456789012ULL, att, gyro, grav);
    T_EQ_INT(n, 80);
    T_EQ_INT(p[10], 0x44);                                   /* 그 글의 패킷도 길이 칸이 44다 */
    T_CHECK(airpods_parse(p, n, &s) == 1);
    T_CHECK(s.t_ns == 123456789012ULL);
    for (int i = 0; i < 3; i++) { T_EQ_INT(s.att[i], att[i]); T_EQ_INT(s.gyro[i], gyro[i]); T_EQ_INT(s.grav[i], grav[i]); }

    n = make_sample(p, 1, 5, att, gyro, grav);               /* 일련번호가 길어져 한 바이트 밀린 패킷 */
    T_EQ_INT(n, 81);
    T_CHECK(airpods_parse(p, n, &s) == 1);
    T_EQ_INT(s.gyro[1], 294);

    T_CHECK(airpods_parse(p, 45, &s) == 0);                  /* 짧은 상태 메시지 */
    p[n - 58] = 0x02;                                        /* 표본이 아닌 묶음 */
    T_CHECK(airpods_parse(p, n, &s) == 0);
    p[n - 58] = 0x01;
    p[10]++;                                                 /* 적힌 길이가 실제와 다름 */
    T_CHECK(airpods_parse(p, n, &s) == 0);
    p[10]--;
    p[4] = 0x06;                                             /* 다른 알림(귀에 꽂았는지 등) */
    T_CHECK(airpods_parse(p, n, &s) == 0);
}

/* 초당 deg도로 sec초 동안 도는 표본을 0.02초 간격으로 넣고 돈 각도의 합을 돌려준다. (gx, gy, gz)는 중력의 방향이다 */
static double spin(airpods_yaw_t *y, uint64_t *t, double deg, double sec, double gx, double gy, double gz)
{
    double sum = 0.0, g = sqrt(gx * gx + gy * gy + gz * gz), raw = deg / 0.122;
    for (int k = 0; k < (int)(sec / 0.02 + 0.5); k++) {
        airpods_sample_t s = { .t_ns = (*t += 20000000ULL) };
        /* 오른쪽으로 도는 것은 아래 방향 축을 도는 것이다 */
        s.gyro[0] = (int16_t)lround(raw * gx / g); s.gyro[1] = (int16_t)lround(raw * gy / g); s.gyro[2] = (int16_t)lround(raw * gz / g);
        s.grav[0] = (int16_t)gx; s.grav[1] = (int16_t)gy; s.grav[2] = (int16_t)gz;
        sum += airpods_yaw(y, &s);
    }
    return sum;
}

static void test_yaw(void)
{
    airpods_yaw_t y = { 0 };
    uint64_t t = 1000;
    T_CHECK(spin(&y, &t, 0.0, 0.02, 0, 0, -1000) == 0.0);                /* 첫 표본은 앞의 것이 없어 0 */
    double right = spin(&y, &t, 90.0, 1.0, 0, 0, -1000);
    T_CHECK(fabs(right - 90.0) < 1.0);                                   /* 오른쪽으로 90도/초로 1초 */
    double left = spin(&y, &t, -90.0, 1.0, 0, 0, -1000);
    T_CHECK(fabs(left + 90.0) < 1.0);
    double tilted = spin(&y, &t, 90.0, 1.0, 0, 707, -707);               /* 고개를 45도 기울인 채 */
    T_CHECK(fabs(tilted - 90.0) < 1.5);
    T_CHECK(spin(&y, &t, 0.5, 1.0, 0, 0, -1000) == 0.0);                 /* 떨림 */

    /* 고개를 끄덕이면(회전축이 수평) 좌우로는 돌지 않는다 */
    double nod = 0.0;
    for (int k = 0; k < 50; k++) {
        airpods_sample_t s = { .t_ns = (t += 20000000ULL), .gyro = { 800, 0, 0 }, .grav = { 0, 0, -1000 } };
        nod += airpods_yaw(&y, &s);
    }
    T_CHECK(nod == 0.0);

    t += 2000000000ULL;                                                  /* 2초 끊김: 그 사이를 세지 않는다 */
    T_CHECK(fabs(spin(&y, &t, 90.0, 0.02, 0, 0, -1000)) < 0.01);
    airpods_sample_t empty = { .t_ns = (t += 20000000ULL), .gyro = { 0, 0, 800 } };      /* 켠 직후의 빈 표본 */
    T_CHECK(airpods_yaw(&y, &empty) == 0.0);
}

static void test_open_errors(void)
{
    char err[160] = "";
    T_CHECK(airpods_open("에어팟", err, sizeof err) == -1 && err[0]);
    err[0] = 0;
    T_CHECK(airpods_open("00:11:22:33:44", err, sizeof err) == -1 && err[0]);
}

int main(void)
{
    test_request();
    test_start_order();
    test_parse();
    test_yaw();
    test_open_errors();
    T_DONE();
}
