/* 손맛 이펙트. 스테이지는 사건이 난 틱에 fx_*를 부르고, 실행기가 그림 위에 입힌다.
 * 지속 시간은 틱으로 센다(50ms 단위로 올림). */
#include "ui/ui_internal.h"
#include "core/log.h"

#include <string.h>

#define MAX_FX    48
#define MAX_PANES 9            /* 초파리 칸 1개 + 참가자 칸 8개 */

enum { FX_SHAKE = 1, FX_SHOCK, FX_TRAIL, FX_FLASH };

typedef struct {
    int type, pane;            /* pane: 0은 초파리 칸, 1부터는 참가자 */
    int left, total;           /* 남은 틱, 전체 틱 */
    int a, b;                  /* 흔들기 세기 또는 좌표 */
    color_t col;
} fx_t;

static fx_t list[MAX_FX];
static int calls;
static int in_draw;

static int ticks_of(int ms) { int t = (ms + 49) / 50; return t < 1 ? 1 : t; }

static void add(int type, int pane, int ticks, int a, int b, color_t col)
{
    calls++;
    if (in_draw)
        log_warn_once("fx-in-draw", "fx_*는 stage_draw에서 부르지 않습니다. stage_tick에서 부르세요");
    int idx = pane + 1;
    if (idx < 0 || idx >= MAX_PANES) {
        log_warn_once("fx-pane", "fx_*의 칸 번호가 틀렸습니다: %d", pane);
        return;
    }
    for (int i = 0; i < MAX_FX; i++) {
        if (list[i].left > 0) continue;
        list[i] = (fx_t){ type, idx, ticks, ticks, a, b, col };
        return;
    }
}

void fx_shake(int pane, int strength, int ms)  { add(FX_SHAKE, pane, ticks_of(ms), strength, 0, C_NONE); }
void fx_shockwave(int pane, int x, int y)      { add(FX_SHOCK, pane, 6, x, y, C_WHITE); }
void fx_trail(int pane, int x, int y)          { add(FX_TRAIL, pane, 4, x, y, C_GRAY2); }
void fx_flash(int pane, color_t col, int ms)   { add(FX_FLASH, pane, ticks_of(ms), 0, 0, col); }

void fx_reset(void)            { memset(list, 0, sizeof list); calls = 0; in_draw = 0; }
int  fx_call_count(void)       { return calls; }
void fx_set_in_draw(int on)    { in_draw = on; }

void fx_tick(void)
{
    for (int i = 0; i < MAX_FX; i++)
        if (list[i].left > 0) list[i].left--;
}

static void apply_shake(canvas_t *c, const fx_t *f)
{
    int s = f->a < 1 ? 1 : (f->a > 8 ? 8 : f->a);
    int dx = (f->left % 2) ? s : -s;
    for (int y = 0; y < c->h; y++) {
        color_t *row = &c->px[y * c->w];
        if (dx > 0) {
            memmove(row + dx, row, (size_t)(c->w - dx));
            memset(row, C_BG, (size_t)dx);
        } else {
            memmove(row, row - dx, (size_t)(c->w + dx));
            memset(row + c->w + dx, C_BG, (size_t)(-dx));
        }
    }
}

static void apply_shock(canvas_t *c, const fx_t *f)
{
    int r = (f->total - f->left + 1) * 3;      /* 틱마다 3픽셀씩 커지는 고리 */
    for (int dx = -r; dx <= r; dx++) {
        int dy = 0;
        while ((dy + 1) * (dy + 1) + dx * dx <= r * r) dy++;
        ui_px(c, f->a + dx, f->b + dy, f->col);
        ui_px(c, f->a + dx, f->b - dy, f->col);
        ui_px(c, f->a + dy, f->b + dx, f->col);
        ui_px(c, f->a - dy, f->b + dx, f->col);
    }
}

void fx_apply(int pane, canvas_t *c)
{
    if (!c) return;
    int idx = pane + 1;
    for (int i = 0; i < MAX_FX; i++) {
        const fx_t *f = &list[i];
        if (f->left <= 0 || f->pane != idx) continue;
        switch (f->type) {
        case FX_SHAKE: apply_shake(c, f); break;
        case FX_SHOCK: apply_shock(c, f); break;
        case FX_TRAIL: ui_rect(c, f->a, f->b, 2, 2, f->left > 2 ? C_GRAY3 : C_GRAY1); break;
        case FX_FLASH:                         /* 바둑판 모양으로 덮어 원래 그림이 비치게 한다 */
            for (int y = 0; y < c->h; y++)
                for (int x = (y + f->left) % 2; x < c->w; x += 2)
                    ui_px(c, x, y, f->col);
            break;
        }
    }
}
