/* frame.h : 화면 메시지의 내용을 만들고 푸는 코드 (스테이지가 만들고 클라이언트가 푼다) */
#ifndef SSP_FRAME_H
#define SSP_FRAME_H

#include <stddef.h>
#include <stdint.h>
#include "protocol.h"
#include "brain/brain_host.h"
#include "ui/ui_internal.h"

#define FRAME_MAP_CELLS (BRAIN_MAP_W * BRAIN_MAP_H)

/* 받은 뇌 지도를 담아 두는 곳. map을 hud에 넘기면 된다. */
typedef struct {
    uint16_t density[FRAME_MAP_CELLS];
    float    heat[FRAME_MAP_CELLS];
    uint8_t  mark[FRAME_MAP_CELLS];
    brain_map_t map;
    int has_map;
} frame_map_t;

/* out에 내용을 만든다. 쓴 바이트 수를 돌려주고 실패하면 0이다. map이 있으면 *flags에 FRAME_HAS_MAP을 켠다.
 * head의 n_panes개만큼 panes를 담는다. */
size_t frame_pack(uint8_t *out, size_t cap, frame_head_t *head, const canvas_t *fly,
                  canvas_t *const panes[], const brain_map_t *map, uint8_t *flags);

/* 내용을 푼다. fly와 panes[0..PROTO_PANES-1]는 PANE_W x PANE_H 캔버스여야 한다. 0이면 성공. */
int frame_unpack(const uint8_t *in, size_t len, uint8_t flags, frame_head_t *head,
                 canvas_t *fly, canvas_t *const panes[], frame_map_t *fm);

/* 보스전의 뇌 표시(MSG_BOSS에서 상태 뒤에 붙는 부분)를 만들고 푼다. 서버가 만들고 클라이언트가 푼다.
 * 만들기: head의 stim, rate, takeoff는 채워서 넘긴다. 쓴 바이트 수를 돌려주고 실패하면 0이다.
 * 풀기: 0이면 성공. 뇌 지도가 없으면 fm->has_map이 0이다. */
size_t boss_brain_pack(uint8_t *out, size_t cap, boss_brain_msg_t *head, const brain_map_t *map, uint8_t *flags);
int boss_brain_unpack(const uint8_t *in, size_t len, uint8_t flags, boss_brain_msg_t *head, frame_map_t *fm);

#endif
