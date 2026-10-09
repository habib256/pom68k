// POM68K — Macintosh 68k emulator
// VERHILLE Arnaud — Copyright (C) 2026 — GPLv3 (see LICENSE)
//
// DebugHistory — see DebugHistory.h for the format.

#include "DebugHistory.h"

#include <cinttypes>
#include <cstdio>
#include <memory>

namespace pom68k::dbg {

bool exportHistory(const std::string& path, const IdentityNotes& identity,
                   const std::string& model, const Target& target,
                   std::string& why) {
    if (path.empty()) {
        why = "Export : chemin de fichier attendu";
        return false;
    }
    std::unique_ptr<std::FILE, int (*)(std::FILE*)> f(
        std::fopen(path.c_str(), "w"), &std::fclose);
    if (!f) {
        why = "Export impossible : " + path;
        return false;
    }
    std::FILE* out = f.get();
    std::fprintf(out, "# POM68K debugger history v1\n");
    for (const auto& [key, value] : identity)
        std::fprintf(out, "# %s %s\n", key.c_str(), value.c_str());
    std::fprintf(out, "# model %s\n", model.c_str());

    std::vector<HistoryEntry> entries;
    target.history(entries, 0);
    const std::uint64_t recorded = target.historyRecorded();
    std::fprintf(out, "# instructions %" PRIu64 " recorded, %zu kept, %" PRIu64
                      " dropped\n",
                 recorded, entries.size(), recorded - entries.size());
    for (const HistoryEntry& e : entries) {
        std::fprintf(out, "I %" PRId64 " %08X %04X %04X", e.clock, e.pc,
                     e.opcode, e.sr);
        for (std::uint32_t v : e.d) std::fprintf(out, " %08X", v);
        for (std::uint32_t v : e.a) std::fprintf(out, " %08X", v);
        std::fputc('\n', out);
    }

    std::vector<TrapEntry> traps;
    target.traps(traps, 0);
    const std::uint64_t trapsRecorded = target.trapsRecorded();
    std::fprintf(out, "# exceptions %" PRIu64 " recorded, %zu kept, %" PRIu64
                      " dropped\n",
                 trapsRecorded, traps.size(), trapsRecorded - traps.size());
    for (const TrapEntry& t : traps)
        std::fprintf(out, "E %" PRId64 " %02X %08X %04X\n", t.clock, t.vector,
                     t.stackedPc, t.trapWord);
    if (std::ferror(out) || std::fflush(out) != 0) {
        why = "Export : écriture incomplète de " + path;
        return false;
    }
    return true;
}

} // namespace pom68k::dbg
