#!/bin/sh
# 진짜 뇌에 필요한 자료를 받아서 data/ 아래에 준비한다. 한 번만 돌리면 된다(약 135MB를 받는다).
#   sh tools/brain_data.sh
# 결과: data/brain.bin, data/brain_groups.txt, data/brain_map.bin
# data/ 는 저장소에 올리지 않는다.
set -e
RAW=data/raw
mkdir -p "$RAW"

fetch() {   # fetch <파일 이름> <주소>
    if [ ! -s "$RAW/$1" ]; then
        echo "받는 중: $1"
        curl -fsSL -o "$RAW/$1.part" "$2"
        mv "$RAW/$1.part" "$RAW/$1"
    fi
}
SHIU=https://raw.githubusercontent.com/philshiu/Drosophila_brain_model/main
fetch Connectivity_783.parquet "$SHIU/Connectivity_783.parquet"
fetch Completeness_783.csv     "$SHIU/Completeness_783.csv"
fetch annotations.tsv "https://raw.githubusercontent.com/flyconnectome/flywire_annotations/main/supplemental_files/Supplemental_file1_neuron_annotations.tsv"

# 변환에만 쓰는 파이썬 환경. 시스템에는 아무것도 설치하지 않는다.
PY=data/venv/bin/python
if [ ! -x "$PY" ]; then
    python3 -m venv --without-pip data/venv
    curl -fsSL -o data/get-pip.py https://bootstrap.pypa.io/get-pip.py
    "$PY" data/get-pip.py -q
fi
"$PY" -c "import pyarrow, numpy" 2>/dev/null || "$PY" -m pip install -q pyarrow numpy

"$PY" tools/brain_convert.py "$RAW" data
ls -l data/brain.bin data/brain_groups.txt data/brain_map.bin
