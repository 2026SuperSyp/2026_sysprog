# flyvis 활동을 진짜 뇌(발화 모델)의 같은 유형·같은 칸 뉴런에 넣고 반응을 잰다
import sys, csv, gzip, subprocess
from eyelib import *
OUT_TYPES = ["T2","T2a","T3","T4a","T4b","T4c","T4d","T5a","T5b","T5c","T5d","Tm1","Tm2","Tm3","Tm4","Tm9","Tm20"]
ALL_TYPES = OUT_TYPES + ["L1","L2","L3","L4","L5","C2","C3","Mi1","Mi4","Mi9","T1"]
WATCH = [("LC4",["LC4"]),("LPLC2",["LPLC2"]),("LC9",["LC9"]),("LC11",["LC11"]),("LC31a",["LC31a"]),("LPLC4",["LPLC4"]),
         ("DNp09",["DNp09"]),("DNa02",["DNa02"]),("DNp01",["DNp01"]),("DNp07",["DNp07"]),("MDN",["MDN"])]
idx_of = {r:i for i,r in enumerate(ids)}
cols = list(csv.DictReader(gzip.open(f"{HERE}/column_assignment.csv.gz","rt")))
def load(model="000"):
    return np.load(f"{HERE}/flyvis_act_{model}.npz")
def build(A, types, side, rmax):
    u, v = A["u"], A["v"]; hex_of = {(int(a),int(b)):k for k,(a,b) in enumerate(zip(u,v))}
    # 뉴런 -> (유형, 격자 칸)
    cells = {}
    for r in cols:
        if r["hemisphere"] != side or r["type"] not in types: continue
        k = hex_of.get((int(r["p"]), int(r["q"])))
        if k is not None: cells.setdefault(r["type"], []).append((idx_of[int(r["root_id"])], k))
    names = [k.split("|")[0] for k in A.files if "|" in k]; stim_names = sorted(set(names) - {"gray"})
    norm = {}
    for t in types:
        # 회색 화면 대비 탈분극만 쓴다. 유형마다 자극 묶음 전체의 99.5 백분위를 rmax로 맞춘다
        allv = np.concatenate([np.maximum(A[f"{s}|{t}"] - A[f"gray|{t}"], 0).ravel() for s in stim_names])
        norm[t] = max(np.percentile(allv, 99.5), 1e-6)
    return cells, norm, stim_names
def schedule(A, stim, cells, norm, rmax, path):
    n_ticks = 20; lines = []
    mean_rate = []
    for tk in range(n_ticks):
        rows = []
        for t, lst in cells.items():
            a = np.maximum(A[f"{stim}|{t}"][tk*5:(tk+1)*5] - A[f"gray|{t}"][tk*5:(tk+1)*5], 0).mean(0) if stim != "gray" else np.zeros(721)
            hz = np.clip(a / norm[t], 0, 1.5) * rmax
            for (i, k) in lst:
                if hz[k] >= 1.0: rows.append((i, hz[k]))
        mean_rate.append(len(rows))
        lines.append(f"T {len(rows)}\n" + "".join(f"{i} {h:.1f}\n" for i, h in rows))
    open(path, "w").write("".join(lines))
    return np.mean(mean_rate)
def watchfile(path):
    names = []
    with open(path, "w") as f:
        for nm, t in WATCH:
            for sd in ("left","right"):
                s = sel(t, sd); f.write(f"{nm}_{sd} {len(s)} " + " ".join(map(str, s)) + "\n"); names.append((f"{nm}_{sd}", len(s)))
    return names
def run_sched(path, wpath, seed=7):
    o = subprocess.run([f"{HERE}/eyeprobe2", f"{ROOT}/data", path, wpath, str(seed)], capture_output=True, text=True).stdout
    return np.array([[float(x) for x in ln.split()] for ln in o.strip().split("\n")])
if __name__ == "__main__":
    model = sys.argv[1]; side = sys.argv[2]; rmax = float(sys.argv[3]); which = sys.argv[4] if len(sys.argv) > 4 else "out"
    A = load(model); types = OUT_TYPES if which == "out" else ALL_TYPES
    cells, norm, stims = build(A, types, side, rmax)
    names = watchfile(f"{HERE}/watch.txt")
    print(f"모델 {model}, {side} 눈, 최대 {rmax:.0f}Hz, 넣는 유형 {len(types)}종, 대응된 뉴런 {sum(len(v) for v in cells.values())}개")
    hdr = ["자극","넣은뉴런","전체발화","최대틱","ms"] + [f"{nm}" for nm,_ in WATCH]
    print(" | ".join(hdr) + "   (집단은 왼/오, 뉴런 하나의 초당 발화 수)")
    for s in ["gray"] + stims:
        nst = schedule(A, s, cells, norm, rmax, f"{HERE}/sched.txt")
        M = run_sched(f"{HERE}/sched.txt", f"{HERE}/watch.txt")
        tot, peak, ms = M[:,0].sum(), M[:,0].max(), M[:,1].mean()
        g = M[:,2:].sum(0)        # 1초 동안의 집단 합
        cells_txt = []
        for k in range(len(WATCH)):
            l = g[2*k] / max(names[2*k][1],1); r = g[2*k+1] / max(names[2*k+1][1],1)
            cells_txt.append(f"{l:5.1f}/{r:5.1f}")
        print(f"{s:11s} | {nst:6.0f} | {tot:8.0f} | {peak:6.0f} | {ms:5.1f} | " + " | ".join(cells_txt))
