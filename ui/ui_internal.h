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
void canvas_blit_scaled(canvas_t *dst, int dx, int dy, const canvas_t *src, int scale);   /* 가로세로 scale배로 키워서 놓는다. C_NONE은 건너뛴다 */

/* ---- 색 (palette.c) ---- */
/* 코어의 그림에만 쓰는 색. 스테이지가 쓰는 색(ui.h) 뒤에 이어 붙인다. */
#define CX_TAN    ((color_t)24)   /* 밝은 황토 */
#define CX_CLAY   ((color_t)25)   /* 황토 */
#define CX_MAROON ((color_t)26)   /* 어두운 빨강 */
#define CX_TEAL   ((color_t)27)   /* 어두운 청록 */
#define CX_SLATE  ((color_t)28)   /* 어두운 남회색 */
/* 보스전의 3D 화면에 쓰는 단계 색. 0이 가장 밝고(가깝고) 번호가 클수록 어둡다(멀다) */
#define CX_SHADES 7
#define CX_WALL0  ((color_t)29)   /* 바깥 벽: 29..35 */
#define CX_PILL0  ((color_t)36)   /* 기둥: 36..42 */
#define CX_FLOORS 5
#define CX_FLOOR0 ((color_t)43)   /* 바닥: 43..47. 0이 발밑이다 */
#define CX_CEILS  4
#define CX_CEIL0  ((color_t)48)   /* 천장: 48..51. 0이 머리 위다 */
void ui_palette_rgb(color_t c, uint8_t rgb[3]);

/* ---- 글자 (font.c, utf8.c) ---- */
#define UI_KFONT_LATIN_DY 3      /* 한글이 섞인 글자열에서 영문과 숫자를 이만큼 내려 그린다(ui_text) */
int    ui_text_h(const char *s);                             /* 한 줄의 높이. 한글이 섞였으면 12, 아니면 8 */
/* 받아 두기. 화면을 픽셀 그림으로 보낼 때(ui/hd.h) 글자와 몇 가지 그림은 캔버스에 그린 것을 키우지 않고 실제
 * 해상도로 다시 그린다(ui/hd_art.c). begin과 end 사이에 target 캔버스에 ui_text로 그리는 글자는 캔버스에 그려지지
 * 않고 목록에 쌓인다. 말풍선의 상자도 그린 쪽이 ui_capture_note로 알려 두고 캔버스에는 그리지 않는다.
 * 그림(.spr) 가운데 진행자 시프봇도 알려 두고 캔버스에는 그리지 않는다(같은 그림을 매끈하게 키워 그린다).
 * 다른 그림은 받아 두지 않는다(캔버스에 그대로 그린다).
 * end는 쌓인 개수를 돌려주고 목록을 items에 준다(다음 begin까지 유효하다).
 * 목록에는 그린 순서대로 쌓인다. 목록이 차면 그 뒤의 것은 그냥 캔버스에 그려진다. */
enum { UI_HD_TEXT = 0,           /* 글자: s를 (x, y)에 col로 */
       UI_HD_BUBBLE,             /* 말풍선 상자: (x, y)에 w x h. 꼬리의 자리가 a(없으면 -1) */
       UI_HD_SPRITE };           /* 그림: 이름이 s인 그림(w x h)의 a번째 장을 (x, y)에 */
typedef struct { uint8_t kind; color_t col; int16_t x, y, w, h, a; char s[128]; } ui_text_item_t;
void   ui_text_capture_begin(const canvas_t *target);
int    ui_text_capture_end(const ui_text_item_t **items);
/* c가 받아 두는 캔버스이면 목록에 올리고 1을 돌려준다(그러면 부른 쪽은 캔버스에 그리지 않는다). 아니면 0 */
int    ui_capture_note(const canvas_t *c, int kind, int x, int y, int w, int h, int a, const char *s);
/* 그림 한 장의 픽셀(w x h개의 색 번호, 빈 곳은 C_NONE). 없는 그림이면 NULL */
const color_t *ui_sprite_pixels(sprite_id_t id, int frame, int *w, int *h);
int    utf8_next(const char **s);                            /* 글자 하나를 읽고 다음으로 넘어간다. 끝이면 0 */
size_t utf8_copy(char *dst, size_t cap, const char *src);    /* 넘치면 글자 경계에서 잘라 담는다 */

