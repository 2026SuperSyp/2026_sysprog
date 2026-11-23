/* msg.h : 메시지를 보내고 받는 공용 코드 (소켓과 파이프가 같이 쓴다) */
#ifndef SSP_MSG_H
#define SSP_MSG_H

#include <stddef.h>
#include <stdint.h>
#include "protocol.h"

/* 메시지 하나를 다 보낸다. 0이면 성공, -1이면 실패(상대가 끊겼다). */
int msg_send(int fd, uint8_t type, uint8_t flags, uint16_t slot, const void *body, uint32_t len);

/* 메시지 하나를 다 받는다. 1: 받았다, 0: 상대가 닫았다, -1: 오류이거나 len이 cap을 넘는다. */
int msg_recv(int fd, msg_hdr_t *h, void *body, uint32_t cap);

/* 막히지 않게 연 fd(O_NONBLOCK)에서 쓰는 것. 상대가 멈춰 있어도 부른 쪽은 기다리지 않는다.
 *
 * msg_recv_some: 온 만큼만 읽어 모은다. 어디까지 받았는지는 part에 남는다(처음에 0으로 채워 둔다).
 *   1: 메시지 하나가 다 왔다(머리는 part->h, 내용은 body), 0: 아직 덜 왔다,
 *   -1: 상대가 닫았거나 오류이거나 len이 cap을 넘는다.
 * msg_send_once: 머리와 내용을 write 한 번으로 보낸다(내용은 MSG_ONCE_MAX 바이트까지).
 *   파이프는 이 크기를 나눠 쓰지 않으므로 메시지가 중간에 끊기지 않는다.
 *   1: 보냈다, 0: 지금은 자리가 없다(아무것도 쓰지 않았다), -1: 실패. */
#define MSG_ONCE_MAX 256
typedef struct { msg_hdr_t h; uint32_t got; } msg_part_t;
int msg_recv_some(int fd, msg_part_t *part, void *body, uint32_t cap);
int msg_send_once(int fd, uint8_t type, uint8_t flags, uint16_t slot, const void *body, uint32_t len);

/* 그림(색 번호 n개)을 (개수, 색) 쌍으로 줄인다. 쓴 바이트 수를 돌려준다. cap이 모자라면 0. */
size_t rle_pack(const uint8_t *px, size_t n, uint8_t *out, size_t cap);
/* 줄인 그림을 n개의 색 번호로 되살린다. 0이면 성공, -1이면 길이가 맞지 않는다. */
int    rle_unpack(const uint8_t *in, size_t len, uint8_t *px, size_t n);

#endif
