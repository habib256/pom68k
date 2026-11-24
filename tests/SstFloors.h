// POM68K — per-corpus-file agreement floors for the SST runners.
//
// The SST corpora were produced by other emulators (SingleStepTests 68000/v1
// by MAME's core, the 68030/68040 corpora by the WinUAE oracle): grade-D
// evidence. A disagreement opens an investigation; it is not a verdict, and
// a gate that demands full agreement stays red for every open question at
// once, which hides a real regression among the vectors that do agree.
// `--floors FILE` judges the run against a floor instead, one row per corpus
// file (tools/cpu_vectors.py does the same for the public vectors, per cell):
//
//   # comment
//   <file stem>  <vectors that must agree>  <vectors in the file>
//
// fields separated by blanks or tabs. The run fails when a file agrees on
// fewer vectors than its floor, when a file's vector count differs from its
// row (the corpus changed: tests/sst_corpora.lock pins it), when a corpus file
// has no row or a row has no corpus file, and when the ruled `--known`
// mechanism is violated (tests/SstKnown.h: a listed vector that passes, a
// stale or invalid frame/@SR rule, a listed name the corpus does not hold). A
// file that agrees on MORE than its floor is reported with the row to write,
// and does not fail: raising a floor is a deliberate edit.
//
// A ruled vector (a `--known` entry met as the list says) is counted apart,
// never as agreement; so is an open one (`@OPEN`, printed as `M open`). `--write-floors FILE` rewrites the floors from a whole
// run that violates nothing; no gate passes it.
#pragma once

#include <cstdio>
#include <fstream>
#include <map>
#include <sstream>
#include <string>
#include <vector>

namespace sstfloors {

struct FileTally {
    std::string file;               // corpus file stem
    long total = 0, agreed = 0, ruled = 0;
    long broken = 0;                // violations of the ruled --known mechanism
    long open = 0;                  // @OPEN vectors that agree: neither agreement nor ruled
};

// The gate a floors file serves: its stem (tests/sst_floors/sst68030_solo.tsv).
inline std::string gateOf(const std::string& path) {
    const size_t slash = path.find_last_of("/\\");
    std::string stem = slash == std::string::npos ? path : path.substr(slash + 1);
    const size_t dot = stem.rfind('.');
    return dot == std::string::npos ? stem : stem.substr(0, dot);
}

class Floors {
public:
    // False (with a message) when the file cannot be read or a row is malformed.
    bool load(const std::string& path) {
        path_ = path;
        gate_ = gateOf(path);
        std::ifstream in(path);
        if (!in) { std::fprintf(stderr, "cannot read --floors '%s'\n", path.c_str()); return false; }
        int lineNo = 0;
        for (std::string line; std::getline(in, line);) {
            ++lineNo;
            const size_t start = line.find_first_not_of(" \t\r");
            if (start == std::string::npos || line[start] == '#') continue;
            std::istringstream row(line);
            std::string file, extra;
            long floor = -1, total = -1;
            if (!(row >> file >> floor >> total) || (row >> extra) || floor < 0 ||
                total < floor || !rows_.emplace(file, Row{floor, total}).second) {
                std::fprintf(stderr, "%s:%d: malformed or duplicate floor row\n", path.c_str(), lineNo);
                return false;
            }
        }
        loaded_ = true;
        return true;
    }
    bool active() const { return loaded_; }

    // The per-file verdict printed beside the file's count.
    std::string mark(const FileTally& t) const {
        const auto it = rows_.find(t.file);
        if (t.broken) return "BROKEN-RULE";
        if (it == rows_.end()) return "NO-FLOOR-ROW";
        if (it->second.total != t.total) return "CORPUS-CHANGED";
        if (t.agreed < it->second.floor) return "BELOW-FLOOR " + std::to_string(it->second.floor);
        return "floor " + std::to_string(it->second.floor);
    }
    bool fileOk(const FileTally& t) const {
        const auto it = rows_.find(t.file);
        return !t.broken && it != rows_.end() && it->second.total == t.total &&
               t.agreed >= it->second.floor;
    }

