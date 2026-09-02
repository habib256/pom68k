// POM68K — Macintosh 68k emulator
// VERHILLE Arnaud — Copyright (C) 2026 — GPLv3 (see LICENSE)
//
// The GUI ↔ machine-thread save/restore slot, lifted out of the former
// monolithic GUI entry point unchanged (2026-08-09) so it can be reached by
// a gate. This carries no GUI dependency and never did — the ImGui row that
// drives it now lives in `GuiShell.cpp`.

#pragma once

#include <cerrno>
#include <cstring>

#include "AtomicReplace.h"
#include "SaveStateMachines.h"

#include <cstdio>
#include <mutex>
#include <string>
#include <vector>

// ── Save states (TODO § C GUI wiring) ───────────────────────────────────
// Shared plumbing embedded in each machine-thread struct: the GUI queues a
// request; the MACHINE thread performs the save/load between two quanta
// (the Cmd::CpuEngine precedent — a restore replaces the whole tree, so it
// must land between two instructions, never mid-quantum from the GUI
// thread) and posts a one-line outcome the machine window displays. The
// run function fills in the profile tag and the state-file path (tagged
// like the .pram file, so states pair with their boot volume).
struct SaveStateSlot {
    pom68k::SnapMachine kind{};        // 0 = profile not wired

    // The state-file path travels under the slot's own mutex, the
    // `requestInsertFloppy` convention of `MachineHost.h`: the machine thread
    // must never read a std::string the GUI thread may still be assigning.
    // It was a plain public member until TSan caught the GUI frame
    // re-assigning it inside `gui_smoke_test` while the machine thread was in
    // apply() (nightly run 33605191940, 2026-09-02). DEV.md § 6: GUI → machine
    // state crosses the boundary by value with the request, never as a shared
    // mutable member. The lock is taken once per request and once per tick
    // that has one, never inside a quantum.
    void setPath(std::string p) {
        std::lock_guard<std::mutex> l(mu_);
        path_ = std::move(p);
    }
    std::string path() {
        std::lock_guard<std::mutex> l(mu_);
        return path_;
    }

    void request(bool load) {
        std::lock_guard<std::mutex> l(mu_);
        pending_ |= load ? 2 : 1;
    }
    std::string message() {
        std::lock_guard<std::mutex> l(mu_);
        return message_;
    }

    // Machine-thread side, called from applyCmds() (between quanta).
    // Returns what actually happened (bit 0 = saved, bit 1 = restored) so
    // single-threaded callers (the Plus loop) can resync their frame clock
    // after a restore.
    template <class Mem, class Cpu>
    int apply(Mem& mem, Cpu& cpu) {
        int p;
        std::string path;                  // snapshot: the GUI owns the member
        {
            std::lock_guard<std::mutex> l(mu_);
            p = pending_;
            pending_ = 0;
            path = path_;
        }
        if (!p) return 0;
        int done = 0;
        if (kind == pom68k::SnapMachine{} || path.empty()) {
            post("Save states: profil non câblé");
            return 0;
        }
        if (p & 1) {
            std::vector<uint8_t> blob;
            pom68k::save(mem, cpu, kind, blob);
            // Atomic temp+rename, the floppy write-back convention: a crash
            // mid-write must never leave a truncated state file behind.
            const std::string tmp = path + ".tmp";
            // Report errno with the refusal. `gui_smoke_test` failed exactly
            // once in a 228-gate parallel run on 2026-08-27 with "rename
            // impossible" and nothing else — 50 further runs, loaded and
            // idle, never reproduced it. A guard that cannot say WHY it
            // refused makes its own flake un-diagnosable (method rule R5).
            const auto why = [] {
                const char* e = std::strerror(errno);
                return std::string(e ? e : "?");
            };
            errno = 0;
            std::FILE* f = std::fopen(tmp.c_str(), "wb");
            if (!f || std::fwrite(blob.data(), 1, blob.size(), f) != blob.size()) {
                const std::string reason = why();
                if (f) std::fclose(f);
                std::remove(tmp.c_str());
                post("État NON sauvé: écriture impossible (" + tmp + ") — " + reason);
            } else {
                errno = 0;
                std::fclose(f);
                if (!atomicReplaceFile(tmp, path)) {
                    const std::string reason = why();
                    std::remove(tmp.c_str());
                    post("État NON sauvé: rename impossible (" + path + ") — "
                         + reason);
                } else {
                    post("État sauvé → " + path + " ("
                         + std::to_string((blob.size() + 512) / 1024) + " Ko)");
                    done |= 1;
                }
            }
        }
        if (p & 2) {
            std::vector<uint8_t> blob;
            std::FILE* f = std::fopen(path.c_str(), "rb");
            if (!f) {
                post("Aucun état à restaurer (" + path + ")");
            } else {
                std::fseek(f, 0, SEEK_END);
                long n = std::ftell(f);
                std::fseek(f, 0, SEEK_SET);
                blob.resize(n > 0 ? size_t(n) : 0);
                size_t got = blob.empty() ? 0
                           : std::fread(blob.data(), 1, blob.size(), f);
                std::fclose(f);
                std::string err;
                if (got != blob.size()) {
                    post("État NON restauré: lecture tronquée (" + path + ")");
                } else if (!pom68k::load(mem, cpu, kind,
                                         blob.data(), blob.size(), err)) {
                    // A refused snapshot leaves the machine untouched — the
                    // reason (profile/ROM/RAM mismatch, corruption) is
                    // load()'s own explanation.
                    post("État NON restauré: " + err);
                } else {
                    post(err.empty() ? "État restauré ← " + path
                                     : "État restauré (" + err + ")");
                    done |= 2;
                }
            }
        }
        return done;
    }

private:
    void post(std::string m) {
        std::lock_guard<std::mutex> l(mu_);
        message_ = std::move(m);
        std::printf("SaveState: %s\n", message_.c_str());
    }
    std::mutex mu_;
    int pending_ = 0;                  // bit 0 = save, bit 1 = load
    std::string message_;
    std::string path_;
};
