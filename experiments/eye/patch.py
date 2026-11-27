# "보이는 물체"를 눈의 칸 단위로 넣는다: 시야의 한 자리(칸 묶음)에 해당하는 시엽 뉴런만 켠다.
# 물체가 가까울수록 차지하는 칸이 많다. 칸 수에 따라 걷기, 방향, 도약이 어떻게 달라지는지 본다.
import csv, gzip, sys
from eyelib import *
TYPES = ["T2","T2a","T3","Tm1","Tm2","Tm3","Tm4","Tm9","Tm20","Tm21"]
idx_of = {r:i for i,r in enumerate(ids)}
cols = [r for r in csv.DictReader(gzip.open(f"{HERE}/column_assignment.csv.gz","rt")) if r["type"] in TYPES]
def patch(sd, cp, cq, rad):
    out = []
    for r in cols:
        if r["hemisphere"] != sd: continue
        dp, dq = int(r["p"]) - cp, int(r["q"]) - cq
        if (abs(dp) + abs(dq) + abs(dp + dq)) / 2 <= rad: out.append(idx_of[int(r["root_id"])])
    return out
g = lambda r,t,s=None: r[sel(t,s)].mean()
if __name__ == "__main__":
    hz = float(sys.argv[1]) if len(sys.argv) > 1 else 150
    print(f"칸 단위 물체, {hz:.0f}Hz, 1초. 넣는 유형 {TYPES}")
    for sd in ("left","right"):
        for (cp,cq) in ((0,0),(6,-3)):
            for rad in (0,1,2,3,4,6,9,13):
                S = patch(sd, cp, cq, rad)
                ncol = 1 + 3*rad*(rad+1)
                r, info = run({i:hz for i in S}, ms=1000)
                pk = int(info.split("peak_tick=")[1].split()[0])
                print(f"{sd:5s} 중심({cp:2d},{cq:2d}) 반지름 {rad:2d} (약 {ncol:3d}칸, {len(S):4d}뉴런): LC31a {g(r,'LC31a',sd):5.1f} LC9 {g(r,'LC9',sd):4.1f} LC11 {g(r,'LC11',sd):5.1f} LC4 {g(r,'LC4',sd):5.1f} | 걷기 왼/오 {g(r,'DNp09','left'):5.1f}/{g(r,'DNp09','right'):5.1f} 방향 {g(r,'DNa02','left'):5.1f}/{g(r,'DNa02','right'):5.1f} 도약 {g(r,'DNp01'):5.1f} | 최대틱 {pk}", flush=True)
