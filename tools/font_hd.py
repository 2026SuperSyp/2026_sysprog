"""큰 글꼴 변환기: TTF 글꼴을 가장자리가 부드러운 48픽셀 그림으로 바꿔 ui/assets/font48.fnt 를 만든다.

화면을 실제 픽셀로 그릴 때(Sixel, ui/hd.c) 쓰는 글꼴이다. 12x12 글꼴(tools/font_convert.py)을 키우면 계단이 보여서
같은 글꼴을 크게 다시 뽑았다. 글자가 차지하는 칸의 비율은 12x12 글꼴과 같다(한글 12x12 -> 48x48, 영문 6x8 -> 24x32).
그래서 화면 배치는 그대로 두고 글자 그림만 바꿔 그릴 수 있다.

쓰는 법 (저장소 맨 위 폴더에서)
    data/venv/bin/python -m pip install freetype-py          (한 번만)
    F=data/raw/font
    data/venv/bin/python tools/font_hd.py $F/Freesentation-5Medium.ttf ui/assets/font48.fnt --latin $F/Freesentation-7Bold.ttf
    data/venv/bin/python tools/font_hd.py 글꼴.ttf 결과.fnt --preview 미리보기.png

글꼴
    Freesentation(프리젠테이션체) 2.001. 한글은 5 Medium, 영문과 숫자는 7 Bold(작게 그려서 굵은 것을 쓴다).
    SIL Open Font License 1.1 (ui/assets/FONT_LICENSE.txt).
    글꼴 파일은 저장소에 올리지 않는다(data/raw/font/).

담는 글자
    영문 대문자, 숫자, 기호(0x21..0x7E에서 소문자를 뺀 것). 소문자는 그릴 때 대문자로 바꾼다(12x12 때와 같다).
    한글 음절 2350자(KS X 1001의 완성형), 호환 자모 51자, 그리고 tools/font_convert.py의 EXTRA와 같은 부호.
    여기 없는 글자는 그릴 때 12x12 글꼴을 키워서 쓴다.

그리는 방법
    FreeType으로 다듬지 않고(hinting 없이) 회색 그림을 그린다. 큰 글자는 다듬지 않는 쪽이 모양이 고르다.
    한글은 글자의 폭이 칸의 폭과 같아지는 크기로 그리고, 전체 음절의 위아래 범위를 칸 가운데에 놓는다.
    영문은 대문자 높이가 26픽셀이 되는 크기로 그려 칸 가운데에 놓는다. 칸보다 넓은 글자(M, W)는 가로로 줄인다.

파일 형식 (숫자는 리틀 엔디언)
    머리 16바이트
        0   "SSPH"
        4   버전(1)                     1바이트
        5   넓은 칸의 너비(48), 높이(48)  1바이트씩
        7   좁은 칸의 너비(24), 높이(32)  1바이트씩
        9   0                           3바이트
        12  글자 수 N                   4바이트
    글자표  N개 x (글자 코드 4바이트, 그림이 시작하는 자리 4바이트). 코드가 작은 것부터.
            코드가 128보다 작으면 좁은 칸, 아니면 넓은 칸이다. 자리는 그림 부분의 처음에서 센 바이트 수다.
    그림    글자마다 칸의 픽셀을 왼쪽 위부터 줄 단위로, 같은 값이 이어지는 길이로 줄여 담는다.
            한 바이트가 (이어지는 길이 - 1) x 16 + 진하기(0~15)다.
"""
import argparse
import struct
import sys
import zlib

import freetype

WIDE_W, WIDE_H = 48, 48
NARROW_W, NARROW_H = 24, 32
LATIN_CAP = 26                     # 영문 대문자의 높이(픽셀)
LATIN_BASE = 28                    # 영문의 밑줄이 놓이는 줄(칸의 위에서)
LATIN_DY = 3                       # 한글이 섞인 줄에서 영문을 내려 그리는 양(12x12 기준). ui/font.c와 같다

EXTRA = [0x00B7, 0x00D7, 0x2018, 0x2019, 0x201C, 0x201D, 0x2026,
         0x2190, 0x2191, 0x2192, 0x2193, 0x2605, 0x2665]
JAMO = list(range(0x3131, 0x3164))

SAMPLE = [
    "슈퍼 시프 파티  ROUND 3",
    "초파리는 다가가면 달아나요",
    "W S 앞뒤  A D 옆걸음  마우스 시선",
    "클릭/SPACE 무기 쓰기  C 살금살금",
    "1 파리채 2 스프레이 20 3 끈끈이 2",
    "SPACE: READY   F: START   Q: QUIT",
    "80/80  176  YOU 120  FLY 95%  +3 -2 ~!?",
]


