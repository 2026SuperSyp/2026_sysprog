#!/bin/sh
# 스테이지 하나가 서버에서도 도는지 본다: 서버와 봇 접속자 2명이 그 스테이지로 2라운드를 하고 랭킹까지 간다.
#   sh tests/stage.sh <스테이지 이름> [포트] [뇌: fast|slow|real]      (make test-stage STAGE=이름 [BRAIN=real])
# 보는 것: 봇과 서버가 정상 종료했는가, 무효가 된 라운드가 없는가, 스테이지 로그에 메모리 검사 오류와 경고가 없는가.
# make run / make bot 에서 도는 스테이지가 서버에서는 안 도는 경우(결과를 안 보냄, 너무 느림, 죽음)를 미리 잡는다.
STAGE=$1
PORT=${2:-4670}
BRAIN=${3:-fast}
B=${SSP_BUILD:-build/dev}
L=build/logs
[ -n "$STAGE" ] || { echo "스테이지 이름을 주세요: sh tests/stage.sh <이름>"; exit 2; }
[ -x $B/stage_$STAGE ] || { echo "실패: $B/stage_$STAGE 가 없습니다. 먼저 make all"; exit 1; }
mkdir -p $L
LOG=$L/stage_$STAGE.log
before=$(wc -l < $LOG 2>/dev/null || echo 0)

timeout 120 $B/server --port=$PORT --fast --once --seed=1 --order=$STAGE --rounds=2 --boss=0 --brain=$BRAIN \
    --ranking=$L/stage_test_ranking.txt > $L/stage_test_server.log 2>&1 &
SV=$!
i=0
while ! grep -q "기다립니다" $L/stage_test_server.log && kill -0 $SV 2>/dev/null && [ $i -lt 300 ]; do sleep 0.05; i=$((i + 1)); done
timeout 120 $B/client --bot --port=$PORT --name=BOT1 --wait=2 > $L/stage_test_bot1.log 2>&1 &
B1=$!
sleep 0.2
timeout 120 $B/client --bot --port=$PORT --name=BOT2 --wait=2 > $L/stage_test_bot2.log 2>&1 &
B2=$!
wait $B1; r1=$?
wait $B2; r2=$?
wait $SV; rs=$?

new=$(tail -n +$((before + 1)) $LOG 2>/dev/null)
why=""
[ $r1 -eq 0 ] && [ $r2 -eq 0 ] || why="$why 봇이 랭킹까지 가지 못함($r1,$r2)"
[ $rs -eq 0 ] || why="$why 서버 종료값=$rs"
grep -q "무효" $L/stage_test_server.log && why="$why 무효가 된 라운드가 있음($(grep -m1 '무효' $L/stage_test_server.log))"
echo "$new" | grep -q -E "AddressSanitizer|LeakSanitizer|runtime error" && why="$why 스테이지 로그에 메모리 검사 오류"
warn=$(echo "$new" | grep -c "^경고")
[ "$warn" -eq 0 ] || why="$why 스테이지 경고 ${warn}개"

if [ -z "$why" ]; then
    echo "통과 $STAGE (서버 + 봇 2명, 뇌 $BRAIN): $(cat $L/stage_test_bot1.log)"
    echo "$new" | grep "^BRAIN" | tail -n 1 | sed 's/ wait_ms_sum.*//'
    exit 0
fi
echo "실패 $STAGE:$why"
echo "  로그: $L/stage_test_server.log $LOG"
tail -n 5 $L/stage_test_server.log
exit 1
