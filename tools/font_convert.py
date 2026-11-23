"""한글 글꼴 변환기: TTF 글꼴을 12x12 한 가지 색 그림으로 바꿔 ui/assets/hangul12.fnt 를 만든다.

쓰는 법 (저장소 맨 위 폴더에서)
    data/venv/bin/python -m pip install freetype-py          (한 번만)
    data/venv/bin/python tools/font_convert.py data/raw/font/Freesentation-4Regular.ttf ui/assets/hangul12.fnt
    data/venv/bin/python tools/font_convert.py 글꼴.ttf 결과.fnt --preview 미리보기.ppm   (눈으로 확인할 그림도 만든다)

글꼴
    Freesentation(프리젠테이션체) 2.001의 4 Regular. SIL Open Font License 1.1.
    받는 곳: https://freesentation.blog/freesentation
    글꼴 파일은 저장소에 올리지 않는다(data/raw/font/). 바꾼 결과와 라이선스 전문(ui/assets/FONT_LICENSE.txt)만 올린다.

담는 글자
    한글 음절 U+AC00..U+D7A3 (11172자), 호환 자모 U+3131..U+3163 (51자),
    그리고 EXTRA에 적은 문장 부호와 화살표(5x7 글꼴에 없는 것).

바꾸는 방법 (기본값인 mono 방식)
    1. FreeType의 자동 다듬기로 획을 픽셀 격자에 맞춘 뒤 PPEM 픽셀 크기의 한 가지 색 그림으로 그린다.
    2. 모든 글자를 같은 자리(가로 여백, 밑줄 높이)에 놓는다. 자리는 칸의 오른쪽 끝 열을 가장 덜 쓰는 곳으로
       고르고, 그래도 칸 밖으로 나가는 글자만 한 칸씩 민다.
    3. 다듬다가 다른 글자와 그림이 똑같아진 음절(예: 몸과 몹)은 fit 방식으로 다시 그린다.
       fit 방식은 다듬지 않은 윤곽을 4분의 1 픽셀씩 밀어 가며 그려 보고 실제 모양과 가장 가까운 그림을 고른다.
    부호는 칸 가운데에 놓고, 칸보다 크면 들어갈 때까지 줄여서 다시 그린다.
    굵기(4 Regular), 크기(14픽셀), 방식(mono)은 예문을 그려 눈으로 비교해서 골랐다.
      - 3 Light는 획이 빠지고, 6 SemiBold부터는 획 굵기가 들쭉날쭉하다.
      - 13.5픽셀 이하는 글자가 11줄이라 줄 사이가 비지만 받침 있는 글자(를, 탕, 잡)가 뭉개진다.
        14픽셀은 12줄을 다 쓴다. 15픽셀은 칸을 넘는다.
      - 다듬지 않는 방식(gray, monoraw)은 획이 사라지거나 두 줄로 번진다.
    --mode, --ppem, --threshold로 다른 값을 시험할 수 있다.
    결과는 FreeType 판에 따라 몇 글자가 달라질 수 있다(올린 파일은 freetype-py 2.5.1, FreeType 2.13.2로 만들었다).

파일 형식 (숫자는 리틀 엔디언)
    머리 16바이트
        0   "SSPF"
        4   버전(1)                     1바이트
        5   칸 너비(12)                 1바이트
        6   칸 높이(12)                 1바이트
        7   구간 수 R                   1바이트
        8   글자 수 N                   4바이트
        12  0                           4바이트
    구간표  R개 x (첫 글자 코드 4바이트, 글자 수 4바이트). 코드가 작은 것부터, 겹치지 않는다.
    그림    N개 x 18바이트. 구간표 순서대로 이어진다.
            글자 하나는 12x12 = 144비트다. 왼쪽 위 픽셀부터 줄 단위로 담고, 바이트 안에서는 높은 비트가 먼저다.
            (y줄 x칸의 픽셀은 i = y * 12 + x 일 때 i // 8번째 바이트의 0x80 >> (i % 8) 비트)
"""
import argparse
import collections
import re
import struct
import sys

import freetype

CELL_W = 12
CELL_H = 12
GLYPH_BYTES = (CELL_W * CELL_H + 7) // 8

