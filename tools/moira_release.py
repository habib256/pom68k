#!/usr/bin/env python3
"""Export the Moira fork, its JIT and their machine-free gates as a release.

The release is a source tree that builds and tests on its own: the 68000-68040
core (`extern/moira/Moira`), the 80-bit softfloat behind its FPU, the JIT
(`src/jit`), six header-only host adapters the JIT gates use, and every gate
that links no Macintosh. Each exported file keeps its POM68K path and bytes,
so `MANIFEST.sha256` can be checked against any POM68K checkout of the same
commit and every path the documents cite still resolves. Only four files are
release-specific: the root `CMakeLists.txt`, `README.md` and `LICENSES.md`
(templates under `tools/moira_release/`) and the generated `RELEASE.txt`.

    tools/moira_release.py --check              # closure of the worktree
    tools/moira_release.py --out DIR            # tarball of HEAD (clean)
    tools/moira_release.py --out DIR --upstream CLONE --verify

`--check` is what `moira_release_test` runs: every listed path exists and is
tracked, and every quoted `#include` of every exported source resolves inside
the export — the property that makes the release build without POM68K.
`--upstream` takes a checkout of github.com/dirkwhoffmann/Moira at the fork's
recorded origin (UPSTREAM_PIN) and adds `patches/moira-vs-upstream.patch`.
`--verify` configures, builds and runs the `unit` and `jit` labels of the
extracted release in a scratch directory, and fails on any soft-skip there.
"""
import argparse
import gzip
import hashlib
import io
import os
from pathlib import Path
import re
import shutil
import subprocess
import sys
import tarfile
import tempfile

sys.dont_write_bytecode = True
ROOT = Path(__file__).resolve().parents[1]
TEMPLATES = "tools/moira_release"

# github.com/dirkwhoffmann/Moira commit the fork started from; recovered from
# NeoST's submodule gitlink on 2026-08-28 (NeoST NEOST_VENDOR.md § Pin
# d'origine) and recorded in extern/moira/NEOST_VENDOR.md.
UPSTREAM_PIN = "1efd69467ca13b27b2fb40febd5cb31dbecdea5f"

# What ships. Directories are taken whole (tracked files only).
EXPORT = [
    # The core and its records.
    "extern/moira/Moira",
    "extern/moira/LICENSE",
    "extern/moira/README.md",
    "extern/moira/POM68K_VENDOR.md",
    "extern/moira/POM68K_SURFACE.md",
    "extern/moira/NEOST_VENDOR.md",
    "extern/softfloat",
    # The JIT and the header-only adapters its gates include.
    "src/jit",
    "src/SaveState.h",
    "src/JitStartupConfig.h",
    "src/StartupSnapshot.h",
    "src/StartupOptions.h",
    "src/StartupValueDecoding.h",
    "src/StartupValuePolicy.h",
    # Gates that link Moira (or Moira + JIT) and nothing else.
    "tests/softfloat_primitives_test.c",
    "tests/cache040_test.cpp",
    "tests/fpu_sanity.cpp",
    "tests/fpu040_test.cpp",
    "tests/fpu_context_test.cpp",
    "tests/callm_rtm_test.cpp",
    "tests/berr020_test.cpp",
    "tests/berr030_test.cpp",
    "tests/moira_rules_test.cpp",
    "tests/moira_state_test.cpp",
    "tests/mmu040_atc_test.cpp",
    "tests/moira_interp_test.cpp",
    "tests/moira_interp_compare.cmake",
    "tests/jit_asset_free_lockstep_test.cpp",
    "tests/jit_atc040_cases.cpp",
    "tests/jit_atc040_cases.h",
    "tests/jit_copyback_write_040_test.cpp",
    "tests/jit_restart_write_030_test.cpp",
    "tests/jit_store_guard_a64_test.cpp",
    "tests/jit_backend_parity_test.cpp",
    "tests/JitTestConfig.h",
    "tests/PortableEnv.h",
    # Differential corpora: harnesses, ruling lists, locks, the owned corpus.
    "tests/sst68000.cpp",
    "tests/sst68030.cpp",
    "tests/sst68040.cpp",
    "tests/SstJitLockstep.h",
    "tests/SstKnown.h",
    "tests/sst68030.known",
    "tests/sst68030_fpuraw.known",
    "tests/sst68030_fpu2.known",
    "tests/sst68030_rmw.known",
    "tests/sst68040_fpu.known",
    "tests/sst68040_fpu2.known",
    "tests/sst68040_fpuh.known",
    "tests/sst_corpora.lock",
    "tests/fetch_sst_68000.sh",
    "tests/cpu_vectors",
    "tests/cpu_vectors_contract_test.py",
    "tools/cpu_vectors.py",
    "tools/verify_sst_corpora.py",
    "performance_budgets.tsv",
    # Rulings the core cites (`oracle/README.md § Rulings`), and 040 caches.
    "oracle/README.md",
    "docs/CACHE_040.md",
]
# Release-specific files: (template path, path in the release).
TEMPLATE_FILES = [
    (f"{TEMPLATES}/CMakeLists.txt", "CMakeLists.txt"),
    (f"{TEMPLATES}/README.md", "README.md"),
    (f"{TEMPLATES}/LICENSES.md", "LICENSES.md"),
]
LICENSE_COPY = ("LICENSE", "LICENSE-GPL-3.0")

