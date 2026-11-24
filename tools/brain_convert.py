"""배선 파일을 C가 읽는 바이너리로 바꾼다. 한 번만 돌리면 된다.

    python3 tools/brain_convert.py <받은 파일 폴더> <결과 폴더> [--shuffle=씨앗]

받은 파일 폴더에 있어야 하는 것
    Connectivity_783.parquet   뉴런 사이의 연결 (FlyWire v783, Shiu 등 2024의 공개 자료)
    Completeness_783.csv       뉴런 목록. 줄 순서가 뉴런 번호다
    annotations.tsv            뉴런의 세포 유형 표 (flywire_annotations의 공개 자료)

만드는 것
    brain.bin          머리말 24바이트, 그 뒤로 줄 시작 위치(뉴런 수 + 1개, 4바이트씩),
                       받는 뉴런 번호(연결 수만큼, 4바이트씩), 세기(연결 수만큼, 2바이트씩)
    brain_groups.txt   게임이 쓰는 뉴런 집단. 한 줄에 "이름 개수 번호 번호 ..."
    brain_map.bin      뉴런의 위치(앞에서 본 모습). 뉴런마다 2바이트: 가로(0~255), 세로(0~254).
                       가로세로를 같은 비율로 줄였다. 위치를 모르는 뉴런은 세로가 255다
    shuffled/          --shuffle을 주면 배선을 섞은 대조용 뇌도 만든다(위 세 파일이 다 들어간다).
                       섞는 방법과 그대로 남는 통계는 tools/brain_shuffle.py에 있다
"""
import csv
import struct
import sys

import numpy as np
import pyarrow.parquet as pq

# 집단마다 (세포 유형 표의 어느 칸, 값들, 쪽). 쪽이 None이면 좌우를 모두 넣는다.
GROUPS = {
    "BR_LOOM_L":   ("type", {"LC4", "LPLC2"}, "left"),
    "BR_LOOM_R":   ("type", {"LC4", "LPLC2"}, "right"),
    "BR_SUGAR":    ("sub",  {"sugar"}, None),
    "BR_BITTER":   ("sub",  {"bitter"}, None),
    # 표에서 짠맛이라고 적힌 집단(LB3d, LB3b)은 이 모델에서 먹기를 늘린다. 먹기를 줄이는 것은
    # 'glutamate'라고 적힌 LB1e다. 초당 90번 이상 자극하면 뇌 전체가 폭주하므로 세게 넣지 않는다.
    "BR_SALT":     ("sub",  {"glutamate"}, None),
    "BR_WATER":    ("sub",  {"water"}, None),
    "BR_ESCAPE":   ("type", {"DNp01"}, None),
    "BR_TURN_L":   ("type", {"DNa02", "DNa01"}, "left"),
    "BR_TURN_R":   ("type", {"DNa02", "DNa01"}, "right"),
    "BR_BACKWARD": ("type", {"MDN"}, None),
    "BR_FEED":     ("type", {"CB0701"}, None),      # 주둥이를 내미는 운동 뉴런(MN9)
    # 몸을 움직이는 명령 뉴런(보스전). 근거와 잰 값은 docs/brain-notes.md
    "BR_OBJECT_L": ("type", {"LC9"}, "left"),       # 물체를 보는 시각 뉴런. DNp09에 가장 많이 들어온다
    "BR_OBJECT_R": ("type", {"LC9"}, "right"),
    "BR_WALK_L":   ("type", {"DNp09"}, "left"),     # 앞으로 걷기 명령 뉴런(P9, Bidaye 외 2020)
    "BR_WALK_R":   ("type", {"DNp09"}, "right"),
    "BR_STEER_L":  ("type", {"DNa02"}, "left"),     # 방향 전환 명령 뉴런(Rayshubskiy 외 2024)
    "BR_STEER_R":  ("type", {"DNa02"}, "right"),
}


