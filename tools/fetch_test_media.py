#!/usr/bin/env python3
"""Put the reference media the asset-requiring gates open back into a tree.

The repository carries no system disk (they are private inputs, like ROMs).
Every image the gates need lives in one zip on archive.org, listed file by
file in tools/test_media.sha256: a path the list does not name is never
written, and a file whose SHA-256 differs from the list is refused.

A file already in the tree is never overwritten — a working copy may hold
volumes the emulator has written to; a differing one is reported "(kept)".
Files installed below hdv/ref/ or disks35/ref/ are made read-only, as those
reference fixtures must stay (CLAUDE.md, "Reference media remain
immutable"). Members are streamed to disk and hashed on the way: the
archive is ~210 MiB and unpacks to ~915 MiB, so nothing is held in memory.

Usage: tools/fetch_test_media.py [--strict] [--url URL] [--zip FILE]
                                 [--cache FILE] [--manifest FILE] [--root DIR]
  --zip FILE    use a local copy of the archive instead of downloading it
  --cache FILE  use FILE as the archive when it is one; otherwise download
                and keep the download there for the next run. A cached file
                that is not a zip, or a cached zip that fails the manifest,
                is downloaded once more and replaced. Every file is
                SHA-256-checked whatever the zip's origin.
  --strict      a kept-but-different file is an error too (CI)
  --self-test   offline check of the extraction and cache rules

Downloads use HTTPS only. This script uploads nothing. The ROMs are not in
this archive: the emulator's own downloader (src/RomFetch.h) installs them,
and tools/build_asset_archives.py builds both archives.
"""

import argparse
import hashlib
import os
import shutil
import stat
import sys
import tempfile
import time
import urllib.request
import zipfile

# The item does not exist yet; a 404/403/HTML answer is reported, not fatal
# to anything but this run.
DEFAULT_URL = "https://archive.org/download/pom68k_media/pom68k_media.zip"
CHUNK = 1 << 20
MAX_DOWNLOAD = 2 << 30          # the archive is ~210 MiB; refuse anything absurd


def read_manifest(path):
    entries = {}
    with open(path, encoding="utf-8") as f:
        for line in f:
            line = line.rstrip("\n")
            if not line.strip() or line.lstrip().startswith("#"):
                continue
            digest, rel = line.split("  ", 1)
            if rel.startswith("/") or ".." in rel.split("/"):
                raise SystemExit(f"fetch_test_media: unsafe manifest path {rel!r}")
            entries[rel] = digest.lower()
    return entries


def download(url, dest):
    """Stream `url` to `dest`. Six tries with growing pauses: archive.org
    answers 5xx for a minute now and then."""
    if not url.startswith("https://"):
        raise SystemExit(f"fetch_test_media: refusing a non-HTTPS URL: {url}")
    last = None
    for attempt in range(1, 7):
        try:
            req = urllib.request.Request(url, headers={"User-Agent": "pom68k-fetch-test-media"})
            with urllib.request.urlopen(req, timeout=120) as r, open(dest, "wb") as out:
                if not r.geturl().startswith("https://"):
                    raise OSError(f"redirected off HTTPS to {r.geturl()}")
                total = 0
                for chunk in iter(lambda: r.read(CHUNK), b""):
                    total += len(chunk)
                    if total > MAX_DOWNLOAD:
                        raise OSError("the download is larger than any media archive")
                    out.write(chunk)
            return
        except Exception as e:  # network errors, HTTP 4xx/5xx
            last = e
            print(f"fetch_test_media: attempt {attempt} failed: {e}", file=sys.stderr)
            if getattr(e, "code", None) in (403, 404):
                break                          # the item is not there; retrying will not help
            time.sleep(5 * attempt)
    raise SystemExit(f"fetch_test_media: cannot download {url}: {last}")


def sha256_file(path):
    digest = hashlib.sha256()
    with open(path, "rb") as f:
        for chunk in iter(lambda: f.read(CHUNK), b""):
            digest.update(chunk)
    return digest.hexdigest()


def is_reference(rel):
    parts = rel.split("/")
    return len(parts) > 2 and parts[0] in ("hdv", "disks35") and parts[1] == "ref"


