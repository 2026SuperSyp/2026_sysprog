from eyelib import *
import re, collections
d=np.load(f"{HERE}/conn.npz"); pre,post,w=d["pre"],d["post"],d["w"]
colmask=np.array([bool(re.match(r"^(Tm|TmY|T[0-9]|Y[0-9])",t or "")) for t in typ])
def inputs_of(js):
    m=np.isin(post,js)&(w>0)&colmask[pre]; return np.unique(pre[m])
# (a) LC9 하나의 수용야: 여러 뉴런, 양쪽, 씨앗 2개
for sd in ("left","right"):
    lc9=sel("LC9",sd); rng=np.random.default_rng(3); pick=rng.permutation(lc9)[:12]
    rates=[]; esc=[]; walk=[]
    for j in pick:
        S=inputs_of([j])
        for seed in (7,11):
            r,_=run({i:200 for i in S},ms=1000,seed=seed)
            rates.append(r[j]); esc.append(r[sel("DNp01")].mean()); walk.append(r[sel("DNp09",sd)].mean())
    rates=np.array(rates)
    print(f"(a) {sd}: LC9 하나의 수용야 200Hz, 12개x씨앗2: 대상 LC9 평균 {rates.mean():.1f}Hz, 10Hz 넘은 비율 {100*(rates>10).mean():.0f}%, 걷기 평균 {np.mean(walk):.1f}, 도약 평균 {np.mean(esc):.1f}")
# (c) 걷기 명령 뉴런의 입력
dn=sel("DNp09","left"); m=np.isin(post,dn); cnt=collections.Counter()
for p,ww in zip(pre[m],w[m]): cnt[typ[p] or "?"]+=int(ww)
print("(c) DNp09(왼) 입력 상위:",cnt.most_common(12))
for label,stim in [("LC31a 왼 150Hz 직접",{i:150 for i in sel(["LC31a","LC31"],"left")}),
                   ("LC11 왼 150Hz 직접",{i:150 for i in sel("LC11","left")}),
                   ("LC9 왼 30개만 150Hz 직접",{i:150 for i in sel("LC9","left")[:30]}),
                   ("LC9 왼 10개만 150Hz 직접",{i:150 for i in sel("LC9","left")[:10]}),
                   ("LC9 왼 전부 40Hz 직접",{i:40 for i in sel("LC9","left")})]:
    r,info=run(stim,ms=1000)
    print(f"[{label}] 걷기 {r[sel('DNp09','left')].mean():.1f}/{r[sel('DNp09','right')].mean():.1f} 방향 {r[sel('DNa02','left')].mean():.1f}/{r[sel('DNa02','right')].mean():.1f} 도약 {r[sel('DNp01')].mean():.1f} | {info.split()[1]}")
print("LC31 계열 유형:", sorted(set(t for t in typ if t and t.startswith("LC31"))), len(sel(["LC31a","LC31"],"left")))