def main(src, dst):
    ids = [int(line.split(",")[0]) for line in open(f"{src}/Completeness_783.csv").read().split("\n")[1:] if line]
    index_of = {root: i for i, root in enumerate(ids)}
    n = len(ids)

    t = pq.read_table(f"{src}/Connectivity_783.parquet",
                      columns=["Presynaptic_ID", "Presynaptic_Index", "Postsynaptic_Index", "Excitatory x Connectivity"])
    pre_id = t["Presynaptic_ID"].to_numpy()
    pre = t["Presynaptic_Index"].to_numpy()
    post = t["Postsynaptic_Index"].to_numpy()
    w = t["Excitatory x Connectivity"].to_numpy()

    # 뉴런 번호가 목록의 줄 순서와 같은지, 보내는 뉴런 순서로 정렬돼 있는지, 세기가 2바이트에 들어가는지 확인한다.
    assert all(index_of[int(pre_id[k])] == int(pre[k]) for k in range(0, len(pre), 9973))
    assert (np.diff(pre) >= 0).all()
    assert pre.max() < n and post.max() < n
    assert -32768 <= w.min() and w.max() <= 32767

    row = np.zeros(n + 1, dtype=np.uint32)
    row[1:] = np.cumsum(np.bincount(pre, minlength=n))
    with open(f"{dst}/brain.bin", "wb") as f:
        f.write(struct.pack("<8sIIII", b"SSPBRAIN", 1, n, len(pre), 0))
        row.tofile(f)
        post.astype(np.uint32).tofile(f)
        w.astype(np.int16).tofile(f)

    rows = list(csv.DictReader(open(f"{src}/annotations.tsv"), delimiter="\t"))

    # 뉴런의 위치. 표의 x는 좌우(초파리의 왼쪽이 작은 값), y는 위아래(아래가 큰 값)다.
    xy = np.full((n, 2), np.nan)
    for r in rows:
        i = index_of.get(int(r["root_id"]))
        if i is not None and r["pos_x"] and r["pos_y"]:
            xy[i] = (float(r["pos_x"]), float(r["pos_y"]))
    known = ~np.isnan(xy[:, 0])
    x0, y0 = xy[known, 0].min(), xy[known, 1].min()
    scale = 255.0 / (xy[known, 0].max() - x0)
    pos = np.full((n, 2), 255, dtype=np.uint8)
    pos[known, 0] = np.clip((xy[known, 0] - x0) * scale, 0, 255).astype(np.uint8)
    pos[known, 1] = np.clip((xy[known, 1] - y0) * scale, 0, 254).astype(np.uint8)
    pos.tofile(f"{dst}/brain_map.bin")
    print(f"위치를 아는 뉴런 {int(known.sum())}개, 세로 최댓값 {int(pos[known, 1].max())}")

    with open(f"{dst}/brain_groups.txt", "w") as f:
        for name, (column, values, side) in GROUPS.items():
            members = []
            for r in rows:
                if column == "type":
                    hit = r["cell_type"] in values or r["hemibrain_type"] in values
                else:
                    hit = r["cell_class"] == "gustatory" and r["cell_sub_class"] in values
                if hit and (side is None or r["side"] == side) and int(r["root_id"]) in index_of:
                    members.append(index_of[int(r["root_id"])])
            members = sorted(set(members))
            f.write(" ".join([name, str(len(members))] + [str(m) for m in members]) + "\n")
            print(f"{name:12s} {len(members)}개")
    print(f"뉴런 {n}개, 연결 {len(pre)}개")


if __name__ == "__main__":
    paths = [a for a in sys.argv[1:] if not a.startswith("--")]
    options = [a for a in sys.argv[1:] if a.startswith("--")]
    if len(paths) != 2 or any(not a.startswith("--shuffle") for a in options):
        sys.exit(__doc__)
    main(paths[0], paths[1])
    if options:
        sys.dont_write_bytecode = True
        import brain_shuffle
        seed = int(options[0].partition("=")[2] or 1)
        if not brain_shuffle.make(paths[1], f"{paths[1]}/shuffled", seed):
            sys.exit(1)