def extract(zip_path, want, root):
    """Write every listed file the tree lacks. Returns (written, kept,
    problems); a problem that does not end in "(kept)" is the archive's."""
    written = kept = 0
    problems = []
    try:
        z = zipfile.ZipFile(zip_path)
    except (zipfile.BadZipFile, OSError) as e:
        return 0, 0, [f"the archive is not a readable zip ({e})"]
    with z:
        members = {i.filename: i for i in z.infolist() if not i.is_dir()}
        for rel, digest in sorted(want.items()):
            dst = os.path.join(root, *rel.split("/"))
            if os.path.exists(dst):
                if sha256_file(dst) != digest:
                    problems.append(f"{rel}: the tree's copy differs from the manifest (kept)")
                kept += 1
                continue
            if rel not in members:
                problems.append(f"{rel}: missing from the archive")
                continue
            os.makedirs(os.path.dirname(dst) or ".", exist_ok=True)
            tmp = dst + ".fetchtmp"
            h = hashlib.sha256()
            try:
                with z.open(members[rel]) as src, open(tmp, "wb") as out:
                    for chunk in iter(lambda: src.read(CHUNK), b""):
                        h.update(chunk)
                        out.write(chunk)
            except (zipfile.BadZipFile, OSError, EOFError, ValueError) as e:
                problems.append(f"{rel}: unreadable in the archive ({e})")
                if os.path.exists(tmp):
                    os.remove(tmp)
                continue
            if h.hexdigest() != digest:
                os.remove(tmp)
                problems.append(f"{rel}: SHA-256 mismatch in the archive (not written)")
                continue
            if is_reference(rel):
                os.chmod(tmp, stat.S_IRUSR | stat.S_IRGRP | stat.S_IROTH)
            os.replace(tmp, dst)
            written += 1
    return written, kept, problems


def archive_problems(problems):
    return [p for p in problems if not p.endswith("(kept)")]


def fetch(args, want, fetcher=download):
    """Locate or download the archive, extract, and on a bad cache retry
    once from the network."""
    from_cache = False
    with tempfile.TemporaryDirectory() as scratch:
        if args.zip:
            zip_path = args.zip
        elif args.cache and os.path.exists(args.cache) and zipfile.is_zipfile(args.cache):
            print(f"fetch_test_media: using the cached archive {args.cache}")
            zip_path, from_cache = args.cache, True
        else:
            if args.cache and os.path.exists(args.cache):
                print(f"fetch_test_media: {args.cache} is not a zip — downloading again",
                      file=sys.stderr)
            zip_path = args.cache or os.path.join(scratch, "media.zip")
            print(f"fetch_test_media: downloading {args.url}", flush=True)
            part = zip_path + ".fetchtmp"
            os.makedirs(os.path.dirname(os.path.abspath(part)), exist_ok=True)
            fetcher(args.url, part)
            os.replace(part, zip_path)

        written, kept, problems = extract(zip_path, want, args.root)
        if from_cache and archive_problems(problems):
            print(f"fetch_test_media: the cached archive fails the manifest "
                  f"({archive_problems(problems)[0]}) — downloading again", file=sys.stderr)
            part = args.cache + ".fetchtmp"
            fetcher(args.url, part)
            os.replace(part, args.cache)
            more, kept, problems = extract(args.cache, want, args.root)
            written += more
    return written, kept, problems


