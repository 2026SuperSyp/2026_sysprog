# 폭주의 정체: 냄새 한 유형(DM1 왼쪽 20Hz)으로 폭주시킨 뒤 어떤 뉴런들이 발화하는지 본다
import collections, csv
from eyelib import *
cls = np.load(f"{HERE}/cls.npy", allow_pickle=True)
sup = np.array([""]*N, dtype=object); idx = {r:i for i,r in enumerate(ids)}
nt = np.array([""]*N, dtype=object)
for r in csv.DictReader(open(f"{SRC}/annotations.tsv"), delimiter="\t"):
    i = idx.get(int(r["root_id"]))
    if i is not None: sup[i] = r["super_class"]; nt[i] = r["top_nt"]
np.save(f"{HERE}/sup.npy", sup); np.save(f"{HERE}/nt.npy", nt)
s = np.where((typ=="ORN_DM1")&(side=="left"))[0]
r, info = run({i:20 for i in s}, ms=500); print(info)
tot = r.sum()
def top(keys, label, k=12):
    c = collections.Counter(); n = collections.Counter()
    for key, v in zip(keys, r):
        c[key] += v; n[key] += 1
    print(label, [(a, f"{100*b/tot:.1f}%", n[a], f"{b/n[a]:.0f}Hz") for a, b in c.most_common(k)])
top(sup, "큰 분류별:"); top(cls, "분류별:"); top(typ, "유형별:", 25); top(nt, "전달물질별:")
print("발화한 뉴런 수", int((r>0).sum()), "/", N, " 100Hz 넘는 뉴런", int((r>100).sum()))
