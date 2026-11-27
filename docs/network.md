# 여러 대로 접속하기

서버 한 대에 다른 컴퓨터의 클라이언트가 붙는 방법이다. 기본 포트는 4620이다.

아래 절차는 마이크로소프트의 WSL 네트워크 문서(2026년 6월 판)를 따른 것이다. 팀장 노트북의 구성(윈도우 11, WSL 2.7.13, 설정 파일 없음 = NAT 방식)까지는 확인했고, **노트북 두 대로 실제 접속해 보는 것은 아직 하지 않았다.** 처음 해 볼 때 이 문서대로 되는지 확인하고, 다른 점이 있으면 고친다.

## 먼저 알아 둘 것

- **같은 컴퓨터 안**에서는 아무 설정도 필요 없다. `make server`를 띄우고 다른 우분투 창에서 `make client`를 하면 된다.
- **접속하는 쪽(클라이언트)** 은 설정이 필요 없다. WSL에서 밖으로 나가는 접속은 그냥 된다.
- **서버를 띄우는 컴퓨터**만 설정이 필요하다. WSL은 기본적으로 윈도우 뒤에 숨어 있어서(NAT 방식) 다른 컴퓨터가 바로 들어오지 못한다. 아래 둘 중 하나를 한다.

## 서버 컴퓨터: 방법 1 (권장) 미러 방식

윈도우 11 22H2 이상에서 된다. 한 번 해 두면 다시 할 필요가 없다.

1. 윈도우의 사용자 폴더에 `.wslconfig` 파일을 만든다(`C:\Users\이름\.wslconfig`).

   ```
   [wsl2]
   networkingMode=mirrored
   ```

2. 파워셸에서 WSL을 껐다 켠다.

   ```
   wsl --shutdown
   ```

3. **관리자 권한** 파워셸에서 들어오는 접속을 허용한다(포트 4620만). 첫 줄은 게임 접속(TCP)이고, 둘째 줄은 서버 찾기(UDP, 아래 "서버를 스스로 찾기")다.

   ```
   New-NetFirewallHyperVRule -Name "SuperSypParty" -DisplayName "Super Syp Party" -Direction Inbound -VMCreatorId '{40E0AC32-46A5-438A-A0B2-2B479E8F2E90}' -Protocol TCP -LocalPorts 4620
   New-NetFirewallHyperVRule -Name "SuperSypPartyFind" -DisplayName "Super Syp Party (find)" -Direction Inbound -VMCreatorId '{40E0AC32-46A5-438A-A0B2-2B479E8F2E90}' -Protocol UDP -LocalPorts 4620
   ```

4. 우분투에서 `make server`를 띄운다.

되돌리려면 `.wslconfig`의 두 줄을 지우고 `wsl --shutdown`을 한 뒤, 관리자 파워셸에서 `Remove-NetFirewallHyperVRule -Name "SuperSypParty"`와 `Remove-NetFirewallHyperVRule -Name "SuperSypPartyFind"`를 한다.

## 서버 컴퓨터: 방법 2 포트 전달

미러 방식을 쓸 수 없을 때 쓴다. WSL의 주소는 컴퓨터를 다시 켜면 바뀌므로 그때마다 1번을 다시 해야 한다. 이 방법은 TCP만 넘겨주므로 서버 찾기(`--host=auto`)는 되지 않는다. 접속하는 쪽에서 주소를 직접 적는다.

1. **관리자 권한** 파워셸에서:

   ```
   netsh interface portproxy add v4tov4 listenport=4620 listenaddress=0.0.0.0 connectport=4620 connectaddress=(wsl hostname -I)
   New-NetFirewallRule -DisplayName "Super Syp Party" -Direction Inbound -Protocol TCP -LocalPort 4620 -Action Allow
   ```

   `wsl hostname -I`가 주소를 여러 개 내놓으면 첫 번째 것을 직접 적는다.

2. 우분투에서 `make server`를 띄운다.

지우려면 `netsh interface portproxy delete v4tov4 listenport=4620 listenaddress=0.0.0.0`과 `Remove-NetFirewallRule -DisplayName "Super Syp Party"`를 한다.

## 접속하는 컴퓨터

1. 서버 컴퓨터의 주소를 알아낸다. 서버 컴퓨터의 윈도우 파워셸에서 `ipconfig`를 치고, 쓰고 있는 Wi-Fi의 "IPv4 주소"를 본다(예: 192.168.0.12).
2. 우분투에서:

   ```
   make client HOST=192.168.0.12 NAME=YEONWOO
   ```

## 서버를 스스로 찾기 (`--host=auto`)

주소를 적지 않고 `make client HOST=auto NAME=이름`(또는 `build/dev/client --host=auto`)으로 띄우면, 클라이언트가 같은 망에 "서버 있나요"를 UDP로 뿌리고(브로드캐스트, 포트는 게임과 같은 4620) 답한 서버에 붙는다. 1초 동안 답이 없으면 주소를 직접 적으라고 알려 준다.

| 경우 | 되는가 |
|---|---|
| 서버와 클라이언트가 같은 컴퓨터 | 된다(설정 없음) |
| 서로 다른 컴퓨터, 둘 다 미러 방식 | 서버 쪽에 위 3번의 UDP 줄까지 넣었으면 될 것으로 본다. **아직 해 보지 않았다** |
| 서버가 방법 2(포트 전달) | 안 된다. 포트 전달은 TCP만 넘긴다 |
| 클라이언트가 기본(NAT) 방식의 WSL | 안 될 수 있다. 뿌린 물음이 WSL 안쪽 망에만 퍼지고 Wi-Fi로 나가지 않는다 |

- 안 되면 주소를 직접 적는다(`HOST=192.168.0.12`). 게임 접속 자체는 찾기와 상관없이 된다.
- 학교나 카페의 Wi-Fi는 브로드캐스트도 막는 경우가 많다. 휴대폰 핫스팟에서는 대개 된다.

## 안 될 때

| 증상 | 볼 것 |
|---|---|
| "접속할 수 없습니다" | 서버가 떠 있는가. 주소가 서버 컴퓨터의 Wi-Fi 주소가 맞는가. 두 컴퓨터가 같은 Wi-Fi인가 |
| 같은 Wi-Fi인데도 안 됨 | 학교나 카페의 Wi-Fi는 기기끼리의 접속을 막는 경우가 많다. 휴대폰 핫스팟에 두 컴퓨터를 붙여서 다시 해 본다 |
| 서버 컴퓨터 안에서는 되는데 밖에서 안 됨 | 위 방법 1의 3번 또는 방법 2의 방화벽 줄을 했는가. 관리자 권한이었는가 |
| 방법 2에서 어제는 됐는데 오늘 안 됨 | WSL 주소가 바뀌었다. 포트 전달을 지우고 다시 건다 |
| `--host=auto`가 서버를 못 찾음 | 위 "서버를 스스로 찾기"의 표를 본다. 급하면 주소를 직접 적는다 |
| 현장에서 끝내 안 됨 | 노트북 한 대에서 `make demo-local`로 시연한다(서버, 봇 2명, 사람 1명) |

포트를 바꾸려면 서버와 클라이언트 양쪽에 `PORT=번호`를 주고, 위 명령의 4620도 같은 번호로 바꾼다.
