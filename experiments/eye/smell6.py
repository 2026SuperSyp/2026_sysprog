import subprocess
from eyelib import *
cls = np.load(f"{HERE}/cls.npy", allow_pickle=True)
def runx(d, stim, ms=1000, seed=7):
    p = f"{HERE}/stim_l.txt"; open(p,"w").write("".join(f"{int(i)} {h:.1f}\n" for i,h in stim.items()))
    o = subprocess.run([f"{HERE}/eyeprobe", d, p, str(ms), str(seed), f"{HERE}/cnt_l.bin"], capture_output=True, text=True).stdout
    return np.fromfile(f"{HERE}/cnt_l.bin", dtype=np.uint32)/(ms/1000), int(o.split("peak_tick=")[1].split()[0])
g = lambda r,t,s=None: r[sel(t,s)].mean()
for name, d in [("원래", f"{ROOT}/data"), ("LN 부호 고침", f"{HERE}/data_lnfix")]:
    for t in ("ORN_DM1", "ORN_VA2", "ORN_DA1", "ORN_DL1"):
        for sd in ("left", "right"):
            for seed in (7, 11):
                s = np.where((typ==t)&(side==sd))[0]
                r, pk = runx(d, {i:60 for i in s}, seed=seed)
                pn = lambda q: r[(cls=='ALPN')&(side==q)].mean()
                print(f"[{name}] {t} {sd:5s} 씨앗{seed:2d}: 최대틱 {pk:6d} PN 왼/오 {pn('left'):5.1f}/{pn('right'):5.1f} | 방향 왼/오 {g(r,'DNa02','left'):5.1f}/{g(r,'DNa02','right'):5.1f} 걷기 {g(r,'DNp09','left'):4.1f}/{g(r,'DNp09','right'):4.1f} DNa01 {g(r,'DNa01','left'):5.1f}/{g(r,'DNa01','right'):5.1f}", flush=True)
