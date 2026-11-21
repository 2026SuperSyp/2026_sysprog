/* cfg.h : 설정 파일 불러오기 (코어 전용). 읽는 함수 cfg_get은 include/stage.h에 있다. */
#ifndef SSP_CFG_H
#define SSP_CFG_H

void cfg_load(const char *path);     /* 파일이 없으면 경고만 남긴다 */

#endif
