#!/bin/sh
# 서버와 봇 접속자 2명이 화면 없이 로비부터 랭킹까지 가는지 본다.
#   sh tests/e2e.sh [포트]
# 서버는 --fast(기다리지 않고 진행)와 --once(한 판 뒤 종료)로 띄운다.
PORT=${1:-4699}
B=build/dev
mkdir -p build/logs

timeout 90 $B/server --port=$PORT --fast --once --seed=1 --order=example --rounds=2 > build/logs/e2e_server.log 2>&1 &
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

if [ $r1 -eq 0 ] && [ $r2 -eq 0 ] && [ $rs -eq 0 ]; then
    echo "통과 e2e (서버 + 봇 2명): $(cat build/logs/e2e_bot1.log)"
    exit 0
fi
echo "실패 e2e: 봇1=$r1 봇2=$r2 서버=$rs"
tail -n 5 build/logs/e2e_server.log build/logs/e2e_bot1.log build/logs/e2e_bot2.log
exit 1
