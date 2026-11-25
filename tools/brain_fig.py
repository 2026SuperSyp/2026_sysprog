#!/usr/bin/env python3
"""뇌 그림: 뉴런의 위치에 점을 찍고, 자극을 넣은 뉴런, 그때 발화한 뉴런, 반응을 읽는 뉴런을 색칠한다.

    python3 tools/brain_fig.py <원본 폴더 data/raw> <뉴런 집단 data/brain_groups.txt> <발화 기록 폴더> <그림 폴더>

발화 기록은 build/.../brainprobe data --dump=폴더 가 만든다(조건마다 파일 하나: 첫 줄에 넣은 집단과 읽는 집단,
그 뒤로 "뉴런 번호 발화 수"). 뉴런의 위치는 세포 유형 표의 좌표를 앞에서 본 모습으로 쓴다.
그림은 남의 것을 가져오지 않고 이 자료로 직접 찍는다. make brain-figs 가 부른다.

색: 회색 = 모든 뉴런, 주황 = 그 자극에서 발화한 뉴런(많이 발화할수록 밝다), 하늘색 = 자극을 넣은 감각 뉴런,
    빨강 동그라미 = 반응을 읽는 뉴런
"""
import csv
import glob
import os
import struct
import sys
import zlib

import numpy as np

W, PAD = 440, 10


def write_png(path, rgb):
    h, w, _ = rgb.shape
    raw = b"".join(b"\x00" + rgb[y].tobytes() for y in range(h))

    def chunk(kind, data):
        body = kind + data
        return struct.pack(">I", len(data)) + body + struct.pack(">I", zlib.crc32(body) & 0xFFFFFFFF)

    with open(path, "wb") as f:
        f.write(b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", w, h, 8, 2, 0, 0, 0))
                + chunk(b"IDAT", zlib.compress(raw, 9)) + chunk(b"IEND", b""))


def main(raw_dir, groups_path, dump_dir, out_dir):
    ids = [int(line.split(",")[0]) for line in open(f"{raw_dir}/Completeness_783.csv").read().split("\n")[1:] if line]
    index_of = {root: i for i, root in enumerate(ids)}
    xy = np.full((len(ids), 2), np.nan, dtype=np.float64)
    for r in csv.DictReader(open(f"{raw_dir}/annotations.tsv", encoding="utf-8"), delimiter="\t"):
        i = index_of.get(int(r["root_id"]))
        if i is not None and r["pos_x"] and r["pos_y"]:
            xy[i] = (float(r["pos_x"]), float(r["pos_y"]))
    known = ~np.isnan(xy[:, 0])
    lo, hi = np.percentile(xy[known], 0.2, axis=0), np.percentile(xy[known], 99.8, axis=0)
    scale = (W - 2 * PAD) / (hi[0] - lo[0])
    H = int((hi[1] - lo[1]) * scale) + 2 * PAD
    px = np.clip(((xy - lo) * scale + PAD), 0, [W - 1, H - 1])
    px[~known] = -1
    px = px.astype(np.int32)

    groups = {}
    for line in open(groups_path):
        t = line.split()
        groups[t[0]] = np.array([int(v) for v in t[2:]], dtype=np.int64)

    # 바탕: 모든 뉴런. 점이 겹친 만큼 밝다
    dens = np.zeros((H, W))
    np.add.at(dens, (px[known, 1], px[known, 0]), 1.0)
    base = np.log1p(dens) / np.log1p(dens.max())
    bg = np.array([14.0, 17.0, 26.0])
    gray = np.array([96.0, 104.0, 120.0])

    def dots(img, idx, color, radius, ring=None):
        for i in idx:
            x, y = px[i]
            if x < 0:
                continue
            for dy in range(-radius - 1, radius + 2):
                for dx in range(-radius - 1, radius + 2):
                    d2 = dx * dx + dy * dy
                    yy, xx = y + dy, x + dx
                    if not (0 <= yy < H and 0 <= xx < W):
                        continue
                    if d2 <= radius * radius:
                        img[yy, xx] = color
                    elif ring is not None and d2 <= (radius + 1) * (radius + 1):
                        img[yy, xx] = ring

    os.makedirs(out_dir, exist_ok=True)
    for path in sorted(glob.glob(f"{dump_dir}/*.txt")):
        name = os.path.splitext(os.path.basename(path))[0]
        lines = open(path).read().split("\n")
        ins = lines[0].split()[1:]
        outs = lines[1].split()[1:]
        img = bg + base[:, :, None] * (gray - bg) * 0.9
        fired = np.zeros((H, W))
        n_fired = 0
        for line in lines[2:]:
            if not line:
                continue
            i, c = (int(v) for v in line.split())
            if px[i, 0] >= 0:
                fired[px[i, 1], px[i, 0]] += c
                n_fired += 1
        # 발화한 뉴런: 조금 번지게 해서(이웃 칸에도) 잘 보이게 한다
        glow = fired.copy()
        glow[1:, :] += fired[:-1, :] * 0.5
        glow[:-1, :] += fired[1:, :] * 0.5
        glow[:, 1:] += fired[:, :-1] * 0.5
        glow[:, :-1] += fired[:, 1:] * 0.5
        a = np.clip(np.log1p(glow) / np.log1p(max(glow.max(), 1.0)) * 1.3, 0.0, 1.0)[:, :, None]
        img = img * (1.0 - a) + np.array([255.0, 150.0, 40.0]) * a
        for g in ins:
            dots(img, groups.get(g, []), np.array([90.0, 200.0, 255.0]), 0)
        for g in outs:
            dots(img, groups.get(g, []), np.array([255.0, 50.0, 70.0]), 4, np.array([255.0, 255.0, 255.0]))
        write_png(f"{out_dir}/brain_{name}.png", np.clip(img, 0, 255).astype(np.uint8))
        print(f"brain_{name}.png: 넣은 집단 {' '.join(ins)}, 읽는 집단 {' '.join(outs)}, 발화한 뉴런 {n_fired}개")


if __name__ == "__main__":
    if len(sys.argv) != 5:
        sys.exit(__doc__)
    main(*sys.argv[1:])
