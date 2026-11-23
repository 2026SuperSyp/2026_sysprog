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
- 한 판이 끝나면 참가자의 누적 점수가 `data/ranking.txt`에 한 줄씩 쌓인다(`이름 점수 시각`). 역대 10위까지가 로비와 랭킹 화면에 나온다. 서버를 `--ranking=파일`로 띄우면 다른 파일을 쓴다. 파일의 꼴은 `core/ranking.h`에 적혀 있다.
- 서버는 초파리 뇌를 따로 뜬 프로세스(`build/dev/brain_proc`)에서 계산한다. 뇌가 죽거나 멈추면 서버가 다시 띄우고, 그동안 초파리는 직전 반응을 유지한다. 뇌의 로그는 `build/logs/brain.log`에 쌓인다. `make run`과 `make bot`은 전처럼 스테이지 안에서 계산한다.

## 진짜 뇌

가짜 뇌 대신 실제 초파리 뇌 배선(FlyWire v783, 뉴런 138,639개)으로 반응을 계산할 수 있다. 스테이지 코드는 그대로다.

```
make brain-data                      (한 번만. 약 135MB를 받아 data/ 에 준비한다)
make brainprobe                      (자극별 반응과 계산 시간을 표로 본다)
make run STAGE=example BRAIN=real    (진짜 뇌로 실행)
make brain-compare                   (대조 실험. 배선을 섞은 뇌를 만들어 진짜 배선과 반응을 나란히 본다)
```

- 지금 진짜 뇌가 계산하는 것: 다가옴(`BR_LOOM_L/R`) → 도주, 방향 전환, 후진. 맛(`BR_SUGAR`, `BR_BITTER`, `BR_SALT`, `BR_WATER`) → 먹기.
- 냄새(`BR_ODOR_*`, `BR_REPEL_*`)와 착지는 진짜 뇌에 연결하지 않았다. 전진은 규칙으로 둔다.
- 짠맛: 표에 짠맛이라고 적힌 뉴런은 진짜 배선에서 먹기를 오히려 늘렸다. 그래서 `BR_SALT`는 먹기를 줄이는 다른 맛 뉴런(LB1e)에 연결했다. 짠맛만 주면 먹지 않고, 단맛과 같이 주면 먹기가 줄어든다(단맛이 아주 세면 다 막지는 못한다). 확실히 막는 것은 쓴맛이다.
- 물은 먹기를 늘리지 않는다(조금 줄인다). 물로 먹게 만드는 규칙은 진짜 뇌에서 통하지 않는다.
- 이 뇌는 어떤 자극에서는 스스로 계속 발화하는 상태(폭주)에 빠진다. 냄새 뉴런이 그래서 연결되지 않았다. 폭주가 감지되면 뇌를 처음 상태로 되돌린다.
- 진짜 뇌로 돌리면 오른쪽 아래 칸이 뇌 지도로 바뀐다. 뉴런을 실제 위치에 놓은 그림이고, 발화가 많은 곳일수록 밝다. 노란 점은 게임이 읽는 출력 뉴런이 발화한 자리다. 아래 막대는 왼쪽부터 왼쪽 회전, 도주, 후진, 먹기, 오른쪽 회전이다.
- 스테이지를 만들 때는 가짜 뇌(`fast`, `slow`)로 충분하다. 올리기 전에 `BRAIN=real`로도 한 번 돌려 본다.
- 대조 실험: 섞은 뇌(`data/shuffled`)는 뉴런, 뉴런마다 보내고 받는 연결 수, 보내는 세기를 그대로 두고 받는 뉴런만 전체에서 섞은 것이다(`tools/brain_shuffle.py`). `BRAIN=shuffled`로 게임에도 붙일 수 있다.
- 섞은 배선에서는 같은 자극을 줘도 출력 뉴런이 발화하지 않아 초파리가 가만히 서 있다. 반응이 코드가 아니라 배선에서 나온다는 것까지는 이 비교로 말할 수 있다.
- 다만 전부 섞으면 감각 뉴런에서 출력 뉴런으로 가는 길이 통째로 끊긴다. 배선의 세부가 얼마나 중요한지는 이 대조로 알 수 없다.

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