/* ---- 그림 (sprite.c) ---- */
void ui_set_stage_dir(const char *dir);      /* 예: "stages/example". NULL이면 스테이지 그림을 찾지 않는다 */

/* ---- 이펙트 (fx.c) ---- */
void fx_reset(void);
void fx_apply(int pane, canvas_t *c);        /* 그 칸에 걸린 이펙트를 그림 위에 입힌다 */
void fx_tick(void);                          /* 남은 시간을 한 틱 줄인다 */
int  fx_call_count(void);                    /* 지금까지 fx_*가 불린 횟수 */
void fx_set_in_draw(int on);                 /* 켜져 있는 동안 fx_*가 불리면 경고를 남긴다 */

/* ---- 키보드 치는 초파리 (flycam.c) ---- */
#define WATCH_FLY (-2)
/* (x, y)부터 47x54 크기로 그린다. rate는 뇌의 집단별 반응이고 없으면 NULL이다.
 * screen은 초파리 앞의 모니터에 띄울 그림(초파리 칸)이고 없으면 NULL이다. */
void flycam_draw(canvas_t *c, int x, int y, const float *rate, int frame, const canvas_t *screen);

/* ---- 합성 (compose.c) ---- */
struct brain_map;                /* brain/brain_host.h */
typedef struct {
    const char *title;           /* 위 띠: 게임 이름 */
    int ticks_left;              /* 위 띠: 남은 틱 */
    int round;                   /* 위 띠: 라운드 (1부터) */
    int my_score;                /* 아래 띠 */
    int fly_score;               /* 아래 띠 */
    int lives;                   /* 아래 띠: 남은 기회 */
    int watch;                   /* 오른쪽 위에 보여 줄 다른 참가자. 없으면 -1, 키보드 치는 초파리는 WATCH_FLY */
    const float *brain_stim;     /* 오른쪽 아래 뇌 표시: 집단별 자극(BRAIN_GROUP_COUNT개). 없으면 NULL */
    const float *brain_rate;     /* 오른쪽 아래 뇌 표시: 집단별 반응 */
    const struct brain_map *brain_map;   /* 진짜 뇌일 때의 뇌 지도. 없으면 NULL이고 그때는 점 격자 그림을 쓴다 */
} hud_t;
/* 전체 화면에 위 띠, 초파리 칸, 내 칸, 다른 참가자 한 명(절반 크기), 뇌 표시, 아래 띠를 놓는다.
 * panes[i]는 참가자 i의 칸이고 me는 가운데에 크게 놓을 참가자다. */
void compose_stage_frame(canvas_t *full, const canvas_t *fly, canvas_t *const panes[],
                         int n_players, int me, const hud_t *hud);
/* 전체 화면의 좌표가 me의 칸 안이면 1을 돌려주고 칸 기준 좌표를 채운다. */
int  compose_my_pane_xy(int screen_x, int screen_y, int *pane_x, int *pane_y);
/* 초파리 뇌 표시 하나를 (x, y)부터 BRAIN_PANEL_W x BRAIN_PANEL_H 크기로 그린다(테두리는 그 바깥 한 픽셀).
 * 미니게임 화면의 오른쪽 아래에 놓는 것과 같은 그림이고, 보스전 화면도 이것을 쓴다.
 * map이 있으면 진짜 뇌의 지도를, 없으면 점 격자 그림을 그린다. stim, rate는 없으면 NULL이다. */
#define BRAIN_PANEL_W (PANE_W / 2)
#define BRAIN_PANEL_H (PANE_H / 2)
void compose_brain_panel(canvas_t *c, int x, int y, const float *stim, const float *rate,
                         const struct brain_map *map, int frame);

