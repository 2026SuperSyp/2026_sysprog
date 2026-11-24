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

int input_parse(const uint8_t *buf, size_t n, in_event_t *ev, size_t *used);

/* ESC [ Vk ; Sc ; Uc ; Kd ; Cs ; Rc _   (win32-input-mode, Windows Terminal)
 * 키를 누를 때와 뗄 때 한 번씩 온다. Vk는 가상 키 번호, Uc는 그 키의 글자(없으면 0), Kd는 눌렀으면 1 뗐으면 0,
 * Cs는 같이 눌린 조합 키(0x10이 Shift)다. 누르고 있으면 Kd가 1인 것이 되풀이된다.
 * 키가 아닌 입력(마우스 보고 같은 제어 문자열)은 Vk가 0인 사건으로 한 글자씩 싸여 올 수 있다. 그런 글자는 모아서
 * 보통 때처럼 다시 해석한다. 돌려주는 값: 1 해석했다(사건이 없으면 kind가 0), 0 아직 다 안 왔다, -1 이 형식이 아니다 */
static int parse_win32(const uint8_t *buf, size_t n, in_event_t *ev, size_t *used)
{
    static uint8_t down[256];            /* 가상 키 번호마다: 눌려 있는가. 되풀이를 가려내는 데 쓴다 */
    static uint8_t raw[64];              /* 싸여 온 글자를 모은 것 */
    static size_t n_raw;
    int v[6] = { 0, 0, 0, 0, 0, 0 }, k = 0;
    size_t i = 2;
    for (; i < n; i++) {
        uint8_t ch = buf[i];
        if (ch >= '0' && ch <= '9') { if (k < 6 && v[k] < 1000000) v[k] = v[k] * 10 + (ch - '0'); }
        else if (ch == ';') k++;
        else break;
    }
    if (i >= n) return 0;
    if (buf[i] != '_') return -1;
    *used = i + 1;
    int vk = v[0] & 0xff, uc = v[2], kd = v[3], shift = (v[4] & 0x10) != 0;
    if (v[0] == 0) {                                 /* 싸여 온 글자 */
        if (!kd || uc <= 0) return 1;
        if (n_raw + 4 > sizeof raw) n_raw = 0;
        if (uc < 0x80) raw[n_raw++] = (uint8_t)uc;
        else if (uc < 0x800) { raw[n_raw++] = (uint8_t)(0xC0 | (uc >> 6)); raw[n_raw++] = (uint8_t)(0x80 | (uc & 0x3F)); }
        else {
            raw[n_raw++] = (uint8_t)(0xE0 | ((uc >> 12) & 0x0F));
            raw[n_raw++] = (uint8_t)(0x80 | ((uc >> 6) & 0x3F));
            raw[n_raw++] = (uint8_t)(0x80 | (uc & 0x3F));
        }
        in_event_t got;
        size_t took;
        while (n_raw > 0 && input_parse(raw, n_raw, &got, &took)) {
            memmove(raw, raw + took, n_raw - took);
            n_raw -= took;
            if (got.kind) { *ev = got; break; }
        }
        return 1;
    }
    uint16_t code = vk == 0x25 ? IN_K_LEFT : vk == 0x26 ? IN_K_UP : vk == 0x27 ? IN_K_RIGHT : vk == 0x28 ? IN_K_DOWN
                  : (uc > 0 && uc < 0x80 ? (uint16_t)uc : 0);
    if (!code) {                                     /* Shift 같은 조합 키만 눌렀거나 뗐다 */
        down[vk] = (uint8_t)(kd != 0);
        return 1;
    }
    ev->code = code;
    ev->flags = (uint8_t)(IN_F_HELD | (shift ? IN_F_SHIFT : 0));
    if (kd) {
        ev->kind = IN_KEY;
        if (down[vk]) ev->flags |= IN_F_REPEAT;
        down[vk] = 1;
    } else {
        ev->kind = IN_KEY_UP;
        down[vk] = 0;
    }
    return 1;
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
    if (buf[1] == '[' && buf[2] >= '0' && buf[2] <= '9') {       /* 누름과 뗌을 따로 알리는 키 사건인가 */
        int r = parse_win32(buf, n, ev, used);
        if (r >= 0) return r;
        *used = 0;
    }

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
    case 'O': if (buf[1] == '[') ev->kind = IN_BLUR; *used = 3; return 1;      /* ESC [ O: 창이 뒤로 갔다 */
    case 'I': *used = 3; return 1;                                             /* ESC [ I: 창이 앞으로 왔다 */
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
