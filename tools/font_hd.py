"""윤곽선 글꼴 변환기: TTF 글꼴의 글자 윤곽선을 꺼내 ui/assets/font_outline.fnt 를 만든다.

화면을 실제 픽셀로 그릴 때(Sixel, ui/hd.c) 쓰는 글꼴이다. 글자를 그림으로 담아 두면 정해진 크기에서만 또렷하고
키우면 흐려진다. 그래서 그림이 아니라 윤곽선(점과 곡선)을 담아 두고, 그릴 때 원하는 크기로 채운다. 어떤 크기에서도
가장자리가 또렷하다. 글자가 차지하는 칸의 비율은 12x12 글꼴(tools/font_convert.py)과 같아서(한글은 정사각형 한 칸,
영문은 그 절반 폭) 화면 배치는 그대로 두고 글자만 바꿔 그릴 수 있다.

쓰는 법 (저장소 맨 위 폴더에서)
    data/venv/bin/python -m pip install freetype-py          (한 번만)
    F=data/raw/font
    data/venv/bin/python tools/font_hd.py $F/Freesentation-5Medium.ttf ui/assets/font_outline.fnt --latin $F/Freesentation-7Bold.ttf

글꼴
    Freesentation(프리젠테이션체) 2.001. 한글은 5 Medium, 영문과 숫자는 7 Bold(한글보다 작게 그려서 굵은 것을 쓴다).
    SIL Open Font License 1.1 (ui/assets/FONT_LICENSE.txt).
    글꼴 파일은 저장소에 올리지 않는다(data/raw/font/).

담는 글자
    영문 대문자, 숫자, 기호(0x21..0x7E에서 소문자를 뺀 것). 소문자는 그릴 때 대문자로 바꾼다(12x12 때와 같다).
    한글 음절 2350자(KS X 1001의 완성형), 호환 자모 51자, 그리고 tools/font_convert.py의 EXTRA와 같은 부호.
    여기 없는 글자는 그릴 때 12x12 글꼴을 키워서 쓴다.

놓는 방법 (그릴 때 ui/hd.c가 한다. 필요한 비율은 파일 머리에 담는다)
    한글: 글자의 폭이 칸의 폭과 같아지는 크기. 음절 전체의 위아래 범위를 칸 가운데에 놓는다.
    영문: 대문자 높이가 칸의 26/48이 되는 크기. 칸(한글 칸의 절반 폭) 가운데에 놓고, 칸보다 넓은 글자는 가로로 줄인다.

파일 형식 (숫자는 리틀 엔디언)
    머리 32바이트
        0   "SSPV"
        4   버전(1)                               1바이트, 그리고 0이 3바이트
        8   글자 수 N                             4바이트
        12  한글: 글꼴 단위 1이 칸의 몇 배인가      float 4바이트 (칸이 60픽셀이면 단위 1이 이 값 x 60픽셀)
        16  한글: 밑줄의 자리(칸의 위에서, 칸의 몇 배) float
        20  영문: 글꼴 단위 1이 한글 칸의 몇 배인가   float
        24  영문: 밑줄의 자리(영문 칸의 위에서, 한글 칸의 몇 배) float
        28  0                                     4바이트
    글자표  N개 x (글자 코드 4바이트, 윤곽선이 시작하는 자리 4바이트). 코드가 작은 것부터.
            코드가 128보다 작으면 영문(좁은 칸), 아니면 한글(넓은 칸)의 비율로 놓는다.
    윤곽선  글자마다: 나아가는 폭, 가로 범위(가장 왼쪽, 가장 오른쪽), 세로 범위(가장 아래, 가장 위)   각 2바이트(부호 있음)
                      닫힌 선의 수 2바이트, 그리고 닫힌 선마다 점의 수 2바이트와 점들
            점 하나는 (x * 2 + 곡선 위의 점이면 1, y) 각 2바이트(부호 있음). 글꼴 단위이고 y는 위가 양수다.
            TrueType의 규칙 그대로다: 곡선 위의 점 사이의 점은 2차 곡선의 조절점이고, 조절점이 둘 이어지면
            그 가운데에 곡선 위의 점이 하나 있는 것으로 본다.
"""
import argparse
import struct
import sys

import freetype

