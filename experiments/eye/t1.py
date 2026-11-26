from eyelib import *
import pyarrow.parquet as pq, collections
t=pq.read_table(f"{SRC}/Connectivity_783.parquet",columns=["Presynaptic_Index","Postsynaptic_Index","Excitatory x Connectivity"])
pre=t["Presynaptic_Index"].to_numpy(); post=t["Postsynaptic_Index"].to_numpy(); w=t["Excitatory x Connectivity"].to_numpy()
np.savez(f"{HERE}/conn.npz",pre=pre,post=post,w=w)
lc9=set(sel("LC9","left")); m=np.isin(post,list(lc9))
cnt=collections.Counter()
for p,ww in zip(pre[m],w[m]): cnt[typ[p] or "?"]+=int(ww)
exc=[(k,v) for k,v in cnt.most_common() if v>0]
print("LC9(왼) 흥분 입력 상위:", [(k,v) for k,v in exc[:25]])
print("뉴런 하나당 흥분 시냅스 합:", sum(v for k,v in exc)/len(lc9), " 억제:", sum(v for k,v in cnt.items() if v<0)/len(lc9))
import re
vis=[k for k,v in exc if re.match(r"^(Tm|TmY|T[0-9]|Y|Mi|Dm|Pm|Li|LC|LT|LPLC|Tlp|MLt|cL|LLPt)",k) and k!="LC9"]
col=[k for k,v in exc if re.match(r"^(Tm|TmY|T[0-9]|Y[0-9])",k)]
print("기둥형 흥분 입력 유형:", col[:30])
for label,types,hz in [("기둥형 흥분 입력 전부 100Hz",col,100),("기둥형 흥분 입력 전부 200Hz",col,200),
                       ("Tm5f만 200Hz",["Tm5f"],200),("Tm5f,T2a,Y3 150Hz",["Tm5f","T2a","Y3"],150)]:
    r,info=run({i:hz for i in sel(types,"left")},ms=1000)
    print(f"\n[{label}] {info}\n  (왼/오) {table(r)}")
