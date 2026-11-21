/* ui_internal.h : 화면 라이브러리의 안쪽 약속
 *
 * 코어만 쓴다. 스테이지와 화면 코드는 include/ui.h만 본다. */
#ifndef SSP_UI_INTERNAL_H
#define SSP_UI_INTERNAL_H

#include <stddef.h>
#include <stdint.h>
#include "ui.h"

/* ---- 캔버스 (canvas.c) ---- */
struct canvas {
    int w, h;
    color_t *px;                 /* w * h개. px[y * w + x] */
};
canvas_t *canvas_new(int w, int h);
void canvas_free(canvas_t *c);
void canvas_blit(canvas_t *dst, int dx, int dy, const canvas_t *src);        /* C_NONE인 픽셀은 건너뛴다 */
void canvas_blit_half(canvas_t *dst, int dx, int dy, const canvas_t *src);   /* 가로세로 절반으로 줄여서 놓는다 */

/* ---- 색 (palette.c) ---- */
void ui_palette_rgb(color_t c, uint8_t rgb[3]);

/* ---- 그림 (sprite.c) ---- */
void ui_set_stage_dir(const char *dir);      /* 예: "stages/example". NULL이면 스테이지 그림을 찾지 않는다 */

/* ---- 이펙트 (fx.c) ---- */
void fx_reset(void);
void fx_apply(int pane, canvas_t *c);        /* 그 칸에 걸린 이펙트를 그림 위에 입힌다 */
void fx_tick(void);                          /* 남은 시간을 한 틱 줄인다 */
int  fx_call_count(void);                    /* 지금까지 fx_*가 불린 횟수 */
void fx_set_in_draw(int on);                 /* 켜져 있는 동안 fx_*가 불리면 경고를 남긴다 */

/* ---- 합성 (compose.c) ---- */
typedef struct {
    const char *title;           /* 위 띠: 게임 이름 */
    int ticks_left;              /* 위 띠: 남은 틱 */
    int round;                   /* 위 띠: 라운드 (1부터) */
    int my_score;                /* 아래 띠 */
    int fly_score;               /* 아래 띠 */
    int lives;                   /* 아래 띠: 남은 기회 */
} hud_t;
/* 전체 화면에 위 띠, 초파리 칸, 내 칸, 다른 두 명의 칸(절반 크기), 아래 띠를 놓는다.
 * panes[i]는 참가자 i의 칸이고 me는 가운데에 크게 놓을 참가자다. */
void compose_stage_frame(canvas_t *full, const canvas_t *fly, canvas_t *const panes[],
                         int n_players, int me, const hud_t *hud);
/* 전체 화면의 좌표가 me의 칸 안이면 1을 돌려주고 칸 기준 좌표를 채운다. */
int  compose_my_pane_xy(int screen_x, int screen_y, int *pane_x, int *pane_y);

/* ---- 표시 (present.c) ---- */
typedef struct present_state present_state_t;
present_state_t *present_new(void);
void present_free(present_state_t *st);
void present_invalidate(present_state_t *st);    /* 다음 번에 전부 다시 그리게 한다 */
/* 전체 화면을 터미널에 보낼 글자열로 바꾼다. 지난번과 달라진 칸만 담는다.
 * 터미널이 240x67칸보다 작으면 0을 돌려준다. 돌려주는 값은 out에 쓴 바이트 수다. */
size_t present_encode(present_state_t *st, const canvas_t *full, int cols, int rows,
                      char *out, size_t cap);
/* 터미널 글자 칸(0부터)을 전체 화면(240x134) 픽셀로 바꾼다. 화면은 터미널 가운데에 놓인다.
 * 화면 밖이면 0을 돌려준다. 글자 한 칸은 가로 1픽셀, 세로 2픽셀이고 위쪽 픽셀을 준다. */
int  present_cell_to_screen(int cols, int rows, int cell_x, int cell_y, int *sx, int *sy);

/* ---- 터미널 (term.c) ---- */
int  term_enter(void);           /* 원시 입력, 대체 화면, 커서 숨김, 마우스 보고를 켠다. 0이면 성공 */
void term_leave(void);           /* 전부 되돌린다. 여러 번 불러도 된다 */
int  term_size(int *cols, int *rows);

/* ---- 입력 해석 (input.c) ---- */
enum { IN_KEY = 1, IN_POINTER = 2 };
enum { IN_F_PRESSED = 1 };       /* 포인터: 왼쪽 버튼이 눌려 있다 */
enum { IN_K_UP = 0x101, IN_K_DOWN, IN_K_RIGHT, IN_K_LEFT };   /* 그 밖의 키는 글자 코드 그대로 */
typedef struct {
    uint8_t  kind;               /* IN_KEY 또는 IN_POINTER */
    uint8_t  flags;
    uint16_t code;               /* IN_KEY: 글자 코드 또는 IN_K_* */
    int16_t  x, y;               /* IN_POINTER: 터미널 글자 칸(0부터). 화면 픽셀로는 present_cell_to_screen으로 바꾼다 */
} in_event_t;
/* buf에서 사건 하나를 꺼낸다. 1: 꺼냈다(used에 쓴 바이트 수), 0: 바이트가 더 필요하다.
 * 모르는 바이트는 사건 없이 건너뛴다(used만 늘고 kind는 0). */
int  input_parse(const uint8_t *buf, size_t n, in_event_t *ev, size_t *used);
int  input_stage_key(const in_event_t *ev);      /* SK_* 또는 SK_NONE */

#endif
