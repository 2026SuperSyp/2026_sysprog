# 팀원 안내

스테이지(미니게임)와 화면을 만들 때 필요한 것만 적었다. 자세한 약속은 `include/`의 헤더 주석과 `docs/skeleton-design.md` 6장에 있다.

## 준비

```
wsl --install -d Ubuntu-24.04          (윈도우에서 한 번만)
sudo apt install build-essential git   (우분투에서 한 번만)
git clone https://github.com/2026SuperSyp/2026_sysprog.git ~/2026_sysprog
cd ~/2026_sysprog
```

코드는 우분투의 홈 폴더(`~/`)에 둔다. 윈도우 쪽 폴더에 두면 느리고 경로 문제로 빌드가 깨질 수 있다.

## 터미널 크기

게임 화면은 가로 240칸, 세로 67줄 이상이 필요하다. 터미널을 최대화하고 `Ctrl`+`-`를 몇 번 눌러 글자를 줄인다. `stty size`를 쳤을 때 `67 240` 이상이면 된다. 모자라면 게임 대신 안내가 뜬다.

## 명령

| 명령 | 하는 일 |
|---|---|
| `make run STAGE=example` | 스테이지를 실행한다. 왼쪽이 초파리, 가운데가 나다. 오른쪽 위에는 무작위로 움직이는 다른 참가자 한 명이 보이고 `Tab`으로 바꾼다. 마지막 사람 다음에는 키보드를 치는 초파리가 나온다. 오른쪽 아래는 초파리 뇌 표시다. `q`로 나간다 |
| `make run STAGE=example SEED=7 PLAYERS=1 BRAIN=slow` | 같은 문제로 다시, 한 명만, 느린 가짜 뇌로 |
| `make bot STAGE=example` | 화면 없이 여러 판을 돌려 검사한다. "통과"가 나와야 한다 |
| `make screen SCREEN=lobby CASE=2` | 화면 하나를 예시 상태로 띄운다(`lobby`, `result`, `ranking`, `spectate`). 아무 키나 누르면 끝난다 |
| `make log STAGE=example` | 스테이지의 `printf` 출력과 경고를 본다 |
| `make termbench` | 이 터미널이 초당 20장을 따라오는지 잰다. 도는 동안 마우스를 움직이면 마우스 보고도 확인한다 |
| `make run STAGE=example BRAIN=real` | 진짜 초파리 뇌 배선으로 돌린다. 아래 "진짜 뇌"의 준비를 먼저 한다 |

## 여러 명이 같이 하기

서버 한 대에 클라이언트가 접속한다. 3명까지 참가자이고 그 뒤로 들어온 사람은 관전자다.

```
make server ORDER=example            (서버. ORDER에 스테이지 이름을 쉼표로 적는다)
make client HOST=서버주소 NAME=이름  (접속. 같은 컴퓨터면 HOST를 빼도 된다)
make demo-local                      (한 컴퓨터에서 서버, 봇 2명, 나 1명으로 한 판)
```

- 다른 컴퓨터에서 접속하려면 서버 컴퓨터에 설정이 필요하다. `docs/network.md`를 본다.
- 로비에서 스페이스로 준비하고, 모두 준비되면 방장(가장 먼저 들어온 사람)이 F를 눌러 시작한다.
- 흐름: 로비 -> 규칙 설명 3초 -> 3, 2, 1, GO -> 게임 20초 -> 판정 -> (4라운드 반복) -> 랭킹 -> 로비.
- 서버는 라운드마다 스테이지 실행 파일(`build/dev/stage_이름`)을 따로 띄운다. 스테이지 코드는 `make run`일 때와 같다.
- 스테이지의 `printf` 출력은 `build/logs/stage_이름.log`에 쌓인다.

## 진짜 뇌

가짜 뇌 대신 실제 초파리 뇌 배선(FlyWire v783, 뉴런 138,639개)으로 반응을 계산할 수 있다. 스테이지 코드는 그대로다.

```
make brain-data                      (한 번만. 약 135MB를 받아 data/ 에 준비한다)
make brainprobe                      (자극별 반응과 계산 시간을 표로 본다)
make run STAGE=example BRAIN=real    (진짜 뇌로 실행)
```

