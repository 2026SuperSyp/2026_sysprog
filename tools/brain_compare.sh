#!/bin/sh
# 대조 실험: 진짜 배선과 섞은 배선에 같은 자극을 넣고 반응을 나란히 보여 준다.
#   sh tools/brain_compare.sh [진짜 뇌 폴더] [섞은 뇌 폴더 ...]
# 섞은 뇌 폴더가 없으면 씨앗 1로 만든다. 섞는 방법은 tools/brain_shuffle.py에 있다.
# 숫자는 뉴런 하나의 초당 발화 수다. 원래 표는 build/logs/brainprobe_*.txt에 남는다.
real=${1:-data}
[ $# -gt 0 ] && shift
[ $# -gt 0 ] || set -- "$real/shuffled"
probe=${SSP_BUILD:-build/dev}/brainprobe
logs=build/logs
mkdir -p "$logs"

if [ ! -s "$real/brain.bin" ]; then
    echo "진짜 뇌의 자료가 없습니다. 먼저 make brain-data 를 실행하세요."
    exit 2
fi

files=""
k=0
for dir in "$real" "$@"; do
    if [ ! -s "$dir/brain.bin" ]; then
        echo "$dir 가 없어서 만듭니다(씨앗 1)."
        data/venv/bin/python tools/brain_shuffle.py "$real" "$dir" 1 || exit 1
    fi
    # 집단과 위치 파일은 진짜 뇌의 것과 같아야 비교가 된다. 섞은 폴더의 것을 매번 다시 맞춘다
    if [ "$dir" != "$real" ]; then cp "$real/brain_groups.txt" "$real/brain_map.bin" "$dir/" || exit 1; fi
    "$probe" "$dir" > "$logs/brainprobe_$k.txt" || exit 1
    files="$files $logs/brainprobe_$k.txt"
    k=$((k + 1))
done

# brainprobe의 한 줄: 자극 이름, 반응 5개, |, 총 발화, 계산 뉴런, |, 틱 평균 ms, 최대 ms
# 자극 이름에 빈칸이 있어서 칸은 뒤에서부터 센다. 한글은 폭이 달라서 이름을 줄 끝에 둔다.
awk -v dirs="$real $*" '
FNR == 1 { file++ }
FNR <= 2 { next }
{
    row = FNR - 2
    if (row > rows) rows = row
    label[row] = $1
    for (i = 2; i <= NF - 11; i++) label[row] = label[row] " " $i
    cell[row, file] = sprintf("%7.1f%7.1f%7.1f%7.1f%7.1f%9d%7.2f%7.2f",
                              $(NF - 10), $(NF - 9), $(NF - 8), $(NF - 7), $(NF - 6), $(NF - 4), $(NF - 1), $NF)
}
END {
    split(dirs, dir, " ")
    top = ""; head = ""
    for (f = 1; f <= file; f++) {
        top = top (f > 1 ? " | " : "") sprintf("%s %-47s", f == 1 ? "진짜 배선:" : "섞은 배선:", dir[f])
        head = head (f > 1 ? " | " : "") "   도주   왼쪽 오른쪽   후진   먹기   총발화   틱ms 최대ms"
    }
    print top
    print head " | 자극(초당 발화 수)"
    for (r = 1; r <= rows; r++) {
        line = ""
        for (f = 1; f <= file; f++) line = line (f > 1 ? " | " : "") cell[r, f]
        print line " | " label[r]
    }
}' $files
