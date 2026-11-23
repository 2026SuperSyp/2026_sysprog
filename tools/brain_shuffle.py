"""배선을 섞은 대조용 뇌를 만든다. 진짜 배선과 나란히 돌려서 반응이 배선에서 나오는지 본다.

    python3 tools/brain_shuffle.py <진짜 뇌 폴더> <결과 폴더> [씨앗]        만들고 검사한다
    python3 tools/brain_shuffle.py --check <진짜 뇌 폴더> <섞은 뇌 폴더>     검사만 한다

섞는 방법
    모든 연결의 "받는 뉴런" 칸을 전체에서 한꺼번에 섞는다. 보내는 뉴런과 세기는 제자리에 둔다.
    그 뒤 자기 자신에게 가는 연결과 같은 쌍이 겹친 연결은 다른 연결과 받는 뉴런을 맞바꿔 없앤다.
    같은 씨앗이면 같은 파일이 나온다. 씨앗은 1 이상이고 머리말의 마지막 칸에 적어 둔다(진짜 배선은 0).

그대로인 것 (검사에서 하나라도 다르면 실패로 끝난다)
    뉴런 수와 번호. 그래서 brain_groups.txt와 brain_map.bin은 복사해서 그대로 쓴다
    연결 수, 뉴런마다 보내는 연결 수, 뉴런마다 받는 연결 수
    뉴런마다 보내는 세기들(부호 포함)과 그 합, 전체 세기 분포
달라지는 것
    누가 누구에게 보내는가, 뉴런마다 받는 세기의 합(흥분과 억제의 균형),
    서로 주고받는 쌍의 비율, 가까운 뉴런끼리 묶이는 구조
"""
import os
import shutil
import struct
import sys
import zlib

import numpy as np

HEADER = "<8sIIII"          # 표시, 판, 뉴런 수, 연결 수, 섞은 씨앗
HEADER_BYTES = 24


def read_brain(path):
    with open(path, "rb") as f:
        magic, version, n, n_syn, seed = struct.unpack(HEADER, f.read(HEADER_BYTES))
        assert magic == b"SSPBRAIN" and version == 1, f"{path}: 형식이 맞지 않습니다"
        row = np.fromfile(f, dtype=np.uint32, count=n + 1)
        post = np.fromfile(f, dtype=np.uint32, count=n_syn)
        w = np.fromfile(f, dtype=np.int16, count=n_syn)
    assert len(w) == n_syn and row[-1] == n_syn, f"{path}: 길이가 맞지 않습니다"
    return n, row, post, w, seed


def senders(n, row):
    """연결마다 보내는 뉴런 번호."""
    return np.repeat(np.arange(n, dtype=np.uint32), np.diff(row).astype(np.int64))


def _mix(z):
    z = (z ^ (z >> np.uint64(30))) * np.uint64(0xBF58476D1CE4E5B9)
    z = (z ^ (z >> np.uint64(27))) * np.uint64(0x94D049BB133111EB)
    return z ^ (z >> np.uint64(31))


def rand64(seed, stream, count):
    """씨앗과 갈래 번호로 64비트 난수 count개를 만든다.
    numpy 판이 달라도 같은 값이 나오도록 직접 계산한다. 식과 상수는 SplitMix64(Steele 등 2014)."""
    step = np.uint64(0x9E3779B97F4A7C15)
    with np.errstate(over="ignore"):
        start = _mix(np.array([seed], dtype=np.uint64) * step + np.uint64(stream))
        return _mix(start + (np.arange(count, dtype=np.uint64) + np.uint64(1)) * step)


def pair_keys(n, pre, post):
    return pre.astype(np.uint64) * np.uint64(n) + post.astype(np.uint64)


def bad_edges(n, pre, post):
    """자기 자신에게 가는 연결과, 같은 (보내는 뉴런, 받는 뉴런) 쌍의 두 번째부터를 찾는다."""
    key = pair_keys(n, pre, post)
    order = np.argsort(key, kind="stable")
    key = key[order]
    bad = pre == post
    bad[order[1:]] |= key[1:] == key[:-1]
    return np.flatnonzero(bad)


def shuffle_posts(n, row, post, seed):
    pre = senders(n, row)
    out = post[np.argsort(rand64(seed, 0, len(post)), kind="stable")]
    for turn in range(1, 500):
        bad = bad_edges(n, pre, out)
        if len(bad) == 0:
            return out
        # 고칠 연결마다 아무 연결이나 하나 골라 받는 뉴런을 맞바꾼다. 받는 연결 수는 변하지 않는다.
        other, first = np.unique(rand64(seed, turn, len(bad)) % np.uint64(len(out)), return_index=True)
        other, bad = other.astype(np.int64), bad[first]
        free = ~np.isin(other, bad)
        other, bad = other[free], bad[free]
        out[bad], out[other] = out[other].copy(), out[bad].copy()
    sys.exit("겹친 연결을 다 없애지 못했습니다")


def make(src, dst, seed):
    assert seed >= 1, "씨앗은 1 이상이어야 합니다"
    if os.path.realpath(src) == os.path.realpath(dst):
        sys.exit("결과 폴더가 원본 폴더와 같습니다. 진짜 배선을 덮어쓰게 되므로 멈춥니다.")
    n, row, post, w, _ = read_brain(f"{src}/brain.bin")
    out = shuffle_posts(n, row, post, seed)
    os.makedirs(dst, exist_ok=True)
    with open(f"{dst}/brain.bin", "wb") as f:
        f.write(struct.pack(HEADER, b"SSPBRAIN", 1, n, len(out), seed))
        row.tofile(f)
        out.tofile(f)
        w.tofile(f)
    for name in ("brain_groups.txt", "brain_map.bin"):
        shutil.copyfile(f"{src}/{name}", f"{dst}/{name}")
    print(f"섞은 배선: {dst}/brain.bin (씨앗 {seed})")
    return check(src, dst)


