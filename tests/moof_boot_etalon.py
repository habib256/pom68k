#!/usr/bin/env python3
"""Boot an original Macintosh ROM from a temporary native physical capture."""
import pathlib
import subprocess
import sys
import tempfile

capture, runner, source, *options = sys.argv[1:]
source = pathlib.Path(source)
if not source.is_file():
    print(f"SKIP: needs {source}")
    sys.exit(0)
with tempfile.TemporaryDirectory(prefix="pom68k-moof-boot-") as directory:
    path = pathlib.Path(directory) / (source.stem + ".moof")
    subprocess.run([capture, str(source), str(path)], check=True)
    sys.exit(subprocess.run([runner, *options, str(path)], check=False).returncode)