# 고른 값
MODE = "mono"
PPEM = 14.0
THRESHOLD = 0.4                    # 회색 그림을 자르는 문턱값. gray, auto, light, fit 방식에서 쓴다
LATIN_DY = 3                       # 한글이 섞인 줄에서 영문을 내려 그리는 양. ui/font.c와 같아야 한다(미리보기용)

HANGUL = [(0xAC00, 11172), (0x3131, 51)]
# 한글 문장에 쓰는 부호 가운데 5x7 글꼴에 없는 것. 한 글자가 12x12 한 칸을 차지한다.
#   가운뎃점, 곱셈표, 작은따옴표 둘, 큰따옴표 둘, 줄임표, 화살표 넷(왼쪽 위 오른쪽 아래), 별, 하트
EXTRA = [0x00B7, 0x00D7, 0x2018, 0x2019, 0x201C, 0x201D, 0x2026,
         0x2190, 0x2191, 0x2192, 0x2193, 0x2605, 0x2665]

SAMPLE = [
    "돌 피하기  ROUND 3",
    "떨어지는 돌을 피하세요",
    "초파리보다 오래 버티세요",
    "준비  초파리 승리  남은 기회 2",
    "← → 또는 마우스  SPACE: 준비",
    "거미줄 미로 뷁 흙 값 없 읊 꿿 를 틀",
    "YOU 120점  FLY 80점  3등!",
]

OUTLINE_SMART_DROPOUTS = 0x10      # FreeType의 FT_OUTLINE_SMART_DROPOUTS
OUTLINE_INCLUDE_STUBS = 0x20       # FT_OUTLINE_INCLUDE_STUBS
SHIFTS = (0.0, 0.25, -0.25, 0.5)   # fit 방식에서 윤곽을 밀어 보는 양(픽셀)
ORPHAN_MIN = 0.3                   # 이만큼 덮였는데 꺼진 픽셀은 사라진 획일 수 있다
ORPHAN_WEIGHT = 3.0
CLIP_WEIGHT = 5.0
SHIFT_WEIGHT = 0.05                # 비슷하면 덜 민 쪽을 고른다

LOAD_FLAGS = {
    "mono": freetype.FT_LOAD_FORCE_AUTOHINT | freetype.FT_LOAD_TARGET_MONO,   # 획을 픽셀에 맞추고 한 가지 색으로 그린다
    "monoraw": freetype.FT_LOAD_NO_HINTING | freetype.FT_LOAD_TARGET_MONO,    # 다듬지 않고 한 가지 색으로
    "gray": freetype.FT_LOAD_NO_HINTING | freetype.FT_LOAD_TARGET_NORMAL,     # 다듬지 않은 회색 그림을 문턱값으로 자른다
    "auto": freetype.FT_LOAD_FORCE_AUTOHINT | freetype.FT_LOAD_TARGET_NORMAL, # 획을 픽셀에 맞춘 회색 그림을 자른다
    "light": freetype.FT_LOAD_FORCE_AUTOHINT | freetype.FT_LOAD_TARGET_LIGHT, # 세로 방향만 맞춘 회색 그림을 자른다
    "fit": freetype.FT_LOAD_NO_HINTING | freetype.FT_LOAD_TARGET_NORMAL,      # 글자마다 실제 모양과 가장 가까운 그림을 고른다
}


def coverage(face, cp, mode, ppem, dx=0.0, dy=0.0):
    """글자 하나를 그려 {(x, y): 덮인 정도 0~1}을 돌려준다. 원점은 밑줄의 왼쪽 끝이고 y는 아래로 늘어난다.
    dx, dy는 그리기 전에 윤곽을 미는 양(픽셀)이다."""
    face.set_char_size(int(round(ppem * 64)), 0, 72, 72)
    face.set_transform(freetype.Matrix(0x10000, 0, 0, 0x10000),
                       freetype.Vector(int(round(dx * 64)), int(round(dy * 64))))
    flags = freetype.FT_LOAD_NO_BITMAP | LOAD_FLAGS[mode]
    if mode == "monoraw":
        # 가는 획이 픽셀 사이로 빠지지 않게 한다(획 끝의 짧은 돌기도 살린다).
        face.load_char(chr(cp), flags)
        try:
            face.glyph._FT_GlyphSlot.contents.outline.flags |= OUTLINE_SMART_DROPOUTS | OUTLINE_INCLUDE_STUBS
        except AttributeError:
            pass
        face.glyph.render(freetype.FT_RENDER_MODE_MONO)
    else:
        face.load_char(chr(cp), flags | freetype.FT_LOAD_RENDER)
    slot = face.glyph
    bm = slot.bitmap
    buf, pitch = bm.buffer, bm.pitch
    one_bit = bm.pixel_mode == freetype.FT_PIXEL_MODE_MONO
    out = {}
    for r in range(bm.rows):
        for c in range(bm.width):
            if one_bit:
                v = 1.0 if buf[r * pitch + (c >> 3)] & (0x80 >> (c & 7)) else 0.0
            else:
                v = buf[r * pitch + c] / 255.0
            if v > 0.0:
                out[(slot.bitmap_left + c, r - slot.bitmap_top)] = v
    return out