LATIN_CAP = 26.0 / 48.0            # 영문 대문자의 높이(한글 칸의 몇 배)
LATIN_BASE = 28.0 / 48.0           # 영문의 밑줄 자리(영문 칸의 위에서, 한글 칸의 몇 배)

EXTRA = [0x00B7, 0x00D7, 0x2018, 0x2019, 0x201C, 0x201D, 0x2026,
         0x2190, 0x2191, 0x2192, 0x2193, 0x2605, 0x2665]
JAMO = list(range(0x3131, 0x3164))


def ksx1001():
    out = []
    for b1 in range(0xB0, 0xC9):
        for b2 in range(0xA1, 0xFF):
            try:
                out.append(ord(bytes([b1, b2]).decode("euc-kr")))
            except UnicodeDecodeError:
                pass
    return sorted(out)


def outline(face, cp):
    """글자 하나의 윤곽선. (나아가는 폭, 닫힌 선들)이고 닫힌 선은 (x, y, 곡선 위인가)의 목록이다. 없으면 None."""
    if face.get_char_index(cp) == 0:
        return None
    face.load_char(chr(cp), freetype.FT_LOAD_NO_SCALE | freetype.FT_LOAD_NO_HINTING | freetype.FT_LOAD_NO_BITMAP)
    g = face.glyph
    o = g.outline
    contours, start = [], 0
    for end in o.contours:
        pts = []
        for i in range(start, end + 1):
            tag = o.tags[i]
            if tag & 2:
                raise SystemExit("3차 곡선이 있는 글꼴은 다루지 않는다 (U+%04X)" % cp)
            pts.append((o.points[i][0], o.points[i][1], tag & 1))
        if len(pts) >= 2:
            contours.append(pts)
        start = end + 1
    if not contours:
        return None
    return g.advance.x, contours


def pack(adv, contours):
    xs = [p[0] for c in contours for p in c]
    ys = [p[1] for c in contours for p in c]
    out = bytearray(struct.pack("<hhhhhH", adv, min(xs), max(xs), min(ys), max(ys), len(contours)))
    for c in contours:
        out += struct.pack("<H", len(c))
        for x, y, on in c:
            out += struct.pack("<hh", x * 2 + on, y)
    return bytes(out)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("ttf")
    ap.add_argument("out")
    ap.add_argument("--latin", help="영문, 숫자, 기호를 꺼낼 글꼴(없으면 같은 글꼴)")
    a = ap.parse_args()
    face = freetype.Face(a.ttf)
    lface = freetype.Face(a.latin) if a.latin else face
    glyphs = {}

    wide = {cp: outline(face, cp) for cp in ksx1001() + JAMO + EXTRA}
    wide = {cp: g for cp, g in wide.items() if g}
    adv = outline(face, ord("가"))[0]
    hangul = [g for cp, g in wide.items() if cp >= 0xAC00]
    asc = max(p[1] for g in hangul for c in g[1] for p in c)
    desc = -min(p[1] for g in hangul for c in g[1] for p in c)
    wide_k = min(1.0 / adv, 1.0 / (asc + desc))      # 칸보다 높으면 들어가게 줄인다
    wide_base = (1.0 - (asc + desc) * wide_k) / 2.0 + asc * wide_k
    glyphs.update(wide)

    cap = max(p[1] for c in outline(lface, ord("H"))[1] for p in c)
    narrow_k = LATIN_CAP / cap
    for cp in range(0x21, 0x7F):
        if ord("a") <= cp <= ord("z"):
            continue
        g = outline(lface, cp)
        if g:
            glyphs[cp] = g

    cps = sorted(glyphs)
    table, data = bytearray(), bytearray()
    for cp in cps:
        table += struct.pack("<II", cp, len(data))
        data += pack(*glyphs[cp])
    head = b"SSPV" + bytes([1, 0, 0, 0]) + struct.pack("<Iffff", len(cps), wide_k, wide_base, narrow_k, LATIN_BASE) + bytes(4)
    with open(a.out, "wb") as f:
        f.write(head + table + data)
    print("글자 %d개, %d바이트 (%s)" % (len(cps), len(head) + len(table) + len(data), a.out))
    print("한글: 단위 %d가 한 칸, 밑줄 %.3f / 영문: 대문자 높이 %d단위, 한 칸에 %.0f단위" % (round(1 / wide_k), wide_base, cap, 0.5 / narrow_k))
    return 0


if __name__ == "__main__":
    sys.exit(main())
