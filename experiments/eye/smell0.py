# 먹이의 맛과 냄새: 감각 뉴런을 한쪽만 켜고 걷기, 방향, 먹기 명령과 폭주 여부를 본다
import csv, collections
from eyelib import *
cls = np.array([""]*N, dtype=object); sub = np.array([""]*N, dtype=object)
idx = {r:i for i,r in enumerate(ids)}
for r in csv.DictReader(open(f"{SRC}/annotations.tsv"), delimiter="\t"):
    i = idx.get(int(r["root_id"]))
    if i is not None: cls[i] = r["cell_class"]; sub[i] = r["cell_sub_class"]
np.save(f"{HERE}/cls.npy", cls); np.save(f"{HERE}/sub.npy", sub)
olf = np.where(cls == "olfactory")[0]
print("냄새 감각 뉴런", len(olf), collections.Counter(side[olf]))
types = collections.Counter(typ[olf]); print(len(types), "유형:", types.most_common(60))
gus = np.where(cls == "gustatory")[0]
print("맛 감각 뉴런", len(gus), collections.Counter(zip(sub[gus], side[gus])))
