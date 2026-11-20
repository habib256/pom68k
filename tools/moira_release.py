#!/usr/bin/env python3
"""Export the Moira fork, its JIT and their machine-free gates as a release.

The release is a source tree that builds and tests on its own: the 68000-68040
core (`extern/moira/Moira`), the 80-bit softfloat behind its FPU, the JIT
(`src/jit`), six header-only host adapters the JIT gates use, and every gate
that links no Macintosh. Each exported file keeps its POM68K path and bytes,
so `MANIFEST.sha256` can be checked against any POM68K checkout of the same
commit and every path the documents cite still resolves. Only five files are
release-specific: the root `CMakeLists.txt`, `README.md`, `LICENSES.md` and
`UPSTREAM_GUIDE.md` (templates under `tools/moira_release/`) and the generated
`RELEASE.txt`.

    tools/moira_release.py --check              # closure of the worktree
    tools/moira_release.py --out DIR            # tarball of HEAD (clean)
    tools/moira_release.py --out DIR --upstream CLONE --verify

`--check` is what `moira_release_test` runs: every listed path exists and is
tracked, every quoted `#include` of every exported source resolves inside
the export — the property that makes the release build without POM68K — and
every exported gate source is named by the release's CMake files.
`--upstream` takes a clone of github.com/dirkwhoffmann/Moira holding the fork's
recorded base (UPSTREAM_PIN) and adds `patches/moira-vs-upstream.patch`, a
`git am` mailbox that recreates the fork's Moira/ as one commit on that base.
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

# github.com/dirkwhoffmann/Moira commit the fork is based on: upstream's master
# of 2026-09-23, onto which the fork was rebased on 2026-11-20
# (extern/moira/POM68K_VENDOR.md § Rebase onto upstream ce28239). The fork
# started from 1efd6946 (NeoST's submodule gitlink, NEOST_VENDOR.md).
UPSTREAM_PIN = "ce28239b0507ebfe1bf2ad2b9c9252dc2f2cc031"

# What ships. Directories are taken whole (tracked files only).
EXPORT = [
    # The license of everything the fork wrote (core additions, JIT, gates).
    "LICENSE-MIT",
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
    "tests/softfloat_symbols_test.py",
    "tests/cache040_test.cpp",
    "tests/fpu_sanity.cpp",
    "tests/fpu040_test.cpp",
    "tests/fpu_context_test.cpp",
    "tests/callm_rtm_test.cpp",
    "tests/moira_upstream_config_test.cpp",
    "tests/icache020_test.cpp",
    "tests/berr020_test.cpp",
    "tests/berr030_test.cpp",
    "tests/moira_rules_test.cpp",
    "tests/fault_transparency_test.cpp",
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
    "tests/jit_program_fuzz.cpp",
    # Its ~45 registrations, included as they are by the release CMakeLists.
    "cmake/Pom68kJitFuzzGates.cmake",
    "tests/JitTestConfig.h",
    "tests/PortableEnv.h",
    # Differential corpora: harnesses, ruling lists, locks, the owned corpus.
    "tests/sst68000.cpp",
    "tests/sst68030.cpp",
    "tests/sst68040.cpp",
    "tests/SstJitLockstep.h",
    "tests/SstKnown.h",
    "tests/sst68030.known",
    "tests/sst68030_solo.known",
    "tests/sst68030_fpuraw.known",
    "tests/sst68030_fpu2.known",
    "tests/sst68030_fpuh.known",
    "tests/sst68030_rmw.known",
    "tests/sst68040.known",
    "tests/sst68040_8k.known",
    "tests/sst68040_fpu.known",
    "tests/sst68040_fpu2.known",
    "tests/sst68040_fpuh.known",
    "tests/sst_corpora.lock",
    "tests/fetch_sst_68000.sh",
    "tests/cpu_vectors",
    "tests/cpu_vectors_contract_test.py",
    "tests/sst_known_contract_test.py",
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
    (f"{TEMPLATES}/UPSTREAM_GUIDE.md", "UPSTREAM_GUIDE.md"),
]
# The GPL text, for extern/softfloat alone (GPL-2.0-or-later files).
LICENSE_COPY = ("LICENSE", "LICENSE-GPL-3.0")

INCLUDE_DIRS = ["extern/moira/Moira", "extern/softfloat", "src", "tests"]
SOURCE_SUFFIXES = {".c", ".cpp", ".h", ".hpp", ".inl"}
INCLUDE_RE = re.compile(r'^\s*#\s*include\s+"([^"]+)"', re.M)
GATE_SUFFIXES = {".c", ".cpp"}


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


def unregistered_gates(files):
    """Every exported gate source must be built by the release.

    A source under tests/ that no release CMake file names would ship
    without running (fault_transparency_test did, from 2026-11-19 until the
    2026-11-20 cut). The release CMakeLists and every exported cmake/ file
    are searched for the source's stem."""
    cmake = "".join(data.decode("utf-8", "replace") for name, (data, _) in files.items()
                    if name == "CMakeLists.txt" or name.startswith("cmake/"))
    return [name for name in sorted(files)
            if name.startswith("tests/") and Path(name).suffix in GATE_SUFFIXES
            and not re.search(rf"\b{re.escape(Path(name).stem)}\b", cmake)]


