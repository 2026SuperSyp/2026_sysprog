from eyelib import *
import re
d=np.load(f"{HERE}/conn.npz"); pre,post,w=d["pre"],d["post"],d["w"]
lc9=sel("LC9","left")
colmask=np.array([bool(re.match(r"^(Tm|TmY|T[0-9]|Y[0-9])",t or "")) for t in typ])
def inputs_of(js,minsyn=1):
    m=np.isin(post,js)&(w>0)&colmask[pre]
    return np.unique(pre[m])
rng=np.random.default_rng(1)
order=rng.permutation(lc9)
print("LC9 하나의 기둥형 흥분 입력 뉴런 수(예):",[len(inputs_of([j])) for j in order[:5]])
for k in (1,3,10,30,87):
    js=order[:k]; S=inputs_of(js)
    for hz in (50,100,200):
        r,info=run({i:hz for i in S},ms=1000)
        tot=info.split()[1]; peak=info.split()[2]
        print(f"LC9 {k:2d}개의 수용야({len(S):5d}뉴런) {hz:3d}Hz: 대상 LC9 {r[js].mean():6.1f}Hz, 나머지 LC9 {r[np.setdiff1d(lc9,js)].mean() if k<87 else 0:5.1f}, 걷기 {r[sel('DNp09','left')].mean():5.1f}/{r[sel('DNp09','right')].mean():5.1f}, 방향 {r[sel('DNa02','left')].mean():5.1f}/{r[sel('DNa02','right')].mean():5.1f}, 도약 {r[sel('DNp01')].mean():5.1f}, LC4 {r[sel('LC4','left')].mean():5.1f} | {tot} {peak}")
