import csv, subprocess, os, collections, pickle
import numpy as np
import os as _os
HERE=_os.path.dirname(_os.path.abspath(__file__)); ROOT=_os.path.abspath(f"{HERE}/../.."); SRC=f"{ROOT}/data/raw"
_c=f"{HERE}/meta.pkl"
if os.path.exists(_c):
    typ,side,pos,ids=pickle.load(open(_c,"rb"))
else:
    ids=[int(l.split(",")[0]) for l in open(f"{SRC}/Completeness_783.csv").read().split("\n")[1:] if l]
    idx={r:i for i,r in enumerate(ids)}; n=len(ids)
    typ=np.array([""]*n,dtype=object); side=np.array([""]*n,dtype=object); pos=np.full((n,3),np.nan)
    for r in csv.DictReader(open(f"{SRC}/annotations.tsv"),delimiter="\t"):
        i=idx.get(int(r["root_id"]))
        if i is None: continue
        typ[i]=r["cell_type"] or r["hemibrain_type"]; side[i]=r["side"]
        if r["pos_x"]: pos[i]=(float(r["pos_x"]),float(r["pos_y"]),float(r["pos_z"]))
    pickle.dump((typ,side,pos,ids),open(_c,"wb"))
N=len(typ)
def sel(t,s=None):
    m=np.isin(typ,t if isinstance(t,(list,tuple,set)) else [t])
    if s: m&=(side==s)
    return np.where(m)[0]
def run(stim,ms=1000,seed=7,tag="x"):
    """stim: {뉴런번호: hz} 또는 (idx배열, hz배열). 뉴런별 초당 발화 수와 정보 문자열을 돌려준다."""
    p=f"{HERE}/stim_{tag}.txt"
    with open(p,"w") as f:
        items=stim.items() if isinstance(stim,dict) else zip(*stim)
        for i,h in items:
            if h>0: f.write(f"{int(i)} {float(h):.3f}\n")
    out=subprocess.run([f"{HERE}/eyeprobe",f"{ROOT}/data",p,str(ms),str(seed),f"{HERE}/cnt_{tag}.bin"],capture_output=True,text=True)
    c=np.fromfile(f"{HERE}/cnt_{tag}.bin",dtype=np.uint32)
    return c/(ms/1000.0), out.stdout.strip()+out.stderr.strip()
READ=[("LC9",["LC9"]),("LC4",["LC4"]),("LPLC2",["LPLC2"]),("LPLC4",["LPLC4"]),("LC11",["LC11"]),("걷기DNp09",["DNp09"]),("방향DNa02",["DNa02"]),("도약DNp01",["DNp01"]),("착지DNp07",["DNp07"])]
def table(rate):
    return "  ".join(f"{nm} {rate[sel(t,'left')].mean():5.1f}/{rate[sel(t,'right')].mean():5.1f}" for nm,t in READ)
