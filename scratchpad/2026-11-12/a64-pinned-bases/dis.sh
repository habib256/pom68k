#!/bin/sh
# usage: dis.sh file.bin  -> disassembly on stdout
python3 -c "
import sys,struct
d=open(sys.argv[1],'rb').read()
print('.text\n_blk:')
for i in range(0,len(d)-3,4): print('.inst 0x%08x'%struct.unpack('<I',d[i:i+4]))
" "$1" > "$1.s"
clang -c -arch arm64 "$1.s" -o "$1.o" && objdump -d --no-show-raw-insn "$1.o" | tail -n +7