INCLUDE_DIRS = ["extern/moira/Moira", "extern/softfloat", "src", "tests"]
SOURCE_SUFFIXES = {".c", ".cpp", ".h", ".hpp", ".inl"}
INCLUDE_RE = re.compile(r'^\s*#\s*include\s+"([^"]+)"', re.M)


def git(*args, binary=False):
    out = subprocess.run(["git", "-C", str(ROOT), *args], check=True,
                         capture_output=True)
    return out.stdout if binary else out.stdout.decode()


def tracked(rev, spec):
    """Tracked files under one export entry, at `rev` or in the worktree."""
    if rev is None:
        names = git("ls-files", "-z", "--", spec).split("\0")
    else:
        names = git("ls-tree", "-r", "-z", "--name-only", rev, "--", spec).split("\0")
    names = [n for n in names if n]
    if not names:
        raise SystemExit(f"moira_release: nothing tracked at '{spec}'")
    return names


def read(rev, name):
    """(bytes, executable) of one tracked file."""
    if rev is None:
        path = ROOT / name
        return path.read_bytes(), os.access(path, os.X_OK)
    mode = git("ls-tree", rev, "--", name).split()[0]
    return git("cat-file", "blob", f"{rev}:{name}", binary=True), mode == "100755"


def collect(rev):
    files = {}
    for spec in EXPORT:
        for name in tracked(rev, spec):
            files[name] = read(rev, name)
    for template, dest in TEMPLATE_FILES:
        files[dest] = read(rev, template)
    files[LICENSE_COPY[1]] = read(rev, LICENSE_COPY[0])
    return files


def include_closure(files):
    """Every quoted include must resolve inside the export."""
    present = set(files)
    missing = []
    for name, (data, _) in sorted(files.items()):
        if Path(name).suffix not in SOURCE_SUFFIXES:
            continue
        here = Path(name).parent
        for inc in INCLUDE_RE.findall(data.decode("utf-8", "replace")):
            candidates = [here / inc] + [Path(d) / inc for d in INCLUDE_DIRS]
            if not any(os.path.normpath(c) in present for c in candidates):
                missing.append(f"{name}: #include \"{inc}\"")
    return missing


def upstream_patch(upstream, files, mtime):
    """Unified diff of extern/moira/Moira against upstream's Moira/.

    diff writes each file's mtime into its header: both trees are stamped with
    the commit time and rendered in UTC, so the patch is byte-reproducible."""
    head = subprocess.run(["git", "-C", str(upstream), "rev-parse", "HEAD"],
                          capture_output=True, text=True)
    if head.returncode != 0 or head.stdout.strip() != UPSTREAM_PIN:
        raise SystemExit(f"moira_release: {upstream} is not a Moira checkout at {UPSTREAM_PIN}")
    with tempfile.TemporaryDirectory(prefix="moira-release-") as work:
        fork = Path(work) / "b" / "Moira"
        fork.mkdir(parents=True)
        for name, (data, _) in files.items():
            if name.startswith("extern/moira/Moira/"):
                (fork / name[len("extern/moira/Moira/"):]).write_bytes(data)
        shutil.copytree(Path(upstream) / "Moira", Path(work) / "a" / "Moira")
        for f in Path(work).rglob("*"):
            os.utime(f, (mtime, mtime))
        diff = subprocess.run(["diff", "-ruN", "a/Moira", "b/Moira"], cwd=work,
                              capture_output=True, env={**os.environ, "TZ": "UTC"})
    if diff.returncode not in (0, 1):
        raise SystemExit(diff.stderr.decode())
    return diff.stdout


def release_note(name, commit, date, files, patch):
    lines = [
        f"release   {name}",
        f"source    POM68K {commit} ({date})",
        f"upstream  github.com/dirkwhoffmann/Moira {UPSTREAM_PIN}",
        f"files     {len(files)}",
    ]
    if patch is not None:
        plus = sum(1 for l in patch.splitlines() if l.startswith(b"+") and not l.startswith(b"+++"))
        minus = sum(1 for l in patch.splitlines() if l.startswith(b"-") and not l.startswith(b"---"))
        lines.append(f"patch     patches/moira-vs-upstream.patch: +{plus} -{minus} lines")
    return ("\n".join(lines) + "\n").encode()