/* ---- 표시 (present.c) ---- */
typedef struct present_state present_state_t;
present_state_t *present_new(void);
void present_free(present_state_t *st);
void present_invalidate(present_state_t *st);    /* 다음 번에 전부 다시 그리게 한다 */
#define PRESENT_MAX_SCALE 6      /* 화면을 키우는 배율의 한도. 배율 N의 화면은 240N x 134N 픽셀, 터미널 240N칸 67N줄이다 */
/* 전체 화면을 터미널에 보낼 글자열로 바꾼다. 지난번과 달라진 칸만 담는다.
 * full은 240x134이거나 그것을 배율 N으로 키운 크기다. 터미널이 그보다 작으면(배율 1이면 240x67칸)
 * 0을 돌려준다. 돌려주는 값은 out에 쓴 바이트 수다. */
size_t present_encode(present_state_t *st, const canvas_t *full, int cols, int rows,
                      char *out, size_t cap);
/* 터미널 글자 칸(0부터)을 전체 화면(240x134) 픽셀로 바꾼다. 화면은 터미널 가운데에 놓인다.
 * 화면 밖이면 0을 돌려준다. 글자 한 칸은 가로 1픽셀, 세로 2픽셀이고 위쪽 픽셀을 준다. */
int  present_cell_to_screen(int cols, int rows, int cell_x, int cell_y, int *sx, int *sy);
/* 배율 scale로 키운 화면을 그리고 있을 때 쓴다. 돌려주는 좌표는 키우기 전(240x134)의 픽셀이다. */
int  present_cell_to_screen_scaled(int cols, int rows, int scale, int cell_x, int cell_y, int *sx, int *sy);

/* ---- 터미널 (term.c) ---- */
int  term_enter(void);           /* 원시 입력, 대체 화면, 커서 숨김, 마우스 보고를 켠다. 0이면 성공 */
void term_leave(void);           /* 전부 되돌린다. 여러 번 불러도 된다 */
int  term_size(int *cols, int *rows);   /* 터미널의 칸 수. 크기가 바뀌었을 때(SIGWINCH)만 커널에 다시 묻는다. 터미널이 아니면 -1 */
int  term_size_asked(void);             /* 커널에 물은 횟수(검사용) */
/* 터미널이 할 수 있는 것. term_enter가 터미널에 물어서 채운다(답이 없으면 sixel은 0이고 칸은 10x20으로 둔다) */
typedef struct {
    int sixel;                   /* Sixel 그림을 받는다 */
    int cell_w, cell_h;          /* 그림을 놓을 때 글자 한 칸으로 치는 픽셀 수 */
} term_caps_t;
const term_caps_t *term_caps(void);

/* ---- 입력 해석 (input.c) ---- */
/* IN_KEY_UP: 누르고 있던 키를 뗐다. 터미널이 누름과 뗌을 따로 알려 줄 때만 온다(IN_F_HELD).
 * IN_BLUR: 창이 뒤로 갔다(이 뒤로는 뗀 것을 받지 못한다). 3은 프로토콜의 INPUT_LOOK이 쓴다 */
enum { IN_KEY = 1, IN_POINTER = 2, IN_KEY_UP = 4, IN_BLUR = 5 };
enum { IN_F_PRESSED = 1,         /* 포인터: 왼쪽 버튼이 눌려 있다 */
       IN_F_SHIFT = 2,           /* 키: Shift와 함께 눌렀다(대문자, Shift+방향키). 보스전에서 그 걸음을 살금살금 걷는 데 쓴다 */
       IN_F_CLICK = 4,           /* 포인터: 왼쪽 버튼을 방금 눌렀다(누른 채 움직이는 동안에는 없다) */
       IN_F_WHEEL_UP = 8,        /* 포인터: 휠을 위로 굴렸다. 버튼이 눌린 것으로 치지 않는다 */
       IN_F_WHEEL_DOWN = 16,
       IN_F_RCLICK = 32,         /* 포인터: 오른쪽 버튼을 방금 눌렀다 */
       IN_F_REPEAT = 64,         /* 키: 누르고 있어서 되풀이된 것이다(IN_F_HELD일 때만 가려낸다) */
       IN_F_HELD = 128 };        /* 키: 터미널이 누름과 뗌을 따로 알려 준다. 이 키를 떼면 IN_KEY_UP이 온다 */
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