def ksx1001():
    out = []
    for b1 in range(0xB0, 0xC9):
        for b2 in range(0xA1, 0xFF):
            try:
                out.append(ord(bytes([b1, b2]).decode("euc-kr")))
            except UnicodeDecodeError:
                pass
    return sorted(out)


def render(face, cp, ppem, xscale=1.0):
    """글자 하나를 그린다. (그림 줄들, 왼쪽 여백, 밑줄 위 높이, 폭)을 돌려준다. 글꼴에 없으면 None."""
    if face.get_char_index(cp) == 0:
        return None
    face.set_char_size(int(round(ppem * 64)))
    face.set_transform(freetype.Matrix(int(round(xscale * 0x10000)), 0, 0, 0x10000), freetype.Vector(0, 0))
    face.load_char(chr(cp), freetype.FT_LOAD_RENDER | freetype.FT_LOAD_NO_HINTING)
    g = face.glyph
    bm = g.bitmap
    rows = [bytes(bm.buffer[y * bm.pitch:y * bm.pitch + bm.width]) for y in range(bm.rows)]
    return rows, g.bitmap_left, g.bitmap_top, g.advance.x / 64.0


def place(cell_w, cell_h, rows, x0, y0):
    """그림을 칸의 (x0, y0)에 놓는다. 칸 밖으로 나가는 부분은 버린다. 0~255의 값 cell_w x cell_h개."""
    cell = bytearray(cell_w * cell_h)
    for dy, row in enumerate(rows):
        y = y0 + dy
        if y < 0 or y >= cell_h:
            continue
        for dx, v in enumerate(row):
            x = x0 + dx
            if 0 <= x < cell_w and v:
                cell[y * cell_w + x] = v
    return cell


def pack(cell):
    out = bytearray()
    prev, run = None, 0
    for v in cell:
        q = (v * 15 + 127) // 255
        if q == prev and run < 16:
            run += 1
        else:
            if prev is not None:
                out.append((run - 1) << 4 | prev)
            prev, run = q, 1
    out.append((run - 1) << 4 | prev)
    return bytes(out)