def write_tarball(path, prefix, files, mtime):
    """Byte-reproducible: sorted members, fixed owner, commit mtime."""
    raw = io.BytesIO()
    with tarfile.open(fileobj=raw, mode="w", format=tarfile.PAX_FORMAT) as tar:
        for name in sorted(files):
            data, executable = files[name]
            info = tarfile.TarInfo(f"{prefix}/{name}")
            info.size = len(data)
            info.mtime = mtime
            info.mode = 0o755 if executable else 0o644
            info.uid = info.gid = 0
            info.uname = info.gname = ""
            tar.addfile(info, io.BytesIO(data))
    with open(path, "wb") as out:
        with gzip.GzipFile(filename="", mode="wb", fileobj=out, mtime=0) as gz:
            gz.write(raw.getvalue())


def extract(files, dest):
    for name, (data, executable) in files.items():
        target = dest / name
        target.parent.mkdir(parents=True, exist_ok=True)
        target.write_bytes(data)
        if executable:
            target.chmod(0o755)


def verify(tree, jobs, sst_root):
    build = tree.parent / "build"
    config = ["cmake", "-S", str(tree), "-B", str(build), "-DCMAKE_BUILD_TYPE=Release"]
    if sst_root:
        config.append(f"-DMOIRA_POM68K_SST_ROOT={sst_root}")
    subprocess.run(config, check=True)
    subprocess.run(["cmake", "--build", str(build), "--parallel", str(jobs)], check=True)
    labels = "unit|jit|corpus" if sst_root else "unit|jit"
    result = subprocess.run(["ctest", "--test-dir", str(build), "-L", labels,
                             "-j", str(jobs), "--output-on-failure"])
    if result.returncode != 0:
        raise SystemExit("moira_release: release gates failed")
    # LastTest.log keeps every gate's output, passed ones included;
    # --output-log does not.
    text = (build / "Testing" / "Temporary" / "LastTest.log").read_text(errors="replace")
    skips = sorted(set(re.findall(r"^SKIP[: ].*$", text, re.M)))
    if skips:
        raise SystemExit("moira_release: soft-skips in the release tier:\n  " + "\n  ".join(skips))
    print(f"moira_release: release gates green ({labels}), no soft-skip")


def main():
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--check", action="store_true", help="closure check of the worktree only")
    ap.add_argument("--rev", default="HEAD", help="commit to export (default HEAD)")
    ap.add_argument("--out", type=Path, help="directory receiving the tarball")
    ap.add_argument("--upstream", type=Path, help="upstream Moira checkout at UPSTREAM_PIN")
    ap.add_argument("--verify", action="store_true", help="build and test the extracted release")
    ap.add_argument("--sst-root", type=Path, help="external corpora for --verify")
    ap.add_argument("--allow-dirty", action="store_true",
                    help="export although exported paths differ from the worktree")
    ap.add_argument("--jobs", type=int, default=os.cpu_count() or 4)
    args = ap.parse_args()

    if args.check:
        files = collect(None)
        missing = include_closure(files)
        if missing:
            print("moira_release: includes that leave the export:\n  " + "\n  ".join(missing))
            return 1
        print(f"moira_release: {len(files)} files, include closure complete")
        return 0

    if not args.out:
        ap.error("--out is required unless --check")
    rev = git("rev-parse", args.rev).strip()
    dirty = git("status", "--porcelain", "--", *EXPORT, TEMPLATES, LICENSE_COPY[0]).strip()
    if dirty and args.rev == "HEAD" and not args.allow_dirty:
        raise SystemExit("moira_release: exported paths have uncommitted changes:\n" + dirty)
    files = collect(rev)
    missing = include_closure(files)
    if missing:
        raise SystemExit("moira_release: includes leave the export:\n  " + "\n  ".join(missing))
    stamp = int(git("show", "-s", "--format=%ct", rev))
    date = git("show", "-s", "--format=%cs", rev).strip()
    name = f"moira-pom68k-{date.replace('-', '')}-{rev[:8]}"

    patch = upstream_patch(args.upstream, files, stamp) if args.upstream else None
    if patch is not None:
        files["patches/moira-vs-upstream.patch"] = (patch, False)
    files["RELEASE.txt"] = (release_note(name, rev, date, files, patch), False)
    manifest = "".join(f"{hashlib.sha256(files[n][0]).hexdigest()}  {n}\n"
                       for n in sorted(files))
    files["MANIFEST.sha256"] = (manifest.encode(), False)

    args.out.mkdir(parents=True, exist_ok=True)
    tarball = args.out / f"{name}.tar.gz"
    write_tarball(tarball, name, files, stamp)
    digest = hashlib.sha256(tarball.read_bytes()).hexdigest()
    (args.out / f"{name}.tar.gz.sha256").write_text(f"{digest}  {tarball.name}\n")
    print(f"moira_release: {tarball} ({len(files)} files, sha256 {digest})")

    if args.verify:
        with tempfile.TemporaryDirectory(prefix="moira-release-verify-") as work:
            tree = Path(work) / name
            with tarfile.open(tarball) as tar:
                tar.extractall(work)
            verify(tree, args.jobs, args.sst_root)
    return 0


if __name__ == "__main__":
    sys.exit(main())