    // Whole-run verdict: prints every failure, every floor that can be raised
    // and one summary line. `violations` counts run-level breaches of the
    // ruled mechanism (listed names the corpus does not hold, unreadable files).
    bool judge(const std::vector<FileTally>& run, long violations) const {
        bool ok = violations == 0;
        long agreed = 0, total = 0, ruled = 0, open = 0, floorTotal = 0, drops = 0;
        std::map<std::string, const FileTally*> seen;
        for (const FileTally& t : run) {
            seen.emplace(t.file, &t);
            agreed += t.agreed; total += t.total; ruled += t.ruled; open += t.open;
            if (t.broken) { ok = false; violations += t.broken; }
            const auto it = rows_.find(t.file);
            if (it == rows_.end()) {
                std::printf("  FAIL %s: corpus file has no row in %s (add: %s\t%ld\t%ld)\n",
                            t.file.c_str(), path_.c_str(), t.file.c_str(), t.agreed, t.total);
                ok = false;
                continue;
            }
            if (it->second.total != t.total) {
                std::printf("  FAIL %s: %ld vectors in the corpus, %ld in %s (corpus changed)\n",
                            t.file.c_str(), t.total, it->second.total, path_.c_str());
                ok = false;
            }
            if (t.agreed < it->second.floor) {
                std::printf("  FAIL %s: %ld agree, below its floor %ld (%s)\n",
                            t.file.c_str(), t.agreed, it->second.floor, path_.c_str());
                ok = false;
                ++drops;
            } else if (t.agreed > it->second.floor && it->second.total == t.total) {
                std::printf("  RAISE %s: %ld agree, floor %ld; write the row in %s as:\n"
                            "%s\t%ld\t%ld\n", t.file.c_str(), t.agreed, it->second.floor,
                            path_.c_str(), t.file.c_str(), t.agreed, t.total);
            }
        }
        for (const auto& [file, row] : rows_) {
            floorTotal += row.floor;
            if (!seen.count(file)) {
                std::printf("  FAIL %s: listed in %s but absent from the corpus\n",
                            file.c_str(), path_.c_str());
                ok = false;
            }
        }
        std::printf("%s: %ld/%ld agree (floor %ld), %ld ruled", gate_.c_str(), agreed, total,
                    floorTotal, ruled);
        if (open) std::printf(", %ld open", open);
        if (!ok)
            std::printf("  <<< FAIL: %ld file(s) below floor, %ld ruled-list violation(s)",
                        drops, violations);
        std::printf("\n");
        return ok;
    }

    // Rewrites `path` from a whole run. Refuses a run that violates the rules.
    static bool write(const std::string& path, const std::vector<FileTally>& run, long violations) {
        for (const FileTally& t : run) violations += t.broken;
        if (violations) {
            std::fprintf(stderr, "--write-floors refused: %ld ruled-list violation(s)\n", violations);
            return false;
        }
        std::map<std::string, const FileTally*> sorted;
        for (const FileTally& t : run) sorted.emplace(t.file, &t);
        std::ofstream out(path);
        if (!out) { std::fprintf(stderr, "cannot write '%s'\n", path.c_str()); return false; }
        long agreed = 0, total = 0, ruled = 0;
        out << "# " << gateOf(path) << " agreement floors (tests/SstFloors.h): file, vectors that\n"
               "# must agree, vectors in the file. Ruled --known vectors are not counted.\n"
               "# Raise a row when the run reports RAISE; never lower one to make a gate pass.\n";
        for (const auto& [file, t] : sorted) {
            out << file << '\t' << t->agreed << '\t' << t->total << '\n';
            agreed += t->agreed; total += t->total; ruled += t->ruled;
        }
        std::printf("%s: %ld/%ld agree, %ld ruled — floors written to %s\n",
                    gateOf(path).c_str(), agreed, total, ruled, path.c_str());
        return bool(out);
    }

private:
    struct Row { long floor, total; };
    std::map<std::string, Row> rows_;
    std::string path_, gate_;
    bool loaded_ = false;
};

}  // namespace sstfloors
