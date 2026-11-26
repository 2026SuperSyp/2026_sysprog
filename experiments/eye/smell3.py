import subprocess, sys
from eyelib import *
from lesion import *
cls = np.load(f"{HERE}/cls.npy", allow_pickle=True); sup = np.load(f"{HERE}/sup.npy", allow_pickle=True)
head, row, pre, post, w = load_bin()
sens = (sup == "sensory"); kc = (cls == "Kenyon_Cell"); alln = (cls == "ALLN")
print("감각 뉴런으로 들어가는 연결:", int(sens[post].sum()), "그중 흥분", int((sens[post]&(w>0)).sum()),
      "| KC->KC:", int((kc[pre]&kc[post]).sum()), "흥분", int((kc[pre]&kc[post]&(w>0)).sum()),
      "| ALLN 출력 중 흥분 비율: %.2f" % ((alln[pre]&(w>0)).sum()/max(alln[pre].sum(),1)))
variants = {"sens": sens[post], "kckc": kc[pre]&kc[post], "both": sens[post] | (kc[pre]&kc[post])}
def runx(d, stim, ms=500, seed=7):
    p = f"{HERE}/stim_l.txt"; open(p,"w").write("".join(f"{int(i)} {h:.1f}\n" for i,h in stim.items()))
    o = subprocess.run([f"{HERE}/eyeprobe", d, p, str(ms), str(seed), f"{HERE}/cnt_l.bin"], capture_output=True, text=True).stdout
    return np.fromfile(f"{HERE}/cnt_l.bin", dtype=np.uint32)/(ms/1000), int(o.split("peak_tick=")[1].split()[0])
g = lambda r,t,s=None: r[sel(t,s)].mean()
for name, mask in [("원래", None)] + list(variants.items()):
    if mask is None: d = f"{ROOT}/data"
    else:
        w2 = w.copy(); w2[mask] = 0; d = save(name, head, row, post, w2)
    for t in ("ORN_DM1", "ORN_DA1", "ORN_VA2"):
        for hz in (20, 60, 150):
            s = np.where((typ==t)&(side=="left"))[0]
            r, pk = runx(d, {i:hz for i in s})
            print(f"[{name:4s}] {t} 왼 {hz:3d}Hz: 최대틱 {pk:6d}{' 폭주' if pk>20000 else '     '} | PN(ALPN) {r[cls=='ALPN'].mean():5.1f} KC {r[kc].mean():5.1f} MBON {r[cls=='MBON'].mean():5.1f} | 걷기 {g(r,'DNp09','left'):4.1f}/{g(r,'DNp09','right'):4.1f} 방향 {g(r,'DNa02','left'):4.1f}/{g(r,'DNa02','right'):4.1f} 도약 {g(r,'DNp01'):4.1f} 먹기 {g(r,'CB0701'):4.1f}", flush=True)
