#!/bin/zsh
# usage: mkharness.sh <tree> <builddir> <outbin>
S=/private/tmp/claude-501/-Users-mercure-src-pom68k/2079b422-640b-467c-9f5a-5b4aa8ae7894/scratchpad
cd $1 && python3 - <<PY
p='tests/q630_ide_boot_etalon.cpp'; s=open(p).read()
head=s[:s.index('int main() {')].replace('#include <fstream>','#include <fstream>\n#include <functional>')
open(p,'w').write(head+open('$S/harness_main.inc').read())
PY
cmake --build $2 --target q630_ide_boot_etalon 2>&1 | grep -E "error|Built target" ; cp $2/q630_ide_boot_etalon $3; git checkout tests/q630_ide_boot_etalon.cpp
