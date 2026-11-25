#!/bin/sh
# 재접속: 게임 도중에 접속이 끊긴 사람이 세션 토큰을 대고 다시 들어오면 앉아 있던 자리를 돌려받는지 본다.
#   sh tests/rejoin.sh [포트]
# 봇에 --drop-at=N 을 주면 화면을 N장 받았을 때 스스로 접속을 끊는다. 그 뒤 클라이언트의 재접속 코드가 다시 붙는다.
#   a. 미니게임 도중에 끊겼다가 다시 붙는다: 같은 자리(me=1)로 랭킹까지 간다
#   b. 보스전 도중에 끊겼다가 다시 붙는다: 쓰러진 것으로 치지 않고 같은 자리로 끝까지 간다
#   c. 서버가 기다려 주지 않으면(--rejoin-ms=0) 다시 들어와도 관전자다(me=-1). 남은 사람은 끝까지 간다
PORT=${1:-4760}
B=${SSP_BUILD:-build/dev}
L=build/logs
mkdir -p $L
fail=0

run_case() {     # 이름, 서버 옵션, 끊는 때
    name=$1; sopt=$2; drop=$3
    RANK=$L/rejoin_${name}_ranking.txt
    rm -f "$RANK"
    timeout 60 $B/server --port=$PORT --once --seed=1 --order=example --ranking="$RANK" $sopt > $L/rejoin_${name}_server.log 2>&1 &
    SV=$!
    sleep 0.5
    timeout 60 $B/client --bot --port=$PORT --name=STAY --wait=2 > $L/rejoin_${name}_STAY.log 2>&1 &
    B1=$!
    sleep 0.2
    timeout 60 $B/client --bot --port=$PORT --name=DROP --wait=2 --drop-at=$drop > $L/rejoin_${name}_DROP.log 2>&1 &
    B2=$!
    wait $B1; r1=$?
    wait $B2; r2=$?
    wait $SV; rs=$?
    PORT=$((PORT + 1))
}

check() {        # 이름, 기대하는 내 자리
    name=$1; me=$2; why=""
    [ $r1 -eq 0 ] && grep -q ' boss=1$' $L/rejoin_${name}_STAY.log || why="$why 남은 사람이 끝까지 가지 못함($r1)"
    [ $r2 -eq 0 ] || why="$why 끊겼던 봇이 끝까지 가지 못함($r2)"
    grep -q '다시 접속했습니다' $L/rejoin_${name}_DROP.log || why="$why 봇이 다시 붙지 못함"
    grep -q " me=$me " $L/rejoin_${name}_DROP.log || why="$why 자리가 다름(기대 me=$me: $(grep '^BOT rounds' $L/rejoin_${name}_DROP.log))"
    [ $rs -eq 0 ] || why="$why 서버 종료 코드 $rs"
    if [ -z "$why" ]; then echo "통과 rejoin $name"; else echo "실패 rejoin $name:$why"; tail -n 4 $L/rejoin_${name}_server.log; fail=1; fi
}

run_case a "--fast --rounds=2 --boss=5" 100
check a 1
grep -q 'DROP 님이 다시 접속했습니다' $L/rejoin_a_server.log || { echo "실패 rejoin a: 서버가 자리를 돌려주지 않음"; fail=1; }
[ "$(grep -c '^DROP ' $L/rejoin_a_ranking.txt 2>/dev/null)" = 1 ] || { echo "실패 rejoin a: 랭킹에 끊겼던 사람의 기록이 없음"; fail=1; }

run_case b "--tick-ms=10 --ticks=60 --rounds=1 --boss=5" 80
check b 1
grep -q 'DROP 님이 다시 접속했습니다' $L/rejoin_b_server.log || { echo "실패 rejoin b: 서버가 자리를 돌려주지 않음"; fail=1; }

run_case c "--fast --rounds=2 --boss=5 --rejoin-ms=0" 100
check c -1

[ $fail -eq 0 ] && echo "통과 rejoin 전부"
exit $fail
