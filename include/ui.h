/* ui.h : 그리기 약속
 *
 * 스테이지와 화면은 이 헤더의 함수로만 그린다.
 *  - 스테이지가 그리는 칸은 항상 PANE_W x PANE_H 한 가지다.
 *  - 칸의 왼쪽 위가 (0, 0)이다. 캔버스 밖 좌표는 잘라낸다(죽지 않는다).
 *  - 색은 아래 이름표만 쓴다. 숫자로 색을 쓰지 않는다. */
#ifndef SSP_UI_API_H
#define SSP_UI_API_H

#include <stdint.h>

#define SCREEN_W  240      /* 전체 화면(고정 좌표) */
#define SCREEN_H  134
#define PANE_W    94       /* 스테이지가 그리는 칸 */
#define PANE_H    108
#define UI_FONT_W 6        /* 글자 한 칸의 크기(자간, 행간 포함) */
#define UI_FONT_H 8

typedef struct canvas canvas_t;    /* 속을 보지 않는다. ui_* 함수로만 그린다 */
typedef uint8_t color_t;           /* 아래 이름표만 쓴다 */
typedef int sprite_id_t;

enum {                             /* 새 색은 끝에만 추가한다 */
    C_NONE = 0,                    /* 투명. 그리지 않는다 */
    C_BG, C_TEXT,
    C_FLY_BODY, C_FLY_EYE, C_WING, C_WEB,
    C_GOOD, C_BAD, C_WARN,
    C_WHITE, C_BLACK, C_GRAY1, C_GRAY2, C_GRAY3,
    C_RED, C_ORANGE, C_YELLOW, C_GREEN, C_CYAN, C_BLUE, C_PURPLE, C_PINK, C_BROWN
};

int  ui_width (const canvas_t *c);
int  ui_height(const canvas_t *c);
void ui_clear (canvas_t *c, color_t bg);
void ui_px    (canvas_t *c, int x, int y, color_t col);
void ui_rect  (canvas_t *c, int x, int y, int w, int h, color_t col);        /* 채운 사각형 */
void ui_line  (canvas_t *c, int x0, int y0, int x1, int y1, color_t col);
void ui_bar   (canvas_t *c, int x, int y, int w, float ratio, color_t col);  /* 높이 4픽셀 */

/* 글자. 대문자, 숫자, 공백, 그리고 ' ! ? / : . , - + % 를 그린다.
 * 소문자는 대문자로 바뀌고 없는 글자는 빈칸이 된다.
 * 칸 안에서는 한 줄에 15자, 전체 화면에서는 40자가 들어간다. */
void ui_text  (canvas_t *c, int x, int y, const char *s, color_t col);
int  ui_text_w(const char *s);                                               /* 글자열의 가로 픽셀 수 */

/* 그림.
 *  - ui_sprite_load("swatter")는 자기 폴더의 assets/swatter.spr을 읽는다.
 *    같은 이름은 같은 번호를 돌려주고, 두 번째부터는 파일을 읽지 않는다.
 *  - 파일이 없거나 틀리면 죽지 않고 SPR_MISSING을 돌려주며 로그에 이유를 남긴다.
 *  - ui_sprite의 (x, y)는 그림의 왼쪽 위다. 크기는 조회 함수로 얻는다.
 *  - SPR_FLY_* 네 장은 같은 크기다. frame은 프레임 수로 나눈 나머지를 쓴다. */
enum { SPR_MISSING = 0, SPR_FLY_IDLE, SPR_FLY_EAT, SPR_FLY_JUMP, SPR_FLY_HIT };
sprite_id_t ui_sprite_load  (const char *name);
int         ui_sprite_w     (sprite_id_t id);
int         ui_sprite_h     (sprite_id_t id);
int         ui_sprite_frames(sprite_id_t id);
void        ui_sprite(canvas_t *c, sprite_id_t id, int x, int y, int frame);

/* 손맛 이펙트. 첫 인자는 칸 번호다.
 *  - 사건이 난 틱에 stage_tick, stage_input, stage_pointer에서 부른다.
 *    stage_draw에서는 부르지 않는다.
 *  - 지속 시간은 코어가 틱 단위로 센다(50ms 단위로 올림).
 *  - fx_trail은 움직이는 동안 매 틱 부른다. */
#define PANE_FLY (-1)              /* 초파리 칸. 0 이상은 그 번호 참가자의 칸 */
void fx_shake    (int pane, int strength, int ms);   /* 칸을 strength픽셀만큼 ms 동안 흔든다 */
void fx_shockwave(int pane, int x, int y);           /* (x, y)에서 퍼지는 충격파 한 번 */
void fx_trail    (int pane, int x, int y);           /* (x, y)에 잔상 하나 */
void fx_flash    (int pane, color_t col, int ms);    /* 칸 전체가 col로 번쩍인다 */

#endif
