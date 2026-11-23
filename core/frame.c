/* 화면 메시지의 내용을 만들고 푼다. 설명은 frame.h와 protocol.h에 있다. */
#include "core/frame.h"
#include "core/msg.h"

#include <string.h>

/* 그림 하나를 "4바이트 길이 + 줄인 바이트"로 붙인다. 쓴 바이트 수, 실패하면 0. */
static size_t put_blob(uint8_t *out, size_t cap, const uint8_t *px, size_t n)
{
    if (cap < 4) return 0;
    size_t len = rle_pack(px, n, out + 4, cap - 4);
    if (len == 0) return 0;
    uint32_t l32 = (uint32_t)len;
    memcpy(out, &l32, 4);
    return len + 4;
}

/* 그림 하나를 꺼낸다. 읽은 바이트 수, 실패하면 0. */
static size_t get_blob(const uint8_t *in, size_t len, uint8_t *px, size_t n)
{
    uint32_t l32;
    if (len < 4) return 0;
    memcpy(&l32, in, 4);
    if (l32 > len - 4 || rle_unpack(in + 4, l32, px, n) != 0) return 0;
    return (size_t)l32 + 4;
}

size_t frame_pack(uint8_t *out, size_t cap, frame_head_t *head, const canvas_t *fly,
                  canvas_t *const panes[], const brain_map_t *map, uint8_t *flags)
{
    const size_t pane_px = (size_t)PANE_W * PANE_H;
    size_t o = sizeof *head, w;
    *flags = 0;
    if (cap < o || head->n_panes > PROTO_PANES) return 0;

    if ((w = put_blob(out + o, cap - o, fly->px, pane_px)) == 0) return 0;
    o += w;
    for (int p = 0; p < head->n_panes; p++) {
        if ((w = put_blob(out + o, cap - o, panes[p]->px, pane_px)) == 0) return 0;
        o += w;
    }

    head->map_w = head->map_h = 0;
    if (map && map->w * map->h == FRAME_MAP_CELLS) {
        /* 칸마다 뉴런 유무, 발화(4배 해서 0~255), 표시를 차례로 담는다 */
        uint8_t cells[FRAME_MAP_CELLS * 3];
        for (int k = 0; k < FRAME_MAP_CELLS; k++) {
            float h = map->heat[k] * 4.0f;
            cells[k] = map->density[k] > 255 ? 255 : (uint8_t)map->density[k];
            cells[FRAME_MAP_CELLS + k] = h > 255.0f ? 255 : (uint8_t)h;
            cells[2 * FRAME_MAP_CELLS + k] = map->mark[k];
        }
        if ((w = put_blob(out + o, cap - o, cells, sizeof cells)) == 0) return 0;
        o += w;
        head->map_w = (uint8_t)map->w;
        head->map_h = (uint8_t)map->h;
        *flags = FRAME_HAS_MAP;
    }
    memcpy(out, head, sizeof *head);
    return o;
}

int frame_unpack(const uint8_t *in, size_t len, uint8_t flags, frame_head_t *head,
                 canvas_t *fly, canvas_t *const panes[], frame_map_t *fm)
{
    const size_t pane_px = (size_t)PANE_W * PANE_H;
    size_t o = sizeof *head, r;
    if (len < o) return -1;
    memcpy(head, in, sizeof *head);
    if (head->n_panes > PROTO_PANES) return -1;

    if ((r = get_blob(in + o, len - o, fly->px, pane_px)) == 0) return -1;
    o += r;
    for (int p = 0; p < head->n_panes; p++) {
        if ((r = get_blob(in + o, len - o, panes[p]->px, pane_px)) == 0) return -1;
        o += r;
    }

    fm->has_map = 0;
    if ((flags & FRAME_HAS_MAP) && head->map_w * head->map_h == FRAME_MAP_CELLS) {
        uint8_t cells[FRAME_MAP_CELLS * 3];
        if (get_blob(in + o, len - o, cells, sizeof cells) == 0) return -1;
        for (int k = 0; k < FRAME_MAP_CELLS; k++) {
            fm->density[k] = cells[k];
            fm->heat[k] = (float)cells[FRAME_MAP_CELLS + k] / 4.0f;
            fm->mark[k] = cells[2 * FRAME_MAP_CELLS + k];
        }
        fm->map = (brain_map_t){ head->map_w, head->map_h, fm->density, fm->heat, fm->mark };
        fm->has_map = 1;
    }
    return 0;
}