def ink(cov, threshold):
    return {p for p, v in cov.items() if v >= threshold}


def misfit(cov, on):
    """켠 픽셀들이 실제 모양(cov)과 얼마나 다른지. 작을수록 좋다.
    다른 넓이에, 켜진 이웃이 하나도 없이 버려진 잉크(획이 통째로 사라진 자리)를 더 크게 쳐서 더한다."""
    err = 0.0
    for p, v in cov.items():
        if p in on:
            err += 1.0 - v
        else:
            err += v
            if v >= ORPHAN_MIN:
                x, y = p
                if (x + 1, y) not in on and (x - 1, y) not in on and (x, y + 1) not in on and (x, y - 1) not in on:
                    err += ORPHAN_WEIGHT * v
    for p in on:
        if p not in cov:
            err += 1.0
    return err


def fitted(face, cp, ppem, threshold, inside):
    """fit 방식: 윤곽을 4분의 1 픽셀씩 밀어 가며 세 가지로 그려 보고(회색을 0.5와 문턱값으로 자른 것,
    처음부터 한 가지 색으로 그린 것) 실제 모양과 가장 가까운 그림을 고른다.
    inside(p)가 거짓인 픽셀은 칸 밖이다. 칸 밖으로 나가는 그림은 크게 깎는다."""
    best, best_score = set(), None
    for dy in SHIFTS:
        for dx in SHIFTS:
            cov = coverage(face, cp, "gray", ppem, dx, dy)
            mono = set(coverage(face, cp, "monoraw", ppem, dx, dy))
            for on in (ink(cov, 0.5), ink(cov, threshold), mono):
                score = (misfit(cov, on) + CLIP_WEIGHT * sum(1 for p in on if not inside(p))
                         + SHIFT_WEIGHT * (abs(dx) + abs(dy)))
                if best_score is None or score < best_score - 1e-9:
                    best, best_score = on, score
    return best


def nudge(lo, hi, size):
    """lo..hi를 0..size-1 안에 넣으려면 얼마나 밀어야 하는지. 더 크면 가운데에 놓는다(양쪽이 잘린다)."""
    if hi - lo + 1 > size:
        return (size - (hi - lo + 1)) // 2 - lo
    if lo < 0:
        return -lo
    if hi >= size:
        return size - 1 - hi
    return 0


def choose_offset(spans, size):
    """모든 글자에 같이 쓸 자리를 고른다. spans는 글자마다의 (가장 작은 좌표, 가장 큰 좌표)다.
    따로 밀어야 하는 글자가 적고, 칸의 마지막 줄(열)을 쓰는 글자가 적은 곳을 고른다.
    마지막 열을 비워야 옆 글자와 붙지 않는다."""
    best = None
    for off in range(-2, size + 3):
        cost = 0
        for lo, hi in spans:
            d = nudge(lo + off, hi + off, size)
            cost += (3 if d else 0) + (1 if hi + off + d >= size - 1 else 0)
        if best is None or cost < best[0]:
            best = (cost, off)
    return best[1]


def settle(g, xoff, base):
    """글자 하나를 칸에 놓는다. 칸 밖으로 나가면 그 글자만 들어올 때까지 민다. 그래도 넘치는 픽셀은 자른다."""
    if not g:
        return frozenset()
    xs = [x for x, _ in g]
    ys = [y for _, y in g]
    ox = xoff + nudge(min(xs) + xoff, max(xs) + xoff, CELL_W)
    oy = base + nudge(min(ys) + base, max(ys) + base, CELL_H)
    return frozenset((x + ox, y + oy) for (x, y) in g if 0 <= x + ox < CELL_W and 0 <= y + oy < CELL_H)


