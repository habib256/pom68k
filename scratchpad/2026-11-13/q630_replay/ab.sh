#!/bin/zsh
# usage: ab.sh <rounds>  — each round: 4 runs of the old binary, 4 of the fixed one
S=/private/tmp/claude-501/-Users-mercure-src-pom68k/2079b422-640b-467c-9f5a-5b4aa8ae7894/scratchpad
D=$S/ab; mkdir -p $D; : > $D/results.txt
for r in $(seq 1 $1); do
  for i in 1 2 3 4 5 6 7 8; do
    if [ $((i % 2)) -eq 1 ]; then B=$S/bin/q630y; T=old; else B=$S/bin/q630fix; T=fix; fi
    d=$D/run_$i; mkdir -p $d; ln -sfn /Users/mercure/src/pom68k-perf/roms $d/roms; ln -sfn /Users/mercure/src/pom68k-perf/hdv $d/hdv
    ( cd $d && $B replay $S/snap 7 12 > out.log 2>&1; e=$?; m=$(grep -a TMPHASH out.log | sed 's/ ata=.*//' | md5); echo "r$r $T exit $e $m" >> $D/results.txt; [ "$m" != e95179d69a27364806d8a05769b09680 ] && cp out.log $D/DIV_${T}_r${r}_i$i.log ) &
  done
  wait
done
date +%H:%M
awk '{print $2,$4,$5}' $D/results.txt | sort | uniq -c