def self_test():
    import types

    def make_zip(path, members):
        with zipfile.ZipFile(path, "w", zipfile.ZIP_DEFLATED) as z:
            for name, data in members.items():
                z.writestr(name, data)

    good_members = {"hdv/ref/A.vhd": b"reference payload", "disks35/B.dsk": b"floppy"}
    want = {rel: hashlib.sha256(data).hexdigest() for rel, data in good_members.items()}
    with tempfile.TemporaryDirectory() as d:
        good = os.path.join(d, "good.zip")
        bad = os.path.join(d, "bad.zip")
        evil = os.path.join(d, "evil.zip")
        make_zip(good, good_members)
        make_zip(bad, {"hdv/ref/A.vhd": b"reference payload"})          # B missing
        make_zip(evil, {"hdv/ref/A.vhd": b"tampered", "disks35/B.dsk": b"floppy",
                        "../escape": b"x", "roms/unlisted.bin": b"x"})
        calls = []

        def fetcher(url, dest):
            calls.append(url)
            shutil.copyfile(good, dest)

        # 1. A poisoned cache is replaced; a good one is not re-fetched.
        for cached, expect_calls in ((bad, 1), (good, 0)):
            tree = tempfile.mkdtemp(dir=d)
            cache = os.path.join(d, "cache.zip")
            shutil.copyfile(cached, cache)
            calls.clear()
            args = types.SimpleNamespace(zip=None, cache=cache, url="https://x/y.zip", root=tree)
            written, _, problems = fetch(args, want, fetcher)
            assert len(calls) == expect_calls, calls
            assert written == 2 and not problems, problems
            with open(cache, "rb") as f, open(good, "rb") as g:
                assert f.read() == g.read(), "cache not replaced"
            ref = os.path.join(tree, "hdv", "ref", "A.vhd")
            assert not os.stat(ref).st_mode & (stat.S_IWUSR | stat.S_IWGRP | stat.S_IWOTH), \
                "a reference fixture was left writable"

        # 2. A member with the wrong digest is not written; unlisted and
        #    escaping members are never written at all.
        tree = tempfile.mkdtemp(dir=d)
        written, _, problems = extract(evil, want, tree)
        assert written == 1, written
        assert any("SHA-256 mismatch" in p for p in problems), problems
        assert not os.path.exists(os.path.join(tree, "hdv", "ref", "A.vhd"))
        assert not os.path.exists(os.path.join(d, "escape"))
        assert not os.path.exists(os.path.join(tree, "roms"))

        # 3. An existing file is never overwritten, even when it differs.
        tree = tempfile.mkdtemp(dir=d)
        os.makedirs(os.path.join(tree, "disks35"))
        with open(os.path.join(tree, "disks35", "B.dsk"), "wb") as f:
            f.write(b"written by the emulator")
        written, kept, problems = extract(good, want, tree)
        assert written == 1 and kept == 1 and problems and problems[0].endswith("(kept)")
        with open(os.path.join(tree, "disks35", "B.dsk"), "rb") as f:
            assert f.read() == b"written by the emulator"

        # 4. An HTML page served in place of the zip is reported, not trusted.
        html = os.path.join(d, "page.zip")
        with open(html, "wb") as f:
            f.write(b"<!DOCTYPE html><html><body>Item not available</body></html>")
        _, _, problems = extract(html, want, tempfile.mkdtemp(dir=d))
        assert problems and "not a readable zip" in problems[0], problems

        # 5. The manifest refuses paths that leave the tree.
        manifest = os.path.join(d, "m.sha256")
        with open(manifest, "w") as f:
            f.write("0" * 64 + "  ../outside\n")
        try:
            read_manifest(manifest)
            raise AssertionError("an escaping manifest path was accepted")
        except SystemExit:
            pass

    here = os.path.dirname(os.path.abspath(__file__))
    listed = read_manifest(os.path.join(here, "test_media.sha256"))
    assert listed, "tools/test_media.sha256 is empty"
    print(f"fetch_test_media: self-test OK ({len(listed)} files listed)")
    return 0


def main():
    here = os.path.dirname(os.path.abspath(__file__))
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    ap.add_argument("--url", default=DEFAULT_URL)
    ap.add_argument("--zip")
    ap.add_argument("--cache")
    ap.add_argument("--manifest", default=os.path.join(here, "test_media.sha256"))
    ap.add_argument("--root", default=os.path.dirname(here))
    ap.add_argument("--strict", action="store_true")
    ap.add_argument("--self-test", action="store_true")
    args = ap.parse_args()
    if args.self_test:
        return self_test()

    want = read_manifest(args.manifest)
    written, kept, problems = fetch(args, want)
    print(f"fetch_test_media: {written} written, {kept} already present, "
          f"{len(problems)} problem(s), {len(want)} listed")
    for p in problems:
        print(f"  {p}", file=sys.stderr)
    if problems and (args.strict or archive_problems(problems)):
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
