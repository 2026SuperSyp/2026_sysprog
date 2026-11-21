/* 표시: 전체 화면(240x134)을 터미널 글자열로 바꾼다.
 *
 * 글자 한 칸이 위아래 픽셀 2개다. 위 픽셀은 글자색, 아래 픽셀은 바탕색으로 하고
 * 반칸 블록 문자(▀)를 찍는다. 지난번과 달라진 칸만 내보낸다. */
#include "ui/ui_internal.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CELL_W SCREEN_W
#define CELL_H (SCREEN_H / 2)

struct present_state {
    color_t top[CELL_H][CELL_W];
    color_t bot[CELL_H][CELL_W];
    int valid;                 /* 0이면 다음 번에 전부 다시 그린다 */
    int cols, rows;            /* 지난번 터미널 크기 */
};

present_state_t *present_new(void)
{
    return calloc(1, sizeof(present_state_t));
}

void present_free(present_state_t *st)        { free(st); }
void present_invalidate(present_state_t *st)  { if (st) st->valid = 0; }

static int put(char *out, size_t cap, size_t *len, const char *s, size_t n)
{
    if (*len + n > cap) return 0;
    memcpy(out + *len, s, n);
    *len += n;
    return 1;
}

size_t present_encode(present_state_t *st, const canvas_t *full, int cols, int rows,
                      char *out, size_t cap)
{
    if (!st || !full || full->w != SCREEN_W || full->h != SCREEN_H) return 0;
    if (cols < CELL_W || rows < CELL_H) { st->valid = 0; return 0; }
    if (cols != st->cols || rows != st->rows) { st->valid = 0; st->cols = cols; st->rows = rows; }

    int offx = (cols - CELL_W) / 2, offy = (rows - CELL_H) / 2;
    size_t len = 0;
    char tmp[64];
    int n;

    if (!st->valid && !put(out, cap, &len, "\033[0m\033[2J", 8)) return 0;

    int last_r = -1, last_c = -1;      /* 방금 찍은 칸. 바로 옆이면 커서를 옮기지 않는다 */
    int fg = -1, bg = -1;
    for (int r = 0; r < CELL_H; r++) {
        for (int c = 0; c < CELL_W; c++) {
            color_t t = full->px[(2 * r) * SCREEN_W + c];
            color_t b = full->px[(2 * r + 1) * SCREEN_W + c];
            if (st->valid && st->top[r][c] == t && st->bot[r][c] == b) continue;
            st->top[r][c] = t;
            st->bot[r][c] = b;

            if (!(r == last_r && c == last_c + 1)) {
                n = snprintf(tmp, sizeof tmp, "\033[%d;%dH", offy + r + 1, offx + c + 1);
                if (!put(out, cap, &len, tmp, (size_t)n)) return len;
            }
            if (t != fg) {
                uint8_t rgb[3];
                ui_palette_rgb(t, rgb);
                n = snprintf(tmp, sizeof tmp, "\033[38;2;%d;%d;%dm", rgb[0], rgb[1], rgb[2]);
                if (!put(out, cap, &len, tmp, (size_t)n)) return len;
                fg = t;
            }
            if (b != bg) {
                uint8_t rgb[3];
                ui_palette_rgb(b, rgb);
                n = snprintf(tmp, sizeof tmp, "\033[48;2;%d;%d;%dm", rgb[0], rgb[1], rgb[2]);
                if (!put(out, cap, &len, tmp, (size_t)n)) return len;
                bg = b;
            }
            if (!put(out, cap, &len, "\xE2\x96\x80", 3)) return len;
            last_r = r;
            last_c = c;
        }
    }
    st->valid = 1;
    return len;
}

int present_cell_to_screen(int cols, int rows, int cell_x, int cell_y, int *sx, int *sy)
{
    int x = cell_x - (cols - CELL_W) / 2;
    int y = cell_y - (rows - CELL_H) / 2;
    if (x < 0 || y < 0 || x >= CELL_W || y >= CELL_H) return 0;
    *sx = x;
    *sy = y * 2;
    return 1;
}