## 한글 글자

`ui_text`에 한글을 그대로 쓰면 된다(소스 파일은 UTF-8). 스테이지의 제목, 규칙, 조작법도 한글로 적는다. 예제(`stages/example/example.c`)를 참고한다.

- 한글 한 글자는 12x12칸, 영문과 숫자는 6x8칸이다. 폭은 `ui_text_w`로 잰다.
- 한글이 섞인 글자열은 한 줄의 높이가 12픽셀이다. 그때 영문과 숫자는 3픽셀 내려서 한글의 가운데 높이에 맞춰 그린다. 영문만 있는 글자열은 예전과 같다.
- 한글은 12픽셀을 꽉 채운다. 여러 줄을 쓸 때는 줄 간격을 14픽셀로 잡는다. 12픽셀이면 위아래 줄이 맞닿는다.
- 그릴 수 있는 글자: 한글 음절 전부(11172자), 낱자모(ㄱ~ㅣ), 그리고 `· × ‘ ’ “ ” … ← ↑ → ↓ ★ ♥`. 이 부호들도 12x12 한 칸을 차지한다. 한자 같은 그 밖의 글자는 빈칸이 된다.
- 획이 많은 글자(뷁, 꿿 등)는 12픽셀에서 뭉개진다. 안내 문장에는 흔한 글자를 쓰고 `make run`으로 한 번 읽어 본다.
- 제목, 규칙, 조작법은 서버를 거치면서 각각 31, 95, 47바이트까지만 전달된다(한글은 한 글자가 3바이트). 넘치면 글자 단위로 잘린다. `stage.h`에 적힌 글자 수를 지키면 넘치지 않는다.
- 글꼴은 Freesentation(프리젠테이션체, SIL Open Font License 1.1)이다. 저작권 표시와 라이선스 전문은 `ui/assets/FONT_LICENSE.txt`에 있다.

글꼴 파일(`ui/assets/hangul12.fnt`)은 저장소에 들어 있어서 따로 준비할 것이 없다. 파일이 없으면 한글이 빈 네모로 보이고 로그에 경고가 남는다. 다시 만들 때만 아래를 돌린다(`data/venv`는 `make brain-data`가 만든다).

```
(https://freesentation.blog/freesentation 에서 ver 2.001을 받아 data/raw/font/ 에 푼다. 글꼴 파일은 저장소에 올리지 않는다)
data/venv/bin/python -m pip install freetype-py
data/venv/bin/python tools/font_convert.py data/raw/font/Freesentation-4Regular.ttf ui/assets/hangul12.fnt --preview build/font_preview.ppm
```

굵기, 크기, 그리는 방식을 바꿔 보려면 `tools/font_convert.py` 맨 위의 설명과 `--mode`, `--ppem` 옵션을 본다. `--preview`는 예문을 그린 그림(PPM)을 만든다.

## 팀 명세서 v6에서 바뀐 이름

| v6 | 지금 |
|---|---|
| `KEY_LEFT`, `KEY_RIGHT`, `KEY_UP`, `KEY_DOWN` | `SK_LEFT`, `SK_RIGHT`, `SK_UP`, `SK_DOWN` |
| `KEY_A`(스페이스), `KEY_B`(F) | `SK_ACT`, `SK_SUB` |
| `SUGAR`, `LOOM_L`, `ESCAPE` 등 | `BR_SUGAR`, `BR_LOOM_L`, `BR_ESCAPE` 등 |
| `fx_shake(캔버스, ...)` | `fx_shake(칸 번호, ...)`. 초파리 칸은 `PANE_FLY` |
| `make screen NAME=lobby` | `make screen SCREEN=lobby` |

## 아직 안 되는 것

- 한글 글자: 한글은 그려진다(위 "한글 글자"). 다만 12픽셀이라 획이 많은 글자는 뭉개지고, 영문 소문자와 괄호 같은 일부 기호는 아직 글꼴에 없다.
- 보스전은 아직 없다. 4라운드가 끝나면 바로 랭킹으로 간다.
