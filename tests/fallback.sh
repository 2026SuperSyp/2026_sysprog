#!/bin/sh
# 뇌가 뜨지 못할 때 규칙으로 움직이는 뇌(fast)로 바꿔 판을 이어 가는지 본다.
#   sh tests/fallback.sh [포트]      포트부터 5개를 쓴다(기본 4680~4684)
# 검사
#   e. 모르는 모델 이름이면 서버가 서지 않는다
#   a. 진짜 뇌의 자료가 없는 폴더로 --brain=real: 서버가 fast로 바꿔 서고 한 판(2라운드)이 끝난다
#   b. 같은 것을 --brain-strict로: 서버가 서지 않는다
#   c. 진짜 뇌로 돌다가 자료를 지우고 뇌를 죽인다: 3번 실패한 뒤 fast로 바꾸고 판이 끝난다. 화면도 점 격자로 바뀐다
#   d. 자료는 그대로 두고 뇌를 한 번 죽인다: 같은 모델(real)로 다시 띄운다
# c와 d는 data/brain.bin이 없으면 건너뛴다(make brain-data가 만든다).
# 뇌의 자료 폴더는 환경 변수 SSP_BRAIN_DATA로 바꾼다(brain/brain_shm.h).
PORT=${1:-4680}
B=${SSP_BUILD:-build/dev}
L=build/logs
mkdir -p $L
: > $L/stage_example.log
fail=0
T=$(mktemp -d)
trap 'rm -rf $T' EXIT

ok()   { echo "통과 fallback $1"; }
bad()  { echo "실패 fallback $1"; fail=$((fail + 1)); }
skip() { echo "건너뜀 fallback $1"; }

SWITCH="규칙으로 움직이는 뇌(fast)로 바꿉니다"

# start_game <이름> <포트> <라운드 수> [서버 옵션...] : 서버와 봇 2명을 띄운다. pid는 SV, B1, B2에 둔다
start_game() {
    n=$1; p=$2; r=$3; shift 3
    $B/server --ranking=$L/fallback_ranking.txt --port=$p --fast --once --seed=1 --order=example --rounds=$r "$@" \
        > $L/fallback_${n}_server.log 2>&1 &
    SV=$!
    # 서버가 포트를 열 때까지 기다린다(뇌를 바꾸는 동안 늦어질 수 있다)
    i=0
    while ! grep -q "기다립니다" $L/fallback_${n}_server.log && kill -0 $SV 2>/dev/null && [ $i -lt 300 ]; do sleep 0.05; i=$((i + 1)); done
    sleep 1.2                # 뜬 지 1초가 안 된 뇌를 죽이는 경우를 피한다
    timeout 60 $B/client --bot --port=$p --name=BOT1 --wait=2 > $L/fallback_${n}_bot1.log 2>&1 &
    B1=$!
    sleep 0.2
    timeout 60 $B/client --bot --port=$p --name=BOT2 --wait=2 > $L/fallback_${n}_bot2.log 2>&1 &
    B2=$!
}

finish_game() {
    wait $B1; r1=$?
    wait $B2; r2=$?
    i=0
    while kill -0 $SV 2>/dev/null && [ $i -lt 100 ]; do sleep 0.05; i=$((i + 1)); done
    if kill -0 $SV 2>/dev/null; then kill -9 $SV; wait $SV; rs=99; else wait $SV; rs=$?; fi
}

wait_playing() {
    i=0
    while [ $i -lt 400 ]; do
        if [ -n "$(pgrep -P $SV -f stage_)" ]; then sleep 0.25; return 0; fi
        sleep 0.02; i=$((i + 1))
    done
    return 1
}

brain_pid() { pgrep -P $SV -x brain_proc | head -n 1; }
values() { grep "^BRAIN shm=/ssp_brain_$SV " $L/stage_example.log | tr ' ' '\n' | sed -n "s/^$1=//p"; }
last()   { values $1 | tail -n 1; }
first()  { values $1 | head -n 1; }
ms()     { echo $(($(date +%s%N) / 1000000)); }

# 서버가 끝난 뒤 이 서버의 공유 메모리 이름을 물려받은 프로세스(뇌, 스테이지)와 공유 메모리가 남지 않았는가
heirs() { grep -lza "^SSP_BRAIN_SHM=/ssp_brain_$SV\$" /proc/[0-9]*/environ 2>/dev/null | wc -l; }
clean() {
    i=0
    while [ "$(heirs)" != "0" ] && [ $i -lt 40 ]; do sleep 0.05; i=$((i + 1)); done
    [ "$(heirs)" = "0" ] && [ ! -e /dev/shm/ssp_brain_$SV ]
}

# ---- e. 모르는 모델 --------------------------------------------------------------
timeout 20 $B/server --ranking=$L/fallback_ranking.txt --port=$PORT --brain=bogus > $L/fallback_e_server.log 2>&1
re=$?
if [ $re -ne 0 ] && [ $re -ne 124 ] && grep -q "모르는 뇌 모델" $L/fallback_e_server.log; then
    ok "e 모르는 모델이면 서버가 서지 않는다 (종료 코드 $re)"
