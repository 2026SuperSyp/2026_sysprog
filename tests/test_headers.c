/* 팀원용 헤더가 약속대로인지 본다. 헤더를 하나씩만 넣어도 컴파일돼야 한다. */
#include "stage.h"
#include "brain.h"
#include "screen.h"
#include "t.h"

int main(void)
{
    T_EQ_INT(SK_NONE, 0);
    T_EQ_INT(SK_LEFT, 1);
    T_EQ_INT(SK_9, 15);
    T_EQ_INT(BR_SUGAR, 0);
    T_EQ_INT(BR_VALENCE, 18);                        /* 전부터 있던 것들의 번호는 그대로다 */
    T_EQ_INT(BR_OBJECT_L, 19);                       /* 새 항목은 끝에만 붙는다(11/25: 몸을 움직이는 명령 뉴런 여섯) */
    T_EQ_INT(BRAIN_GROUP_COUNT, 25);
    T_EQ_INT(C_NONE, 0);
    T_EQ_INT(C_BROWN, 23);
    T_EQ_INT(SPR_MISSING, 0);
    T_EQ_INT(PANE_FLY, -1);
    T_EQ_INT(PANE_W, 94);
    T_EQ_INT(PANE_H, 108);
    T_EQ_INT(SCREEN_W, 240);
    T_EQ_INT(SCREEN_H, 134);
    T_EQ_INT(STAGE_MAX_PLAYERS, 8);
    T_EQ_INT(sizeof(((ranking_t *)0)->best_stage[0]), 32);
    T_EQ_INT(UI_KFONT_W, 12);
    T_EQ_INT(sizeof(((stage_result_t *)0)->score) / sizeof(float), STAGE_MAX_PLAYERS);
    T_CHECK(STAGE_TICK_DT > 0.049 && STAGE_TICK_DT < 0.051);
    T_DONE();
}