def twins(cells):
    """그림이 서로 똑같은 글자들의 코드 목록."""
    groups = collections.defaultdict(list)
    for cp, g in cells.items():
        groups[g].append(cp)
    return [cp for v in groups.values() if len(v) > 1 for cp in v]


def extra_cell(face, cp, mode, ppem, threshold, base):
    """부호 하나. 가로는 칸 가운데에 놓는다. 칸보다 크면 반 픽셀씩 줄여서 다시 그린다."""
    size = ppem
    while True:
        g = ink(coverage(face, cp, "gray" if mode == "fit" else mode, size), threshold)
        if not g:
            return frozenset()
        xs = [x for x, _ in g]
        ys = [y for _, y in g]
        w, h = max(xs) - min(xs) + 1, max(ys) - min(ys) + 1
        if (w <= CELL_W - 1 and h <= CELL_H) or size <= 6:
            break
        size -= 0.5
    return settle(g, (CELL_W - 1 - w) // 2 - min(xs), base)


def build(font_path, mode=MODE, ppem=PPEM, threshold=THRESHOLD, only=None, rescue=True):
    """{코드: 켜진 픽셀 집합}과 통계를 돌려준다. only를 주면 그 글자들만 만든다(시험용)."""
    face = freetype.Face(font_path)
    codes = [cp for first, n in HANGUL for cp in range(first, first + n)]
    if only is not None:
        codes = [cp for cp in codes if cp in only]
    codes = [cp for cp in codes if face.get_char_index(cp) != 0]

    first_mode, first_cut = ("gray", 0.5) if mode == "fit" else (mode, threshold)
    raw = {cp: ink(coverage(face, cp, first_mode, ppem), first_cut) for cp in codes}
    inked = [g for g in raw.values() if g]
    xoff = choose_offset([(min(x for x, _ in g), max(x for x, _ in g)) for g in inked], CELL_W)
    base = choose_offset([(min(y for _, y in g), max(y for _, y in g)) for g in inked], CELL_H)

    def inside(p):
        return 0 <= p[0] + xoff < CELL_W and 0 <= p[1] + base < CELL_H

    if mode == "fit":
        raw = {cp: fitted(face, cp, ppem, threshold, inside) for cp in codes}
    cells = {cp: settle(g, xoff, base) for cp, g in raw.items()}

    same_before = twins(cells)
    if rescue and mode != "fit":
        for cp in same_before:
            raw[cp] = fitted(face, cp, ppem, threshold, inside)
            cells[cp] = settle(raw[cp], xoff, base)

    stats = {
        "xoff": xoff, "base": base,
        "clipped": sum(len(raw[cp]) - len(cells[cp]) for cp in cells),
        "moved": sum(1 for g in raw.values() if g and not all(inside(p) for p in g)),
        "same_before": len(same_before), "same": len(twins(cells)),
        "last_col": sum(1 for g in cells.values() if any(x == CELL_W - 1 for x, _ in g)),
        "last_row": sum(1 for g in cells.values() if any(y == CELL_H - 1 for _, y in g)),
        "pixels": sum(len(g) for g in cells.values()) / max(1, len(cells)),
    }
    for cp in EXTRA:
        if (only is None or cp in only) and face.get_char_index(cp) != 0:
            cells[cp] = extra_cell(face, cp, mode, ppem, threshold, base)
    stats["empty"] = sum(1 for g in cells.values() if not g)
    return cells, stats


def pack(cells):
    codes = sorted(cells)
    ranges = []
    for cp in codes:
        if ranges and ranges[-1][0] + ranges[-1][1] == cp:
            ranges[-1][1] += 1
        else:
            ranges.append([cp, 1])
    out = bytearray(b"SSPF")
    out += struct.pack("<BBBBII", 1, CELL_W, CELL_H, len(ranges), len(codes), 0)
    for first, n in ranges:
        out += struct.pack("<II", first, n)
    for cp in codes:
        bits = bytearray(GLYPH_BYTES)
        for (x, y) in cells[cp]:
            i = y * CELL_W + x
            bits[i // 8] |= 0x80 >> (i % 8)
        out += bits
    return bytes(out), ranges


def latin_glyphs(font_c="ui/font.c"):
    """미리보기용: ui/font.c의 5x7 글꼴 표를 읽는다."""
    table = {}
    try:
        text = open(font_c, encoding="utf-8").read()
    except OSError:
        return table
    for m in re.finditer(r"\{'(\\?.)',\s*\{((?:0x[0-9A-Fa-f]{2},?){7})\}\}", text):
        table[m.group(1)[-1]] = [int(v, 16) for v in m.group(2).rstrip(",").split(",")]
    return table


def draw_text(img, w, x, y, text, cells, latin, latin_dy, color):
    """ui_text와 같은 규칙으로 그린다. img는 (r, g, b)를 w개씩 이어 놓은 목록이다."""
    def dot(px, py):
        if 0 <= px < w and 0 <= py and py * w + px < len(img):
            img[py * w + px] = color

    mixed = any(ord(ch) >= 128 for ch in text)
    for ch in text:
        if ord(ch) < 128:
            rows = latin.get(ch.upper(), ())
            for r, bits in enumerate(rows):
                for k in range(5):
                    if bits & (0x10 >> k):
                        dot(x + k, y + r + (latin_dy if mixed else 0))
            x += 6
        else:
            for (cx, cy) in cells.get(ord(ch), ()):
                dot(x + cx, y + cy)
            x += CELL_W
    return x


def write_ppm(path, img, w, h, scale):
    rows = []
    for y in range(h):
        line = b"".join(bytes(img[y * w + x]) * scale for x in range(w))
        rows.append(line * scale)
    with open(path, "wb") as f:
        f.write(b"P6\n%d %d\n255\n" % (w * scale, h * scale))
        f.write(b"".join(rows))


def preview(path, cells, label, scale=4):
    latin = latin_glyphs()
    w, pitch = 236, 14
    h = 10 + pitch * len(SAMPLE) + 4
    img = [(16, 18, 28)] * (w * h)
    draw_text(img, w, 2, 1, label, cells, latin, LATIN_DY, (255, 210, 60))
    for i, line in enumerate(SAMPLE):
        draw_text(img, w, 2, 11 + i * pitch, line, cells, latin, LATIN_DY, (235, 235, 235))
    write_ppm(path, img, w, h, scale)


def main():
    ap = argparse.ArgumentParser(description="TTF 글꼴을 12x12 한글 글꼴 파일로 바꾼다")
    ap.add_argument("font", help="글꼴 파일(.ttf)")
    ap.add_argument("out", help="결과 파일(예: ui/assets/hangul12.fnt)")
    ap.add_argument("--mode", default=MODE, choices=sorted(LOAD_FLAGS))
    ap.add_argument("--ppem", type=float, default=PPEM, help="글자를 그리는 크기(픽셀)")
    ap.add_argument("--threshold", type=float, default=THRESHOLD, help="회색 그림에서 이만큼 덮인 픽셀부터 켠다(0~1)")
    ap.add_argument("--no-rescue", action="store_true", help="그림이 같아진 글자를 다시 그리지 않는다")
    ap.add_argument("--preview", help="예문을 그린 PPM 그림을 이 이름으로 만든다")
    a = ap.parse_args()

    cells, st = build(a.font, a.mode, a.ppem, a.threshold, rescue=not a.no_rescue)
    data, ranges = pack(cells)
    with open(a.out, "wb") as f:
        f.write(data)
    print(f"{a.out}: 글자 {len(cells)}개, 구간 {len(ranges)}개, {len(data)}바이트")
    print(f"자리: 가로 {st['xoff']}칸, 밑줄 {st['base']}번째 줄. 따로 민 글자 {st['moved']}개, 잘린 픽셀 {st['clipped']}개, "
          f"빈 글자 {st['empty']}개")
    print(f"그림이 같은 글자 {st['same_before']}개 -> 다시 그린 뒤 {st['same']}개. "
          f"마지막 열을 쓰는 글자 {st['last_col']}개, 마지막 줄을 쓰는 글자 {st['last_row']}개, "
          f"글자당 켜진 픽셀 {st['pixels']:.1f}개")
    if a.preview:
        preview(a.preview, cells, f"{a.mode} {a.ppem:g}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
