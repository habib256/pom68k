#!/bin/zsh
# usage: mass.sh <bin> <tag> <step> <nsteps> <rounds> <par> [env...]
S=/private/tmp/claude-501/-Users-mercure-src-pom68k/2079b422-640b-467c-9f5a-5b4aa8ae7894/scratchpad
B=$1; T=$2; ST=$3; N=$4; R=$5; P=$6
D=$S/mass_$T; mkdir -p $D; : > $D/results.txt
for r in $(seq 1 $R); do
  for i in $(seq 1 $P); do
    d=$D/run_$i; mkdir -p $d; ln -sfn /Users/mercure/src/pom68k-perf/roms $d/roms; ln -sfn /Users/mercure/src/pom68k-perf/hdv $d/hdv
    ( cd $d && $B replay $S/snap $ST $N > out.log 2>&1; e=$?; m=$(grep -a TMPHASH out.log | sed 's/ ata=.*//' | md5); echo "r$r i$i exit $e $m" >> $D/results.txt; cp out.log $D/log_$m.log 2>/dev/null ) &
  done
  wait
done
awk '{print $4}' $D/results.txt | sort | uniq -c
