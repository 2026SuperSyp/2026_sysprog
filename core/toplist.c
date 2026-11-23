/* 역대 순위를 메시지에 담고 화면에 그린다. */
#include "core/toplist.h"

#include <stdio.h>
#include <string.h>

_Static_assert(RANKING_NAME_LEN == PROTO_NAME_LEN, "이름 길이가 같아야 그대로 옮길 수 있다");

int toplist_load(top_msg_t *m, const char *path, const rank_rec_t *just, int n_just)
{
    rank_rec_t top[PROTO_TOP_N];
    int n = ranking_load_top(path, top, PROTO_TOP_N);
    if (n < 0) return -1;
    memset(m, 0, sizeof *m);
    for (int i = 0; i < n; i++) {
        m->score[i] = top[i].score;
        memcpy(m->name[i], top[i].name, PROTO_NAME_LEN);
        for (int k = 0; just && k < n_just; k++)
            if (top[i].when == just[k].when && top[i].score == just[k].score &&
                strncmp(top[i].name, just[k].name, RANKING_NAME_LEN - 1) == 0) m->fresh[i] = 1;
    }
    m->n = (uint8_t)n;
    return 0;
}

/* ---- 그리기 ------------------------------------------------------------------ */
#define HEAD_H     11                /* 가름선 1, 빈 줄 1, 제목 8, 빈 줄 1 */
#define ROW_LOOSE  (UI_FONT_H + 1)   /* 줄 간격. 자리가 모자라면 ROW_TIGHT로 줄인다 */
#define ROW_TIGHT  UI_FONT_H
#define ONE_X      20                /* 한 단일 때의 왼쪽 끝. 기본 랭킹 화면의 줄과 맞춘다 */
#define ONE_NAME_W (15 * UI_FONT_W)
#define TWO_X      3                 /* 두 단일 때의 왼쪽 끝 */
#define TWO_STEP   120               /* 단과 단 사이 */
#define TWO_NAME_W (10 * UI_FONT_W)

/* 이름을 가로 max_w 픽셀 안에 들어오게 뒤에서부터 글자 단위로 줄인다. */
static void fit_name(char out[PROTO_NAME_LEN], const char *name, int max_w)
{
    size_t len = strnlen(name, PROTO_NAME_LEN - 1);
    memcpy(out, name, len);
    out[len] = '\0';
    while (len > 0 && ui_text_w(out) > max_w) {
        len--;
        while (len > 0 && ((unsigned char)out[len] & 0xC0) == 0x80) len--;   /* UTF-8 글자의 뒤쪽 바이트 */
        out[len] = '\0';
    }
}

void toplist_draw(canvas_t *full, const top_msg_t *m, int y0, int y1, int mark_fresh)
{
    static const char title[] = "ALL-TIME TOP 10", none[] = "NO RECORDS YET";
    char buf[PROTO_NAME_LEN];
    if (!full || !m) return;
    int n = m->n > PROTO_TOP_N ? PROTO_TOP_N : m->n;
    int room = y1 - y0 - HEAD_H, row_h = ROW_LOOSE, cols = 1;
    if (room < ROW_TIGHT) return;                    /* 제목과 한 줄도 들어가지 않는 띠 */
    /* 넉넉한 줄 간격의 한 단, 좁은 간격의 한 단, 두 단의 순서로 들어가는 것을 고른다 */
    if (n > room / ROW_LOOSE) {
        if (n <= room / ROW_TIGHT) row_h = ROW_TIGHT;
        else {
            cols = 2;
            if (n > 2 * (room / ROW_LOOSE)) row_h = ROW_TIGHT;
        }
    }
    int per_col = room / row_h;
    if (n > cols * per_col) n = cols * per_col;
    int name_w = cols == 1 ? ONE_NAME_W : TWO_NAME_W;

    ui_line(full, ONE_X, y0, SCREEN_W - 1 - ONE_X, y0, C_GRAY1);
    ui_text(full, (SCREEN_W - ui_text_w(title)) / 2, y0 + 2, title, C_YELLOW);
    if (n == 0) {
        ui_text(full, (SCREEN_W - ui_text_w(none)) / 2, y0 + HEAD_H, none, C_GRAY2);
        return;
    }
    for (int i = 0; i < n; i++) {
        int x = cols == 1 ? ONE_X : TWO_X + (i / per_col) * TWO_STEP;
        int y = y0 + HEAD_H + (i % per_col) * row_h;
        int fresh = mark_fresh && m->fresh[i];
        int score = m->score[i] > 99999 ? 99999 : (m->score[i] < -9999 ? -9999 : m->score[i]);
        color_t col = fresh ? C_WHITE : C_GRAY3;

        snprintf(buf, sizeof buf, "%2d", i + 1);
        ui_text(full, x, y, buf, col);
        fit_name(buf, m->name[i], name_w);
        ui_text(full, x + 3 * UI_FONT_W, y, buf, col);
        snprintf(buf, sizeof buf, "%5d", score);
        ui_text(full, x + 4 * UI_FONT_W + name_w, y, buf, col);
        if (fresh && cols == 1) ui_text(full, x + 10 * UI_FONT_W + name_w, y, "NEW", C_GOOD);
    }
}
