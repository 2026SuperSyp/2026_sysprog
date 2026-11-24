/* 표시: 전체 화면을 터미널 글자열로 바꾼다.
 *
 * 글자 한 칸이 위아래 픽셀 2개다. 위 픽셀은 글자색, 아래 픽셀은 바탕색으로 하고
 * 반칸 블록 문자(▀)를 찍는다. 지난번과 달라진 칸만 내보낸다.
 *
 * 화면은 보통 240x134이고 터미널 240칸 67줄에 그린다. 배율 N(1~PRESENT_MAX_SCALE)으로 키운
 * 화면(240N x 134N)도 받는다. 그때는 터미널이 240N칸 67N줄 이상이어야 한다. */
#include "ui/ui_internal.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

struct present_state {
    color_t *top, *bot;        /* 칸마다 지난번에 내보낸 위, 아래 픽셀의 색. cw * ch개 */
    int cw, ch;                /* 지난번 화면의 칸 수 */
    int valid;                 /* 0이면 다음 번에 전부 다시 그린다 */
    int cols, rows;            /* 지난번 터미널 크기 */
};

present_state_t *present_new(void)
{
    return calloc(1, sizeof(present_state_t));
}

void present_free(present_state_t *st)
{
    if (!st) return;
    free(st->top);
    free(st->bot);
    free(st);
}

void present_invalidate(present_state_t *st)  { if (st) st->valid = 0; }

/* 화면의 배율. 240x134의 정수배가 아니면 0 */
static int scale_of(const canvas_t *full)
{
    int s = full->w / SCREEN_W;
    if (s < 1 || s > PRESENT_MAX_SCALE || full->w != SCREEN_W * s || full->h != SCREEN_H * s) return 0;
    return s;
}

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
    if (!st || !full || !scale_of(full)) return 0;
    int cw = full->w, ch = full->h / 2;
    if (cols < cw || rows < ch) { st->valid = 0; return 0; }
    if (cw != st->cw || ch != st->ch) {              /* 화면의 크기가 바뀌었다(배율이 다른 화면이 왔다) */
        color_t *t = realloc(st->top, (size_t)cw * (size_t)ch), *b = realloc(st->bot, (size_t)cw * (size_t)ch);
        if (t) st->top = t;
        if (b) st->bot = b;
        if (!t || !b) { st->cw = st->ch = 0; st->valid = 0; return 0; }
        st->cw = cw;
        st->ch = ch;
        st->valid = 0;
    }
    if (cols != st->cols || rows != st->rows) { st->valid = 0; st->cols = cols; st->rows = rows; }

    int offx = (cols - cw) / 2, offy = (rows - ch) / 2;
    size_t len = 0;
    char tmp[64];
    int n;

    if (!st->valid && !put(out, cap, &len, "\033[0m\033[2J", 8)) return 0;

    int last_r = -1, last_c = -1;      /* 방금 찍은 칸. 바로 옆이면 커서를 옮기지 않는다 */
    int fg = -1, bg = -1;
    for (int r = 0; r < ch; r++) {
        const color_t *row_t = &full->px[(2 * r) * cw], *row_b = &full->px[(2 * r + 1) * cw];
        color_t *old_t = &st->top[r * cw], *old_b = &st->bot[r * cw];
        for (int c = 0; c < cw; c++) {
            color_t t = row_t[c], b = row_b[c];
            if (st->valid && old_t[c] == t && old_b[c] == b) continue;
            old_t[c] = t;
            old_b[c] = b;

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

int present_cell_to_screen_scaled(int cols, int rows, int scale, int cell_x, int cell_y, int *sx, int *sy)
{
    if (scale < 1) scale = 1;
    int cw = SCREEN_W * scale, ch = SCREEN_H * scale / 2;
    int x = cell_x - (cols - cw) / 2;
    int y = cell_y - (rows - ch) / 2;
    if (x < 0 || y < 0 || x >= cw || y >= ch) return 0;
    *sx = x / scale;
    *sy = y * 2 / scale;
    return 1;
}

int present_cell_to_screen(int cols, int rows, int cell_x, int cell_y, int *sx, int *sy)
{
    return present_cell_to_screen_scaled(cols, rows, 1, cell_x, cell_y, sx, sy);
}
