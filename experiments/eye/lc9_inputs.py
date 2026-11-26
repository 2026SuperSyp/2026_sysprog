import csv, sys, collections
import numpy as np, pyarrow.parquet as pq
src="data/raw"   # 저장소 맨 위에서 돌린다
ids=[int(l.split(",")[0]) for l in open(f"{src}/Completeness_783.csv").read().split("\n")[1:] if l]
idx={r:i for i,r in enumerate(ids)}
n=len(ids)
typ=[""]*n; side=[""]*n; cls=[""]*n
for r in csv.DictReader(open(f"{src}/annotations.tsv"),delimiter="\t"):
    i=idx.get(int(r["root_id"]))
    if i is None: continue
    typ[i]=r["cell_type"] or r["hemibrain_type"]; side[i]=r["side"]; cls[i]=r["super_class"]+"/"+r["cell_class"]
t=pq.read_table(f"{src}/Connectivity_783.parquet")
print(t.schema.names)
pre=t["Presynaptic_Index"].to_numpy(); post=t["Postsynaptic_Index"].to_numpy()
w=t["Excitatory x Connectivity"].to_numpy(); 
typ=np.array(typ); side=np.array(side)
FLYVIS=set("R1 R2 R3 R4 R5 R6 R7 R8 L1 L2 L3 L4 L5 Lawf1 Lawf2 Am C2 C3 CT1 Mi1 Mi2 Mi3 Mi4 Mi9 Mi10 Mi11 Mi12 Mi13 Mi14 Mi15 T1 T2 T2a T3 T4a T4b T4c T4d T5a T5b T5c T5d Tm1 Tm2 Tm3 Tm4 Tm5Y Tm5a Tm5b Tm5c Tm9 Tm16 Tm20 Tm28 Tm30 TmY3 TmY4 TmY5a TmY9 TmY10 TmY13 TmY14 TmY15 TmY18".split())
def report(target, sd="left", top=18):
    sel=(typ[post]==target)&(side[post]==sd)
    cnt=collections.Counter(); sign=collections.Counter()
    for p,ww in zip(pre[sel],w[sel]):
        cnt[typ[p] or "?"]+=abs(int(ww)); sign[typ[p] or "?"]+=int(ww)
    tot=sum(cnt.values()); cov=sum(v for k,v in cnt.items() if k in FLYVIS)
    nn=int(((typ==target)&(side==sd)).sum())
    print(f"\n== {target} {sd}: 뉴런 {nn}개, 들어오는 시냅스 {tot}, flyvis 유형이 차지하는 비율 {100*cov/max(tot,1):.1f}%")
    for k,v in cnt.most_common(top):
        print(f"  {k:10s} {v:7d} {100*v/tot:5.1f}%  {'흥분' if sign[k]>0 else '억제'}  {'*flyvis' if k in FLYVIS else ''}")
for tg in ("LC9","LPLC2","LC4","LPLC4"):
    report(tg)
# 유형별 뉴런 수(시엽)
for k in ("R1-6","R7","R8","L1","L2","Mi1","Tm1","Tm2","Tm3","Tm4","T2","T2a","T3","T4a","T5a","TmY5a","LC9"):
    print(k, int((typ==k).sum()), "L", int(((typ==k)&(side=="left")).sum()), "R", int(((typ==k)&(side=="right")).sum()))
