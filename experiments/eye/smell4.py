import collections, csv
from eyelib import *
from lesion import *
cls = np.load(f"{HERE}/cls.npy", allow_pickle=True); nt = np.load(f"{HERE}/nt.npy", allow_pickle=True)
head, row, pre, post, w = load_bin()
alln = np.where(cls=="ALLN")[0]
print("ALLN", len(alln), "예측 전달물질:", collections.Counter(nt[alln]).most_common())
conf = {}; known = {}; idx = {r:i for i,r in enumerate(ids)}
for r in csv.DictReader(open(f"{SRC}/annotations.tsv"), delimiter="\t"):
    i = idx.get(int(r["root_id"]))
    if i is not None: conf[i] = r["top_nt_conf"]; known[i] = r["known_nt"]
print("ALLN 알려진 전달물질(known_nt):", collections.Counter(known.get(i,"") for i in alln).most_common(6))
m = np.isin(pre, alln)
for k in ("acetylcholine","gaba","glutamate","serotonin","dopamine",""):
    mm = m & (nt[pre]==k)
    if mm.sum(): print(f"  {k or '없음':14s} 뉴런 {int((nt[alln]==k).sum()):3d}개 연결 {int(mm.sum()):6d} 시냅스합 {int(np.abs(w[mm]).sum()):7d} 흥분 비율 {(w[mm]>0).mean():.2f}")
# 유형별: 폭주에서 가장 세게 발화한 LN 유형의 전달물질
for t in ("lLN1_bc","lLN2X12","lLN2P_a","lLN2P_b","lLN2P_c","lLN2X03","lLN2F_a","lLN2F_b","lLN2T_a","v2LN30","il3LN6"):
    s = sel(t)
    if len(s): 
        mm = np.isin(pre, s)
        print(f"  {t:9s} {len(s):3d}개 예측 {collections.Counter(nt[s]).most_common(2)} known {collections.Counter(known.get(i,'') for i in s).most_common(1)} 출력 시냅스 {int(np.abs(w[mm]).sum())} 흥분 비율 {(w[mm]>0).mean():.2f}")
