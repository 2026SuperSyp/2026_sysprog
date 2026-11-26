# 더듬이엽 국소 뉴런(ALLN)의 부호를 세포 유형 표의 전달물질에 맞춘 뇌에서 냄새를 다시 본다
import subprocess, csv, collections
from eyelib import *
from lesion import *
cls = np.load(f"{HERE}/cls.npy", allow_pickle=True); nt = np.load(f"{HERE}/nt.npy", allow_pickle=True)
known = np.array([""]*N, dtype=object); idx = {r:i for i,r in enumerate(ids)}
for r in csv.DictReader(open(f"{SRC}/annotations.tsv"), delimiter="\t"):
    i = idx.get(int(r["root_id"]))
    if i is not None: known[i] = r["known_nt"]
np.save(f"{HERE}/known.npy", known)
head, row, pre, post, w = load_bin()
def inhib(i):      # 표에 실험으로 알려진 값이 있으면 그것을, 없으면 예측값을 쓴다
    k = known[i].lower()
    if "gaba" in k or k.startswith("glutamate"): return True
    if "acetylcholine" in k and "negative" not in k: return False
    return nt[i] in ("gaba", "glutamate")
alln = np.where(cls=="ALLN")[0]; inh = np.array([i for i in alln if inhib(i)])
m = np.isin(pre, inh); flipped = int((m & (w>0)).sum())
w2 = w.copy(); w2[m] = -np.abs(w2[m]); d = save("lnfix", head, row, post, w2)
print(f"ALLN {len(alln)}개 중 억제성으로 본 것 {len(inh)}개. 부호를 바꾼 연결 {flipped}개 (전체 연결의 {100*flipped/len(w):.2f}%)")
def runx(d, stim, ms=1000, seed=7):
    p = f"{HERE}/stim_l.txt"; open(p,"w").write("".join(f"{int(i)} {h:.1f}\n" for i,h in stim.items()))
    o = subprocess.run([f"{HERE}/eyeprobe", d, p, str(ms), str(seed), f"{HERE}/cnt_l.bin"], capture_output=True, text=True).stdout
    return np.fromfile(f"{HERE}/cnt_l.bin", dtype=np.uint32)/(ms/1000), int(o.split("peak_tick=")[1].split()[0])
g = lambda r,t,s=None: r[sel(t,s)].mean()
olf_types = sorted(t for t in set(typ[cls=="olfactory"]) if t)
res = []
for t in olf_types:
    for hz in (60, 150):
        s = np.where((typ==t)&(side=="left"))[0]
        r, pk = runx(d, {i:hz for i in s}, ms=500)
        res.append((t, hz, pk, g(r,'DNp09','left'), g(r,'DNp09','right'), g(r,'DNa02','left'), g(r,'DNa02','right'), g(r,'DNp01'), g(r,'MDN'), g(r,'CB0701'), r[cls=='ALPN'].mean(), r[cls=='Kenyon_Cell'].mean(), r[cls=='MBON'].mean()))
        print(f"{t:10s} {hz:3d}Hz 최대틱 {pk:6d}{' 폭주' if pk>20000 else '     '} PN {res[-1][10]:5.1f} KC {res[-1][11]:5.1f} MBON {res[-1][12]:5.1f} | 걷기 {res[-1][3]:4.1f}/{res[-1][4]:4.1f} 방향 {res[-1][5]:4.1f}/{res[-1][6]:4.1f} 도약 {res[-1][7]:4.1f} 후진 {res[-1][8]:4.1f} 먹기 {res[-1][9]:4.1f}", flush=True)
import pickle; pickle.dump(res, open(f"{HERE}/smell5.pkl","wb"))
print("폭주 수:", sum(1 for x in res if x[2]>20000), "/", len(res))
