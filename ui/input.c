/* 입력 해석: 터미널에서 온 바이트를 키와 마우스 사건으로 바꾼다.
 * 해석은 여기 한 곳에서만 한다. */
#include "ui/ui_internal.h"
#include "stage.h"

#include <string.h>

/* ESC [ < 버튼 ; 열 ; 행 M 또는 m (SGR 마우스 보고) */
static int parse_mouse(const uint8_t *buf, size_t n, in_event_t *ev, size_t *used)
{
    int v[3] = {0, 0, 0}, k = 0;
    for (size_t i = 3; i < n; i++) {
        uint8_t ch = buf[i];
        if (ch >= '0' && ch <= '9') {
            if (v[k] < 100000) v[k] = v[k] * 10 + (ch - '0');
        } else if (ch == ';') {
            if (++k > 2) { *used = i + 1; return 1; }      /* 형식이 틀리면 버린다 */
        } else if (ch == 'M' || ch == 'm') {
            *used = i + 1;
            if (k != 2) return 1;
            ev->kind = IN_POINTER;
            ev->x = (int16_t)(v[1] - 1);
            ev->y = (int16_t)(v[2] - 1);
            /* 버튼 값: 아래 두 비트가 버튼(0이 왼쪽), 32가 누른 채 움직임, 64가 휠(64는 위로, 65는 아래로).
             * 'M'은 눌림 또는 누른 채 움직임, 'm'은 뗌 */
            if (v[0] & 64) {
                ev->flags = (v[0] & 1) ? IN_F_WHEEL_DOWN : IN_F_WHEEL_UP;
            } else if ((v[0] & 3) == 0 && ch == 'M') {
                ev->flags = IN_F_PRESSED;
                if (!(v[0] & 32)) ev->flags |= IN_F_CLICK;      /* 움직임이 아니면 방금 누른 것이다 */
            } else if ((v[0] & 3) == 2 && ch == 'M' && !(v[0] & 32)) {
                ev->flags = IN_F_RCLICK;
            }
            return 1;
        } else {
            *used = i + 1;
            return 1;
        }
    }
    return 0;      /* 아직 끝이 안 왔다 */
}

int input_parse(const uint8_t *buf, size_t n, in_event_t *ev, size_t *used)
{
    memset(ev, 0, sizeof *ev);
    *used = 0;
    if (n == 0) return 0;

    if (buf[0] != 0x1B) {                  /* 보통 글자 */
        ev->kind = IN_KEY;
        ev->code = buf[0];
        if (buf[0] >= 'A' && buf[0] <= 'Z') ev->flags = IN_F_SHIFT;
        *used = 1;
        return 1;
    }
    if (n < 2) return 0;
    if (buf[1] != '[' && buf[1] != 'O') {  /* ESC 뒤에 다른 것이 오면 ESC만 버린다 */
        *used = 1;
        return 1;
    }
    if (n < 3) return 0;
    if (buf[1] == '[' && buf[2] == '<')
        return parse_mouse(buf, n, ev, used);

    /* ESC [ 1 ; 수 글자: 조합 키와 함께 누른 방향키. 수에서 1을 뺀 값의 첫 비트가 Shift다(Shift만이면 2) */
    if (buf[1] == '[' && buf[2] == '1' && n >= 4 && buf[3] == ';') {
        if (n < 6) return 0;
        uint16_t code = buf[5] == 'A' ? IN_K_UP : buf[5] == 'B' ? IN_K_DOWN
                      : buf[5] == 'C' ? IN_K_RIGHT : buf[5] == 'D' ? IN_K_LEFT : 0;
        if (buf[4] >= '2' && buf[4] <= '9' && code) {
            ev->kind = IN_KEY;
            ev->code = code;
            ev->flags = ((buf[4] - '1') & 1) ? IN_F_SHIFT : 0;
            *used = 6;
            return 1;
        }
    }
    switch (buf[2]) {
    case 'A': ev->kind = IN_KEY; ev->code = IN_K_UP;    *used = 3; return 1;
    case 'B': ev->kind = IN_KEY; ev->code = IN_K_DOWN;  *used = 3; return 1;
    case 'C': ev->kind = IN_KEY; ev->code = IN_K_RIGHT; *used = 3; return 1;
    case 'D': ev->kind = IN_KEY; ev->code = IN_K_LEFT;  *used = 3; return 1;
    }
    /* 그 밖의 제어 문자열은 끝 글자(0x40~0x7E)까지 건너뛴다 */
    for (size_t i = 2; i < n; i++) {
        if (buf[i] >= 0x40 && buf[i] <= 0x7E) { *used = i + 1; return 1; }
    }
    return 0;
}

int input_stage_key(const in_event_t *ev)
{
    if (!ev || ev->kind != IN_KEY) return SK_NONE;
    switch (ev->code) {
    case IN_K_LEFT:  case 'a': case 'A': return SK_LEFT;
    case IN_K_RIGHT: case 'd': case 'D': return SK_RIGHT;
    case IN_K_UP:    case 'w': case 'W': return SK_UP;
    case IN_K_DOWN:  case 's': case 'S': return SK_DOWN;
    case ' ':                            return SK_ACT;
    case 'f': case 'F':                  return SK_SUB;
    }
    if (ev->code >= '1' && ev->code <= '9') return SK_1 + (ev->code - '1');
    return SK_NONE;
}
