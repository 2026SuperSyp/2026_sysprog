#!/bin/sh
# 보스전 2페이즈. 초파리의 체력이 기준 이하가 되면 스테이지 stages/run(초파리 추격)으로 넘어가고, 그 판정으로
# 보스전이 끝나 랭킹까지 가는지 본다.
#   sh tests/phase2.sh [포트]      포트부터 3개를 쓴다(기본 4680~4682)
#   a. --phase2=1: 보스전이 시작되자마자 2페이즈로 넘어가고, 판정 뒤에 보스전이 끝난다
#   b. --phase2=0: 2페이즈 없이 전처럼 끝난다
#   c. 2페이즈 도중에 스테이지를 죽인다(kill -9): 1페이즈로 돌아가 보스전이 끝난다
PORT=${1:-4680}
B=${SSP_BUILD:-build/dev}
L=build/logs
mkdir -p $L
fail=0

wait_line() {   # wait_line <파일> <패턴> <초>
    _i=0
    while [ $_i -lt $(($3 * 20)) ]; do
        grep -q -- "$2" "$1" 2>/dev/null && return 0
        sleep 0.05
        _i=$((_i + 1))
    done
    return 1
}
run_case() {    # run_case <이름> <포트> <서버 옵션...> : 서버와 봇 2명으로 한 판. rs에 서버 종료값
    _n=$1; _p=$2; shift 2
    $B/server --port=$_p --fast --once --seed=1 --order=example --rounds=1 --ticks=60 --tick-ms=5 --boss=6 \
        --ranking=$L/phase2_ranking.txt "$@" > $L/phase2_${_n}_server.log 2>&1 &
    SV=$!
    wait_line $L/phase2_${_n}_server.log "기다립니다" 10 || { echo "실패 phase2 $_n: 서버가 뜨지 않음"; fail=$((fail + 1)); return 1; }
    $B/client --bot --port=$_p --name=P2A --wait=2 > $L/phase2_${_n}_a.log 2>&1 &
    $B/client --bot --port=$_p --name=P2B --wait=2 > $L/phase2_${_n}_b.log 2>&1 &
}
end_server() {  # 서버가 끝나기를 기다린다(40초). 안 끝나면 죽이고 99
    _i=0
    while kill -0 $SV 2>/dev/null && [ $_i -lt 800 ]; do sleep 0.05; _i=$((_i + 1)); done
    if kill -0 $SV 2>/dev/null; then kill -9 $SV; wait $SV 2>/dev/null; rs=99; else wait $SV; rs=$?; fi
}
check() {       # check <이름> <설명> <있어야 하는 줄> <없어야 하는 줄 또는 빈칸>
    why=""
    [ $rs -eq 0 ] || why="$why 서버 종료값=$rs"
    grep -q -E "AddressSanitizer|LeakSanitizer|runtime error" $L/phase2_$1_server.log && why="$why 서버 로그에 메모리 검사 오류"
    grep -q -- "$3" $L/phase2_$1_server.log || why="$why '$3' 줄이 없음"
    [ -z "$4" ] || ! grep -q -- "$4" $L/phase2_$1_server.log || why="$why '$4' 줄이 있음"
    grep -q "보스전 끝" $L/phase2_$1_server.log || why="$why 보스전이 끝나지 않음"
    if [ -z "$why" ]; then echo "통과 phase2 $1 $2"; else echo "실패 phase2 $1 $2:$why (로그: $L/phase2_$1_server.log)"; fail=$((fail + 1)); fi
}

run_case a $PORT --phase2=1 && { end_server; check a "(체력 기준 1: 바로 2페이즈)" "보스전 2페이즈 시작" ""; }
run_case b $((PORT + 1)) --phase2=0 && { end_server; check b "(2페이즈 없음)" "보스전 시작" "2페이즈"; }
if run_case c $((PORT + 2)) --phase2=1 --tick-ms=30; then
    if wait_line $L/phase2_c_server.log "보스전 2페이즈 시작" 20; then
        sleep 0.5
        pkill -9 -P $SV -f stage_run 2>/dev/null
    fi
    end_server
    check c "(2페이즈의 스테이지를 죽임)" "보스전을 이어 갑니다" ""
fi
[ "$fail" -eq 0 ]
