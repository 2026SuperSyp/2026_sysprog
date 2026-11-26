# 배선의 일부를 끊은 뇌를 만든다(실험용). data_<이름>/brain.bin
import struct, os, shutil
from eyelib import *
def load_bin():
    b = open(f"{ROOT}/data/brain.bin","rb").read()
    magic, ver, n, m, z = struct.unpack("<8sIIII", b[:24])
    row = np.frombuffer(b, dtype=np.uint32, count=n+1, offset=24)
    post = np.frombuffer(b, dtype=np.uint32, count=m, offset=24+4*(n+1))
    w = np.frombuffer(b, dtype=np.int16, count=m, offset=24+4*(n+1)+4*m).copy()
    pre = np.repeat(np.arange(n, dtype=np.uint32), np.diff(row))
    return b[:24], row, pre, post, w
def save(name, head, row, post, w):
    d = f"{HERE}/data_{name}"; os.makedirs(d, exist_ok=True)
    with open(f"{d}/brain.bin","wb") as f:
        f.write(head); row.tofile(f); post.tofile(f); w.astype(np.int16).tofile(f)
    shutil.copy(f"{ROOT}/data/brain_groups.txt", d)
    return d