def guide_coverage(files):
    """UPSTREAM_GUIDE.md sorts every inventory row, each exactly once.

    The guide tells upstream which patch groups concern stock Moira; a row
    added to POM68K_VENDOR.md without being sorted there would ship unseen.
    Section A names rows as `**Row N`; sections B, C and D list them in the
    second cell of a table row."""
    inventory = files["extern/moira/POM68K_VENDOR.md"][0].decode("utf-8", "replace")
    table = inventory.split("## Inventory of local patches", 1)[1].split("\n## ", 1)[0]
    rows = {int(n) for n in re.findall(r"^\| (\d+) \|", table, re.M)}
    found = {}
    guide = files["UPSTREAM_GUIDE.md"][0].decode("utf-8", "replace")
    for section in re.split(r"^## ", guide, flags=re.M):
        name = section[:1]
        if section[1:2] != " " or name not in "ABCD":
            continue
        cited = [int(n) for n in re.findall(r"^\*\*Row (\d+)\b", section, re.M)]
        if name != "A":
            for line in section.splitlines():
                cells = [c.strip() for c in line.strip("|").split("|")]
                if line.startswith("|") and len(cells) > 1 and re.fullmatch(r"[\d, ]+", cells[1]):
                    cited += [int(n) for n in re.findall(r"\d+", cells[1])]
        for n in cited:
            found.setdefault(n, []).append(name)
    problems = [f"row {n}: not sorted in UPSTREAM_GUIDE.md" for n in sorted(rows - set(found))]
    problems += [f"row {n}: sorted {len(v)} times ({', '.join(v)})"
                 for n, v in sorted(found.items()) if len(v) > 1]
    problems += [f"row {n}: in the guide, not in the inventory" for n in sorted(set(found) - rows)]
    return problems


