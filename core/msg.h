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

/* 그림(색 번호 n개)을 (개수, 색) 쌍으로 줄인다. 쓴 바이트 수를 돌려준다. cap이 모자라면 0. */
size_t rle_pack(const uint8_t *px, size_t n, uint8_t *out, size_t cap);
/* 줄인 그림을 n개의 색 번호로 되살린다. 0이면 성공, -1이면 길이가 맞지 않는다. */
int    rle_unpack(const uint8_t *in, size_t len, uint8_t *px, size_t n);

#endif
