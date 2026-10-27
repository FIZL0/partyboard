#!/bin/sh
# syms.sh > syms.txt: every static/global variable of src/game/board/*.c ("file name size"), for dump.py.
B=$(cd "$(dirname "$0")/../../../.." && pwd)/build/linux-default-relwithdebinfo
for f in "$B"/CMakeFiles/dol.dir/src/game/board/*.c.o; do
  b=$(basename "$f" .o)
  nm -S --defined-only "$f" | awk -v b="$b" '$3 ~ /^[bBdD]$/ && $4 !~ /^\./ {print b, $4, strtonum("0x"$2)}'
done
