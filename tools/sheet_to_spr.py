"""구상안 그림(큰 PNG)에서 캐릭터를 잘라 게임용 그림 파일(.spr)로 바꾼다.

    python3 tools/sheet_to_spr.py <그림.png> <결과.spr> <칸 크기> <가로> <세로> <x,y> [<x,y> ...]

구상안은 점으로 그린 그림이라 그대로는 쓸 수 없다. 원본을 '칸 크기' 픽셀씩 묶어 한 픽셀로 줄이고,
그 칸 안에서 가장 많이 보이는 색을 고른다(배경은 버린다).
<가로> <세로>는 원본에서 한 장면이 차지하는 크기이고, <x,y>는 장면마다의 왼쪽 위 좌표다.
장면 하나가 프레임 하나가 된다. Pillow가 필요하다(pip install pillow).
"""
import sys

from PIL import Image

# 구상안의 색 -> (.spr 글자, 색 이름, 대표 RGB)
COLORS = [
    ("w", "C_WHITE",  (245, 245, 245)),
    ("o", "C_ORANGE", (240, 150, 40)),
    ("g", "C_GRAY3",  (150, 160, 180)),
    ("p", "C_PINK",   (245, 150, 185)),
    ("y", "C_YELLOW", (250, 215, 60)),
    ("m", "C_GOOD",   (150, 230, 190)),
]
MIN_LIGHT = 110        # 이보다 어두운 픽셀은 배경으로 본다
MIN_SHARE = 0.07       # 칸의 이만큼 이상을 차지해야 그 칸을 칠한다


def classify(rgb):
    r, g, b = rgb
    if max(r, g, b) < MIN_LIGHT:
        return None
    best, best_d = None, 1e9
    for i, (_, _, (cr, cg, cb)) in enumerate(COLORS):
        d = (r - cr) ** 2 + (g - cg) ** 2 + (b - cb) ** 2
        if d < best_d:
            best, best_d = i, d
    if COLORS[best][0] == "m" and g - r < 30:      # 초록빛이 뚜렷할 때만 민트로 본다. 아니면 회색이다
        best = 2
    return best


def tidy(lines):
    """혼자 떨어진 색 픽셀을 주변 색에 맞춘다(점 그림을 줄이다 생기는 얼룩)."""
    h, w = len(lines), len(lines[0])
    out = [list(line) for line in lines]
    for y in range(h):
        for x in range(w):
            c = lines[y][x]
            if c in ".w":
                continue
            around = [lines[y + dy][x + dx] for dy in (-1, 0, 1) for dx in (-1, 0, 1)
                      if (dx or dy) and 0 <= y + dy < h and 0 <= x + dx < w]
            if c in around:
                continue
            others = [a for a in around if a not in ".w"]
            if len(others) >= 3:
                out[y][x] = max(set(others), key=others.count)
    return ["".join(row) for row in out]


def convert(img, x0, y0, w, h, cell):
    px = img.load()
    cols, rows = int(w / cell), int(h / cell)
    out = []
    for ry in range(rows):
        line = ""
        for rx in range(cols):
            counts = [0] * len(COLORS)
            xa, xb = int(x0 + rx * cell), int(x0 + (rx + 1) * cell)
            ya, yb = int(y0 + ry * cell), int(y0 + (ry + 1) * cell)
            for y in range(ya, yb):
                for x in range(xa, xb):
                    if 0 <= x < img.width and 0 <= y < img.height:
                        k = classify(px[x, y][:3])
                        if k is not None:
                            counts[k] += 1
            area = max(1, (xb - xa) * (yb - ya))
            # 흰 테두리는 다른 색보다 가늘어서, 색이 있으면 색을 먼저 고른다
            colored = max(range(1, len(COLORS)), key=lambda k: counts[k])
            if counts[colored] / area >= MIN_SHARE:
                line += COLORS[colored][0]
            elif counts[0] / area >= MIN_SHARE:
                line += "w"
            else:
                line += "."
        out.append(line)
    return cols, rows, tidy(out)


def main(argv):
    if len(argv) < 7:
        sys.exit(__doc__)
    img = Image.open(argv[1]).convert("RGB")
    cell, w, h = float(argv[3]), int(argv[4]), int(argv[5])
    frames = []
    for spot in argv[6:]:
        x0, y0 = [int(v) for v in spot.split(",")]
        cols, rows, lines = convert(img, x0, y0, w, h, cell)
        frames.append(lines)
    with open(argv[2], "w", newline="\n") as f:
        f.write("; 진행자 시프봇. 구상안 그림에서 tools/sheet_to_spr.py로 옮겼다.\n")
        f.write(f"size {cols} {rows}\n")
        f.write(f"frames {len(frames)}\n")
        f.write("palette . C_NONE " + " ".join(f"{c} {name}" for c, name, _ in COLORS) + "\n")
        for lines in frames:
            for line in lines:
                f.write(line + "\n")
    print(f"{argv[2]}: {cols}x{rows}, {len(frames)}장")


if __name__ == "__main__":
    main(sys.argv)