else
    bad "e 종료 코드=$re"; tail -n 3 $L/fallback_e_server.log
fi

# ---- a. 자료 없는 폴더로 real -> fast로 바꿔 판이 끝난다 ----------------------------
mkdir -p $T/empty
SSP_BRAIN_DATA=$T/empty start_game a $((PORT + 1)) 2 --brain=real
finish_game
line=$(grep "$SWITCH" $L/fallback_a_server.log | head -n 1)
map=$(last map)
if [ $r1 -eq 0 ] && [ $r2 -eq 0 ] && [ $rs -eq 0 ] && [ -n "$line" ] && [ "$map" = "0" ] && clean; then
    ok "a 자료가 없어도 서버가 fast로 서고 판이 끝났다: $line"
else
    bad "a 봇1=$r1 봇2=$r2 서버=$rs 바꿈 줄=[$line] 마지막 뇌 지도=$map"; tail -n 5 $L/fallback_a_server.log
fi

# ---- b. 같은 것을 --brain-strict로 ------------------------------------------------
SSP_BRAIN_DATA=$T/empty timeout 30 $B/server --ranking=$L/fallback_ranking.txt --port=$((PORT + 2)) --brain=real --brain-strict \
    > $L/fallback_b_server.log 2>&1
rb=$?
if [ $rb -ne 0 ] && [ $rb -ne 124 ] && ! grep -q "$SWITCH" $L/fallback_b_server.log; then
    ok "b --brain-strict이면 서버가 서지 않는다 (종료 코드 $rb)"
else
    bad "b 종료 코드=$rb"; tail -n 3 $L/fallback_b_server.log
fi

if [ ! -f data/brain.bin ]; then
    skip "c d (data/brain.bin 없음. make brain-data 뒤에 돈다)"
else
    # ---- c. 진짜 뇌로 돌다가 자료를 지우고 죽인다 ----------------------------------
    mkdir -p $T/real
    for f in brain.bin brain_groups.txt brain_map.bin; do [ -e data/$f ] && ln -s "$(pwd)/data/$f" $T/real/$f; done
    SSP_BRAIN_DATA=$T/real start_game c $((PORT + 3)) 3 --brain=real
    # 첫 라운드는 진짜 뇌로 끝까지 돌게 두고(뇌 지도가 그려지는지 본다), 둘째 라운드 도중에 죽인다
    i=0
    while [ -z "$(first map)" ] && [ $i -lt 600 ]; do sleep 0.05; i=$((i + 1)); done
    wait_playing; old=$(brain_pid)
    rm -f $T/real/*
    t0=$(ms)
    [ -n "$old" ] && kill -9 $old
    i=0
    while ! grep -q "$SWITCH" $L/fallback_c_server.log && [ $i -lt 400 ]; do sleep 0.05; i=$((i + 1)); done
    took=$(($(ms) - t0))
    finish_game
    line=$(grep "$SWITCH" $L/fallback_c_server.log | head -n 1)
    map0=$(first map); map=$(last map)
    if [ -n "$old" ] && [ $r1 -eq 0 ] && [ $r2 -eq 0 ] && [ $rs -eq 0 ] && [ -n "$line" ] && [ "$map0" = "1" ] && [ "$map" = "0" ] && clean; then
        ok "c 자료를 지우고 죽이자 ${took}ms 뒤 fast로 바꿨고 판이 끝났다 (뇌 지도 처음 $map0 -> 끝 $map): $line"
    else
        bad "c 봇1=$r1 봇2=$r2 서버=$rs 바꿈 줄=[$line] 뇌 지도 처음=$map0 끝=$map"; tail -n 8 $L/fallback_c_server.log
    fi

    # ---- d. 자료는 그대로, 한 번 죽인다 ---------------------------------------------
    start_game d $((PORT + 4)) 3 --brain=real
    wait_playing; old=$(brain_pid)
    [ -n "$old" ] && kill -9 $old
    finish_game
    again=$(grep "뇌를 다시 띄웠습니다.*모델 real" $L/fallback_d_server.log | head -n 1)
    map=$(last map)
    if [ -n "$old" ] && [ $r1 -eq 0 ] && [ $r2 -eq 0 ] && [ $rs -eq 0 ] && [ -n "$again" ] && \
       ! grep -q "$SWITCH" $L/fallback_d_server.log && [ "$map" = "1" ] && clean; then
        ok "d 한 번 죽이면 같은 모델로 다시 띄운다: $again"
    else
        bad "d 봇1=$r1 봇2=$r2 서버=$rs 다시 띄움=[$again] 마지막 뇌 지도=$map"; tail -n 5 $L/fallback_d_server.log
    fi
fi

if [ $fail -eq 0 ]; then
    echo "통과 fallback 전부"
    exit 0
fi
echo "실패 fallback $fail건. 로그: $L/fallback_*_server.log $L/brain.log"
exit 1