def histogram(deg):
    edges = [0, 1, 2, 10, 100, 1000, 10000, 1 << 31]
    return np.histogram(deg, bins=edges)[0]


def check(real_dir, shuf_dir):
    n, row, post, w, _ = read_brain(f"{real_dir}/brain.bin")
    n2, row2, post2, w2, seed = read_brain(f"{shuf_dir}/brain.bin")
    assert n == n2 and len(post) == len(post2), "뉴런 수나 연결 수가 다릅니다"
    pre, pre2 = senders(n, row), senders(n2, row2)
    wf, wf2 = w.astype(np.float64), w2.astype(np.float64)
    crc = zlib.crc32(post2.tobytes())
    print(f"뉴런 {n}개, 연결 {len(post)}개, 씨앗 {seed}, 받는 뉴런 칸의 CRC32 {crc:08x}")

    out_deg, out_deg2 = np.diff(row), np.diff(row2)
    in_deg, in_deg2 = np.bincount(post, minlength=n), np.bincount(post2, minlength=n)
    out_sum, out_sum2 = np.bincount(pre, wf, n), np.bincount(pre2, wf2, n)
    in_sum, in_sum2 = np.bincount(post, wf, n), np.bincount(post2, wf2, n)

    print("연결 수별 뉴런 수      0개     1개    2~9  10~99 100~999 1000~9999 10000~")
    for label, deg in (("보내는 연결, 진짜", out_deg), ("보내는 연결, 섞음", out_deg2),
                       ("받는 연결,   진짜", in_deg), ("받는 연결,   섞음", in_deg2)):
        print(f"  {label} " + " ".join(f"{c:7d}" for c in histogram(deg)))

    # 보내는 뉴런 안에서 세기순으로 줄 세워 비교한다. 줄 안의 순서가 달라도 같은 묶음이면 통과한다.
    by_sender = np.sort(pre.astype(np.int64) * 65536 + w + 32768)
    by_sender2 = np.sort(pre2.astype(np.int64) * 65536 + w2 + 32768)
    same = [
        ("뉴런마다 보내는 연결 수", np.array_equal(out_deg, out_deg2)),
        ("뉴런마다 받는 연결 수", np.array_equal(in_deg, in_deg2)),
        ("뉴런마다 보내는 세기들(부호 포함)", np.array_equal(by_sender, by_sender2)),
        ("뉴런마다 보내는 세기의 합", np.array_equal(out_sum, out_sum2)),
        ("전체 세기 분포", np.array_equal(np.sort(w), np.sort(w2))),
    ]
    print("그대로인 것")
    for label, ok in same:
        print(f"  {'같음' if ok else '다름'}  {label}")
    print(f"        보내는 세기의 합: 가장 큰 차이 {np.abs(out_sum - out_sum2).max():.0f}"
          f" (범위 {out_sum.min():.0f} ~ {out_sum.max():.0f})")

    # 쌍마다 번호 하나를 매겨 정렬해 두고(겹친 것은 뺀다) 두 목록에 같이 있는 개수를 센다.
    def pairs(a, b):
        key = np.sort(pair_keys(n, a, b))
        return key[np.concatenate(([True], key[1:] != key[:-1]))]

    key, key2 = pairs(pre, post), pairs(pre2, post2)
    back, back2 = pairs(post, pre), pairs(post2, pre2)

    def common(a, b):       # a의 쌍 가운데 b에도 있는 것의 비율
        return len(np.intersect1d(a, b, assume_unique=True)) / len(a)

    print("달라지는 것                              진짜       섞음")
    print(f"  받는 뉴런이 바뀐 연결의 비율                    {np.mean(post != post2):10.4f}")
    print(f"  진짜 배선에도 있는 쌍의 비율                    {common(key2, key):10.4f}")
    print(f"  서로 주고받는 연결의 비율          {common(back, key):10.4f} {common(back2, key2):10.4f}")
    print(f"  자기 자신에게 가는 연결            {int(np.sum(pre == post)):10d} {int(np.sum(pre2 == post2)):10d}")
    print(f"  같은 쌍이 겹친 연결                {len(post) - len(key):10d} {len(post2) - len(key2):10d}")
    print(f"  받는 세기의 합: 가장 작은 값       {in_sum.min():10.0f} {in_sum2.min():10.0f}")
    print(f"  받는 세기의 합: 가장 큰 값         {in_sum.max():10.0f} {in_sum2.max():10.0f}")
    print(f"  받는 세기의 합: 표준편차           {in_sum.std():10.1f} {in_sum2.std():10.1f}")
    print(f"  받는 세기의 합이 음수인 뉴런       {int(np.sum(in_sum < 0)):10d} {int(np.sum(in_sum2 < 0)):10d}")
    print(f"  받는 세기의 합: 진짜와 섞음의 상관계수          {np.corrcoef(in_sum, in_sum2)[0, 1]:10.4f}")

    ok = all(flag for _, flag in same)
    print("검사 통과" if ok else "검사 실패")
    return ok


if __name__ == "__main__":
    a = sys.argv[1:]
    if len(a) == 3 and a[0] == "--check":
        sys.exit(0 if check(a[1], a[2]) else 1)
    if len(a) not in (2, 3) or a[0].startswith("-"):
        sys.exit(__doc__)
    sys.exit(0 if make(a[0], a[1], int(a[2]) if len(a) == 3 else 1) else 1)