- 지금 진짜 뇌가 계산하는 것: 다가옴(`BR_LOOM_L/R`) → 도주, 방향 전환, 후진. 맛(`BR_SUGAR`, `BR_BITTER`, `BR_SALT`, `BR_WATER`) → 먹기.
- 냄새(`BR_ODOR_*`, `BR_REPEL_*`)와 착지는 진짜 뇌에 연결하지 않았다. 전진은 규칙으로 둔다.
- 짠맛은 가짜 뇌와 다르다. 진짜 배선에서는 짠맛 뉴런이 먹기를 줄이지 않고 오히려 늘렸다.
- 진짜 뇌로 돌리면 오른쪽 아래 칸이 뇌 지도로 바뀐다. 뉴런을 실제 위치에 놓은 그림이고, 발화가 많은 곳일수록 밝다. 노란 점은 게임이 읽는 출력 뉴런이 발화한 자리다. 아래 막대는 왼쪽부터 왼쪽 회전, 도주, 후진, 먹기, 오른쪽 회전이다.
- 스테이지를 만들 때는 가짜 뇌(`fast`, `slow`)로 충분하다. 올리기 전에 `BRAIN=real`로도 한 번 돌려 본다.

## 스테이지를 새로 만드는 순서

1. `stages/example` 폴더를 자기 스테이지 이름으로 복사한다. 예: `cp -r stages/example stages/swat`
2. `make run STAGE=swat`이 도는지 본다. 다른 파일은 고치지 않아도 된다.
3. `stages/swat/` 안의 C 파일, `config.ini`, `assets/`만 고친다.
4. 올리기 전에 `make bot STAGE=swat`이 통과하는지 본다.

## 꼭 지킬 것

- 자기 폴더 밖은 고치지 않는다. 헤더(`include/`)를 바꿔야 하면 서진석에게 말한다.
- 유저마다 상태를 따로 둔다. 초파리 상태도 따로 둔다. 예제의 `side_t`를 참고한다.
- 시간은 틱 수로 센다. 한 틱은 0.05초이고 한 판은 `ctx->n_ticks`번 돈다. 시계를 읽거나 `sleep`하지 않는다.
- 난수는 `stage_rng_*`만 쓴다. `rand()`, `time()`은 쓰지 않는다.
- `stage_draw`에서는 그리기만 한다. 상태를 바꾸지 않는다. 초파리 칸에는 모두에게 같은 그림을 그린다.
- `brain_*`는 `stage_tick` 안에서만 부른다. `brain_fired`는 반응하는 동안 계속 1이므로, 한 번만 반응하게 하려면 쿨다운을 둔다.
- 이펙트(`fx_*`)는 사건이 난 틱에 `stage_tick`이나 `stage_input`에서 부른다. `stage_draw`에서 부르지 않는다.
- 난이도 숫자는 코드에 쓰지 않고 `config.ini`에 둔다.
- 점수는 0 이상이고, 한 판을 잘하면 100 근처가 되게 맞춘다.

## 팀 명세서 v6에서 바뀐 이름

| v6 | 지금 |
|---|---|
| `KEY_LEFT`, `KEY_RIGHT`, `KEY_UP`, `KEY_DOWN` | `SK_LEFT`, `SK_RIGHT`, `SK_UP`, `SK_DOWN` |
| `KEY_A`(스페이스), `KEY_B`(F) | `SK_ACT`, `SK_SUB` |
| `SUGAR`, `LOOM_L`, `ESCAPE` 등 | `BR_SUGAR`, `BR_LOOM_L`, `BR_ESCAPE` 등 |
| `fx_shake(캔버스, ...)` | `fx_shake(칸 번호, ...)`. 초파리 칸은 `PANE_FLY` |
| `make screen NAME=lobby` | `make screen SCREEN=lobby` |

## 아직 안 되는 것

- 한글 글자: 글꼴을 넣기 전이라 한글은 빈 네모로 보인다. 지금은 영문 대문자와 숫자를 쓴다.
- 보스전은 아직 없다. 4라운드가 끝나면 바로 랭킹으로 간다.
- 랭킹은 아직 파일로 남기지 않는다.