def build(path, latin_path=None):
    face = freetype.Face(path)
    cells = {}

    # 한글: 글자의 폭이 칸의 폭과 같아지는 크기. 음절 전체의 위아래 범위를 재서 칸 가운데에 놓는다
    probe = render(face, ord("가"), 100.0)
    ppem = 100.0 * WIDE_W / probe[3]
    wide = ksx1001() + JAMO
    drawn = {cp: render(face, cp, ppem) for cp in wide}
    drawn = {cp: g for cp, g in drawn.items() if g}
    hangul = [g for cp, g in drawn.items() if cp >= 0xAC00]
    asc = max(g[2] for g in hangul)
    desc = max(len(g[0]) - g[2] for g in hangul)
    if asc + desc > WIDE_H:                         # 칸보다 크면 들어가게 줄여서 다시 그린다
        ppem *= WIDE_H / (asc + desc)
        drawn = {cp: render(face, cp, ppem) for cp in drawn}
        hangul = [g for cp, g in drawn.items() if cp >= 0xAC00]
        asc = max(g[2] for g in hangul)
        desc = max(len(g[0]) - g[2] for g in hangul)
    base = (WIDE_H - (asc + desc)) // 2 + asc
    for cp, (rows, left, top, adv) in drawn.items():
        cells[cp] = place(WIDE_W, WIDE_H, rows, int(round((WIDE_W - adv) / 2.0)) + left, base - top)
    for cp in EXTRA:                                 # 부호는 칸 가운데에
        g = render(face, cp, ppem)
        if not g:
            continue
        rows, left, top, adv = g
        w = max((len(r) for r in rows), default=0)
        cells[cp] = place(WIDE_W, WIDE_H, rows, (WIDE_W - w) // 2, (WIDE_H - len(rows)) // 2)
    wide_ppem = ppem

    # 영문: 대문자 높이를 맞추고, 칸보다 넓은 글자는 가로로 줄인다. 한글보다 작게 그리므로 획이 가늘어 보인다.
    # 그래서 더 굵은 글꼴을 따로 줄 수 있다(--latin)
    if latin_path:
        face = freetype.Face(latin_path)
    h = render(face, ord("H"), 100.0)
    ppem = 100.0 * LATIN_CAP / h[2]
    squeezed = []
    for cp in range(0x21, 0x7F):
        if ord("a") <= cp <= ord("z"):
            continue
        g = render(face, cp, ppem)
        if not g:
            continue
        rows, left, top, adv = g
        w = max((len(r) for r in rows), default=0)
        if w > NARROW_W - 1:
            scale = (NARROW_W - 1.5) / w
            squeezed.append((chr(cp), round(scale, 2)))
            rows, left, top, adv = render(face, cp, ppem, scale)
            w = max((len(r) for r in rows), default=0)
        cells[cp] = place(NARROW_W, NARROW_H, rows, (NARROW_W - w) // 2, LATIN_BASE - top)
    face.set_transform(freetype.Matrix(0x10000, 0, 0, 0x10000), freetype.Vector(0, 0))
    info = "한글 %.1f픽셀(밑줄 %d번째 줄, 위 %d 아래 %d), 영문 %.1f픽셀, 가로로 줄인 글자 %s" % (
        wide_ppem, base, asc, desc, ppem, " ".join("%s:%s" % s for s in squeezed) or "없음")
    return cells, info


def write(cells, out_path):
    cps = sorted(cells)
    table, data = bytearray(), bytearray()
    for cp in cps:
        table += struct.pack("<II", cp, len(data))
        data += pack(cells[cp])
    head = b"SSPH" + bytes([1, WIDE_W, WIDE_H, NARROW_W, NARROW_H, 0, 0, 0]) + struct.pack("<I", len(cps))
    with open(out_path, "wb") as f:
        f.write(head + table + data)
    return len(head) + len(table) + len(data), len(cps)


def preview(cells, path, scale):
    """예문을 그려 PNG로 남긴다. 12x12 글꼴과 같은 배치 규칙으로 놓고 scale배(48픽셀 기준)로 줄이거나 키운다."""
    def text_w(s):
        return sum(NARROW_W if ord(ch) < 128 else WIDE_W for ch in s)

    line_h = WIDE_H + 12
    W = max(text_w(s) for s in SAMPLE) + 32
    H = line_h * len(SAMPLE) + 20
    img = bytearray(W * H)
    for i, s in enumerate(SAMPLE):
        x, y = 16, 10 + i * line_h
        mixed = any(ord(ch) >= 128 for ch in s)
        for ch in s:
            cp = ord(ch.upper()) if ch.isascii() else ord(ch)
            narrow = cp < 128
            cw, chh = (NARROW_W, NARROW_H) if narrow else (WIDE_W, WIDE_H)
            cell = cells.get(cp)
            gy = y + (LATIN_DY * WIDE_H // 12 if narrow and mixed else 0)
            if cell:
                for py in range(chh):
                    for px in range(cw):
                        v = cell[py * cw + px]
                        if v:
                            img[(gy + py) * W + x + px] = (v * 15 + 127) // 255 * 17
            x += cw
    if scale != 1.0:                                 # 가까운 네 점을 섞어 크기를 바꾼다
        W2, H2 = int(W * scale), int(H * scale)
        out = bytearray(W2 * H2)
        for yy in range(H2):
            sy = min((yy + 0.5) / scale - 0.5, H - 1.001)
            y0 = max(int(sy), 0)
            fy = max(sy - y0, 0.0)
            for xx in range(W2):
                sx = min((xx + 0.5) / scale - 0.5, W - 1.001)
                x0 = max(int(sx), 0)
                fx = max(sx - x0, 0.0)
                a = img[y0 * W + x0] * (1 - fx) + img[y0 * W + x0 + 1] * fx
                b = img[(y0 + 1) * W + x0] * (1 - fx) + img[(y0 + 1) * W + x0 + 1] * fx
                out[yy * W2 + xx] = int(a * (1 - fy) + b * fy)
        img, W, H = out, W2, H2
    raw = bytearray()
    for y in range(H):
        raw.append(0)
        for x in range(W):
            v = img[y * W + x]
            raw += bytes((18 + (235 - 18) * v // 255, 20 + (235 - 20) * v // 255, 32 + (225 - 32) * v // 255))

    def chunk(kind, body):
        return struct.pack(">I", len(body)) + kind + body + struct.pack(">I", zlib.crc32(kind + body) & 0xFFFFFFFF)

    with open(path, "wb") as f:
        f.write(b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", W, H, 8, 2, 0, 0, 0)) +
                chunk(b"IDAT", zlib.compress(bytes(raw), 6)) + chunk(b"IEND", b""))


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("ttf")
    ap.add_argument("out")
    ap.add_argument("--latin", help="영문, 숫자, 기호를 그릴 글꼴(없으면 같은 글꼴)")
    ap.add_argument("--preview")
    ap.add_argument("--preview-scale", type=float, default=1.0)
    a = ap.parse_args()
    cells, info = build(a.ttf, a.latin)
    size, n = write(cells, a.out)
    print("글자 %d개, %d바이트 (%s)" % (n, size, a.out))
    print(info)
    if a.preview:
        preview(cells, a.preview, a.preview_scale)
        print("미리보기 %s" % a.preview)
    return 0


if __name__ == "__main__":
    sys.exit(main())