def upstream_patch(upstream, files, mtime, source):
    """The fork's Moira/ as one git commit on top of upstream's UPSTREAM_PIN.

    The result is a `git am` mailbox with full blob ids: in a clone of
    upstream, `git checkout -b pom68k UPSTREAM_PIN && git am -3 <patch>`
    recreates the fork's tree as a branch whose parent is an ancestor of
    upstream's master, so git can merge, diff or cherry-pick it three-way.
    The objects are written to a scratch object directory that borrows the
    clone's through alternates: the clone itself is not modified. Author,
    dates and tree are fixed by the POM68K commit, so the mailbox is
    byte-reproducible."""
    def run(args, env=None, data=None):
        out = subprocess.run(["git", "-C", str(upstream), *args], capture_output=True,
                             env=env, input=data)
        if out.returncode != 0:
            raise SystemExit(f"moira_release: git {' '.join(args)}: {out.stderr.decode()}")
        return out.stdout
    head = subprocess.run(["git", "-C", str(upstream), "rev-parse", "--verify", "-q",
                           UPSTREAM_PIN + "^{commit}"], capture_output=True, text=True)
    if head.returncode != 0:
        raise SystemExit(f"moira_release: {upstream} is not a Moira clone holding {UPSTREAM_PIN}")
    objects = Path(run(["rev-parse", "--git-common-dir"]).decode().strip())
    if not objects.is_absolute():
        objects = Path(upstream) / objects
    with tempfile.TemporaryDirectory(prefix="moira-release-") as work:
        (Path(work) / "objects").mkdir()
        who, mail = source
        when = f"{mtime} +0000"
        env = {**os.environ, "TZ": "UTC",
               "GIT_INDEX_FILE": str(Path(work) / "index"),
               "GIT_OBJECT_DIRECTORY": str(Path(work) / "objects"),
               "GIT_ALTERNATE_OBJECT_DIRECTORIES": str(objects / "objects"),
               "GIT_AUTHOR_NAME": who, "GIT_AUTHOR_EMAIL": mail, "GIT_AUTHOR_DATE": when,
               "GIT_COMMITTER_NAME": who, "GIT_COMMITTER_EMAIL": mail,
               "GIT_COMMITTER_DATE": when}
        # Upstream's tree without its Moira/, then the fork's Moira/.
        entries = [line for line in run(["ls-tree", "-r", UPSTREAM_PIN]).decode().splitlines()
                   if not line.split("\t", 1)[1].startswith("Moira/")]
        for name in sorted(files):
            if not name.startswith("extern/moira/Moira/"):
                continue
            data, executable = files[name]
            blob = run(["hash-object", "-w", "--stdin"], env, data).decode().strip()
            mode = "100755" if executable else "100644"
            entries.append(f"{mode} {blob}\tMoira/{name[len('extern/moira/Moira/'):]}")
        run(["update-index", "--add", "--index-info"], env, ("\n".join(entries) + "\n").encode())
        tree = run(["write-tree"], env).decode().strip()
        message = ("Moira: the POM68K fork\n\n"
                   "The whole of POM68K's extern/moira/Moira as one commit on the upstream\n"
                   f"commit the fork is based on ({UPSTREAM_PIN[:7]}). POM68K_VENDOR.md\n"
                   "inventories the patch groups it contains; UPSTREAM_GUIDE.md sorts them\n"
                   "by what stock Moira could take.\n")
        commit = run(["commit-tree", tree, "-p", UPSTREAM_PIN], env, message.encode()).decode().strip()
        return run(["format-patch", "-1", "--stdout", "--full-index", "--binary",
                    "--no-signature", "--no-stat", "--zero-commit", commit], env)


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
    ap.add_argument("--upstream", type=Path, help="upstream Moira clone holding UPSTREAM_PIN")
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
        unbuilt = unregistered_gates(files)
        if unbuilt:
            print("moira_release: exported gates the release never builds:\n  " + "\n  ".join(unbuilt))
            return 1
        unsorted = guide_coverage(files)
        if unsorted:
            print("moira_release: inventory rows and UPSTREAM_GUIDE.md disagree:\n  " + "\n  ".join(unsorted))
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
    unbuilt = unregistered_gates(files)
    if unbuilt:
        raise SystemExit("moira_release: exported gates the release never builds:\n  " + "\n  ".join(unbuilt))
    unsorted = guide_coverage(files)
    if unsorted:
        raise SystemExit("moira_release: inventory rows and UPSTREAM_GUIDE.md disagree:\n  " + "\n  ".join(unsorted))
    stamp = int(git("show", "-s", "--format=%ct", rev))
    date = git("show", "-s", "--format=%cs", rev).strip()
    name = f"moira-pom68k-{date.replace('-', '')}-{rev[:8]}"

    source = (git("show", "-s", "--format=%an", rev).strip(),
              git("show", "-s", "--format=%ae", rev).strip())
    patch = upstream_patch(args.upstream, files, stamp, source) if args.upstream else None
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
