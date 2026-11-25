/* 에어팟 프로의 머리 움직임 센서를 블루투스 소켓으로 직접 읽는다.
 *
 * 에어팟은 소리(A2DP)와 따로, 설정과 센서를 주고받는 통로를 하나 더 연다. 블루투스의 L2CAP 계층 위에 바로 얹힌
 * 통로(PSM 0x1001)이고, 애플이 규격을 공개하지 않았다. 여기 쓴 패킷의 바이트와 자리는 그 통로를 관찰해 공개한
 * 분석 문서에서 가져온 사실이고(출처는 airpods.c의 맨 위), 코드는 그 사실만 보고 직접 썼다.
 *
 * 블루투스 라이브러리(libbluetooth) 없이 커널의 소켓 인터페이스만 쓴다: socket(AF_BLUETOOTH, SOCK_SEQPACKET,
 * BTPROTO_L2CAP), setsockopt로 암호화 수준과 받는 크기 한도, connect. 패킷 하나가 recv 한 번이다.
 *
 * 쓰기 전에: 에어팟이 이 컴퓨터의 리눅스에 짝지어져 있고(bluetoothctl pair) 접속돼 있어야 한다(connect). 귀에 꽂고
 * 있어야 센서가 값을 보낸다. 에어팟 프로 2와 3에서 확인된 방식이다(에어팟 3세대는 다른 방식이라 안 된다). */
#ifndef UI_AIRPODS_H
#define UI_AIRPODS_H

#include <stddef.h>
#include <stdint.h>

#define AIRPODS_MTU     2582         /* 받는 패킷의 크기 한도. 리눅스의 기본값(672)이면 센서가 안 보이는 기종이 있다 */
#define AIRPODS_REQ_MAX 32           /* 요청 패킷 하나의 최대 길이 */

/* 센서가 보낸 표본 하나. 값은 에어팟이 보낸 그대로다(부호 있는 16비트) */
typedef struct {
    uint64_t t_ns;                   /* 에어팟의 시계(나노초) */
    int16_t att[3];                  /* 자세: 단위 쿼터니언의 벡터 부분(x, y, z). 32768이 1 */
    int16_t gyro[3];                 /* 회전 빠르기. 1이 약 0.122도/초 */
    int16_t accel[3];                /* 중력을 뺀 가속도. 약 1000이 1g */
    int16_t grav[3];                 /* 중력의 방향(아래쪽). 약 1000이 1g */
} airpods_sample_t;

/* 받은 패킷이 움직임 표본이면 out에 풀어 넣고 1, 아니면(다른 알림, 상태 메시지) 0 */
int airpods_parse(const uint8_t *pkt, size_t len, airpods_sample_t *out);

/* 센서 흐름을 켜고 끄는 요청 패킷을 buf(AIRPODS_REQ_MAX 이상)에 만든다. seq는 요청의 일련번호, stream은 흐름 번호
 * (14, 16), interval_us는 표본 사이의 간격(마이크로초, 0이면 끈다). 패킷의 길이를 돌려준다 */
size_t airpods_request(uint8_t *buf, uint32_t seq, uint8_t stream, uint32_t interval_us);

/* "00:11:22:33:44:55" 꼴의 주소로 통로를 연다. 열린 소켓(논블로킹)이나 -1. 실패하면 err에 까닭을 적는다 */
int airpods_open(const char *addr, char *err, size_t cap);

/* 열린 통로에 첫 인사와 알림 요청을 보내고 움직임 흐름을 켠다(0.5초쯤 걸린다). 0이면 성공.
 * fd는 airpods_open이 준 것이 아니어도 된다(검사에서는 socketpair를 쓴다) */
int airpods_start(int fd, uint32_t interval_us);

/* 움직임 흐름을 끈다. 닫는 것은 부른 쪽이 한다 */
void airpods_stop(int fd);

/* 표본을 이어 받아 고개가 좌우로 돈 각도를 구한다. 처음에는 0으로 채워 둔다 */
typedef struct {
    uint64_t last_ns;
    int have;
} airpods_yaw_t;

/* 앞 표본부터 이 표본까지 고개가 오른쪽으로 돈 각도(도, 왼쪽이면 음수). 회전 빠르기에서 중력 방향 축의 성분만
 * 꺼내 시간으로 곱한다. 그래서 고개를 숙이거나 기울인 채 돌려도 좌우로 돈 양만 나온다 */
double airpods_yaw(airpods_yaw_t *y, const airpods_sample_t *s);

#endif
