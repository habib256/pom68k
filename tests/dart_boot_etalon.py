#!/usr/bin/env python3
"""Boot the real Plus ROM from a temporary DART representation of its fixture."""
import pathlib
import struct
import subprocess
import sys
import tempfile

runner, source, location = sys.argv[1:]
source = pathlib.Path(source)
if not source.is_file():
    print(f"SKIP: needs {source}")
    sys.exit(0)
data = source.read_bytes()
if len(data) != 819200:
    raise ValueError("the Plus System 3.3 reference must be raw 800K media")
# Independent data-fork writer: forty data/tag chunks, fast-mode ffff entries.
archive = bytearray(struct.pack(">BBH", 0, 1, 800) + b"\xff\xff" * 40)
for offset in range(0, len(data), 20480):
    archive.extend(data[offset:offset + 20480])
    archive.extend(bytes(480))
with tempfile.TemporaryDirectory(prefix="pom68k-dart-boot-") as directory:
    path = pathlib.Path(directory) / "System 3.3.dart"
    path.write_bytes(archive)
    command = [runner]
    if location == "external":
        command.append("--external")
    command.append(str(path))
    sys.exit(subprocess.run(command, check=False).returncode)
