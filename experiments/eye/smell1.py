# 맛(단맛)과 냄새 유형 하나씩을 한쪽만 켜고 명령 뉴런과 폭주 여부를 본다
import collections
from eyelib import *
cls = np.load(f"{HERE}/cls.npy", allow_pickle=True); sub = np.load(f"{HERE}/sub.npy", allow_pickle=True)
def dn(r):
    g = lambda t, s=None: r[sel(t, s)].mean()
    return (f"걷기 {g('DNp09','left'):5.1f}/{g('DNp09','right'):5.1f} 방향 {g('DNa02','left'):5.1f}/{g('DNa02','right'):5.1f} "
            f"도약 {g('DNp01'):5.1f} 후진 {g('MDN'):5.1f} 먹기 {g('CB0701'):5.1f}")
def peak(info): return int(info.split("peak_tick=")[1].split()[0])
print("== 단맛 감각 뉴런 (120Hz, 1초)")
for label, m in [("왼쪽만", (cls=="gustatory")&(sub=="sugar")&(side=="left")), ("오른쪽만", (cls=="gustatory")&(sub=="sugar")&(side=="right")),
                 ("양쪽", (cls=="gustatory")&(sub=="sugar"))]:
    r, info = run({i:120 for i in np.where(m)[0]}, ms=1000)
    print(f"  {label:5s} {dn(r)} | 최대틱 {peak(info)}")
print("== 냄새 감각 뉴런: 유형 하나를 왼쪽만 (0.5초). 최대틱이 20000을 넘으면 폭주")
types = sorted(t for t in set(typ[cls=="olfactory"]) if t)
rows = []
for t in types:
    s = np.where((typ==t)&(side=="left")&(cls=="olfactory"))[0]
    for hz in (20, 60):
        r, info = run({i:hz for i in s}, ms=500)
        rows.append((t, len(s), hz, peak(info), r))
        print(f"  {t:10s} {len(s):3d}개 {hz:3d}Hz {dn(r)} | 최대틱 {peak(info)}{'  폭주' if peak(info)>20000 else ''}", flush=True)
import pickle; pickle.dump([(t,n,hz,p) for t,n,hz,p,_ in rows], open(f"{HERE}/smell1.pkl","wb"))
