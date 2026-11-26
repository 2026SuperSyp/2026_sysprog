from eyelib import *
import re, collections
d=np.load(f"{HERE}/conn.npz"); pre,post,w=d["pre"],d["post"],d["w"]
colmask=np.array([bool(re.match(r"^(Tm|TmY|T[0-9]|Y[0-9])",t or "")) for t in typ])
def inputs_of(js):
    m=np.isin(post,js)&(w>0)&colmask[pre]; return np.unique(pre[m])
lc=sel("LC31a","left"); print("LC31a 왼", len(lc))
m=np.isin(post,lc); cnt=collections.Counter()
for p,ww in zip(pre[m],w[m]): cnt[typ[p] or "?"]+=int(ww)
print("LC31a 입력 상위:",cnt.most_common(14))
rng=np.random.default_rng(5); order=rng.permutation(lc)
for k in (1,3,len(lc)):
    js=order[:k]; S=inputs_of(js)
    for hz in (50,100,200):
        r,info=run({i:hz for i in S},ms=1000)
        print(f"LC31a {k}개의 수용야({len(S)}뉴런) {hz}Hz: LC31a 대상 {r[js].mean():.1f} 전체 {r[lc].mean():.1f}, LC9 {r[sel('LC9','left')].mean():.1f}, 걷기 {r[sel('DNp09','left')].mean():.1f}/{r[sel('DNp09','right')].mean():.1f}, 방향 {r[sel('DNa02','left')].mean():.1f}/{r[sel('DNa02','right')].mean():.1f}, 도약 {r[sel('DNp01')].mean():.1f} | {info.split()[1]} {info.split()[2]}")
