#!/bin/sh
# 서버와 봇 접속자 2명이 화면 없이 로비부터 랭킹까지 가는지 본다.
#   sh tests/e2e.sh [포트]
# 다른 빌드를 보려면 SSP_BUILD=build/release 처럼 폴더를 준다(기본 build/dev).
# 서버는 --fast(기다리지 않고 진행)와 --once(한 판 뒤 종료)로 띄운다. 라운드 뒤에 짧은 보스전(--boss=5)도 한다.
PORT=${1:-4699}
B=${SSP_BUILD:-build/dev}
mkdir -p build/logs
RANK=build/logs/e2e_ranking_$PORT.txt        # 검사용 랭킹 파일. data/ranking.txt는 건드리지 않는다
rm -f "$RANK"

timeout 90 $B/server --port=$PORT --fast --once --seed=1 --order=example --rounds=2 --boss=5 --ranking="$RANK" > build/logs/e2e_server.log 2>&1 &
SV=$!
sleep 0.5
timeout 90 $B/client --bot --port=$PORT --name=BOT1 --wait=2 > build/logs/e2e_bot1.log 2>&1 &
B1=$!
sleep 0.2
timeout 90 $B/client --bot --port=$PORT --name=BOT2 --wait=2 > build/logs/e2e_bot2.log 2>&1 &
B2=$!

wait $B1; r1=$?
wait $B2; r2=$?
wait $SV; rs=$?

# 랭킹 파일에 봇 두 명의 기록이 한 줄씩 있어야 하고, 봇은 그 둘이 든 역대 순위를 받았어야 한다
rk=1
if [ "$(grep -c '^BOT1 ' "$RANK" 2>/dev/null)" = 1 ] && [ "$(grep -c '^BOT2 ' "$RANK" 2>/dev/null)" = 1 ] &&
   grep -q ' top=2 boss=1$' build/logs/e2e_bot1.log && grep -q ' top=2 boss=1$' build/logs/e2e_bot2.log; then rk=0; fi
# 2라운드 뒤에 보스전(5초 길이)을 하고 끝났어야 한다
grep -q '보스전 끝' build/logs/e2e_server.log || rk=1

if [ $r1 -eq 0 ] && [ $r2 -eq 0 ] && [ $rs -eq 0 ] && [ $rk -eq 0 ]; then
    echo "통과 e2e (서버 + 봇 2명): $(cat build/logs/e2e_bot1.log)"
    exit 0
fi
echo "실패 e2e: 봇1=$r1 봇2=$r2 서버=$rs 랭킹=$rk"
tail -n 5 build/logs/e2e_server.log build/logs/e2e_bot1.log build/logs/e2e_bot2.log "$RANK"
exit 1
