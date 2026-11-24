// POM68K — Macintosh 68k emulator
// VERHILLE Arnaud — Copyright (C) 2026 — MIT (see LICENSE-MIT)
//
// SingleStepTests/680x0 68000 harness — the Moira-config gate (M4.5).
// Validates our vendored Moira build (PRECISE_TIMING, address errors, no
// Musashi mimicry) against 1M+ single-instruction vectors on a bare flat
// 16 MB bus: registers, SR, USP/SSP, PC + prefetch queue, touched RAM and
// exact cycle counts. Same hand-rolled JSON scanner as POMIIGS.
// This JSON format is the planned exchange format for the 68030 oracle
// phase (TODO.md § Phase 2) — this harness is its 68000 prototype.
//
//   https://github.com/SingleStepTests/680x0  (68000/v1, fetch_sst_68000.sh)
//
// Vector semantics: `pc` = address of the instruction about to execute,
// `prefetch[0]` = IRD (opcode), `prefetch[1]` = IRC; at instruction
// boundary Moira holds pc0 = pc = instr address (MoiraDataflow prefetch()).
// `length` = bus cycles including any group-0 exception processing.
//
// Usage: sst68000 <dir> [--max N] [--only NAME,..] [--skip NAME,..]
//                 [--no-cycles] [--verbose] [--examples K]
//                 [--no-transactions] [--known FILE]
//                 [--floors FILE | --write-floors FILE]
// Exit 0 = all matched / no data (soft skip); 1 = mismatch; 2 = usage.
// With --floors (tests/SstFloors.h) a whole-corpus run is judged against a
// per-file agreement floor instead: 1 = a file below its floor, a floors row
// out of step with the corpus, or a violated --known rule.
//
// Bus cycles are part of the verdict since 2026-11-06. The corpus records
// every bus cycle of each vector — kind, function code, address, size, value,
// and the idle cycles in between — and two cores can agree on registers and
// on the cycle TOTAL while disagreeing on the order of a prefetch and an
// internal delay, or on which half of a long goes first. A vector passes only
// if its bus sequence and each cycle's start match too. --no-transactions
// restores the state-only comparison (it is what 68010/020 experiments on
// this corpus need).
//
// --known FILE holds `@SR` lines only (tests/SstKnown.h): vectors on which
// the core follows the manual's condition codes against the corpus. Each is
// compared in full with the manual's SR in place of the corpus's, and
// counted as ruled; tests/sst68000.known says which ruling and why.

#include "MoiraCompat.h"
#include "SstFloors.h"
#include "SstKnown.h"

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <set>
#include <string>
#include <vector>

namespace {

struct RamCell { uint32_t addr; uint8_t val; };

// One bus cycle as the corpus records it. kind: 'r' read, 'w' write, 't' the
// indivisible TAS read-modify-write; `start` is its first cycle, counted from
// the instruction's own. Idle ('n') entries only advance `start`.
struct BusCycle {
    char kind; char size; uint8_t fc; uint32_t addr; uint32_t val; long start;
};

bool gDump = false;
bool gTxOn = true;                     // cleared by --no-transactions

// Known-bad vectors in SST/680x0 v1 (upstream issue #4: byte op corrupts the
// upper register bytes in the expected data; our computed values match the
// corrections proposed there).
inline bool knownBad(const std::string& name) {
    return name == "e502 [ASL.b Q, D2] 1583" || name == "e502 [ASL.b Q, D2] 1761";
}

struct CpuState {
    uint32_t d[8] = {}, a[7] = {};
    uint32_t usp = 0, ssp = 0, pc = 0;
    uint16_t sr = 0, prefetch[2] = {};
    std::vector<RamCell> ram;
};

struct Vector {
    std::string name;
    CpuState initial, fin;
    long length = -1;
    std::vector<BusCycle> bus;
};

// ── Flat 16 MB test bus (no Mac hardware, no contention) ─────────────────
class TestCpu : public MoiraCompat {
public:
    TestCpu() : mem(1 << 24, 0) { setModel(moira::CPUModel::M68000); }
    std::vector<uint8_t> mem;
    // Every byte the core wrote, zeroed after the vector: a stray write
    // outside the corpus's final list must not reach the next vector (the
    // floors count each file on its own; tests/SstFloors.h).
    std::vector<uint32_t> dirty;
    // --transactions: every bus cycle of the vector in flight, stamped with
    // the clock at the callback.
    bool record = false;
    mutable std::vector<BusCycle> bus;

private:
    void note(char kind, char size, moira::u32 a, uint32_t v) const {
        if (record)
            bus.push_back({kind, size, uint8_t(readFC()), a & 0xFFFFFF, v,
                           long(getClock())});
    }
    moira::u8  read8(moira::u32 a) const override {
        const moira::u8 v = mem[a & 0xFFFFFF];
        note('r', 'b', a, v);
        return v;
    }
    moira::u16 read16(moira::u32 a) const override {
        const moira::u16 v =
            moira::u16((mem[a & 0xFFFFFF] << 8) | mem[(a + 1) & 0xFFFFFF]);
        note('r', 'w', a, v);
        return v;
    }
    void write8(moira::u32 a, moira::u8 v) const override {
        note('w', 'b', a, v);
        auto& self = *const_cast<TestCpu*>(this);
        self.mem[a & 0xFFFFFF] = v;
        self.dirty.push_back(a & 0xFFFFFF);
    }
    void write16(moira::u32 a, moira::u16 v) const override {
        note('w', 'w', a, v);
        auto& self = *const_cast<TestCpu*>(this);
        self.mem[a & 0xFFFFFF] = moira::u8(v >> 8);
        self.mem[(a + 1) & 0xFFFFFF] = moira::u8(v);
        self.dirty.push_back(a & 0xFFFFFF);
        self.dirty.push_back((a + 1) & 0xFFFFFF);
    }
};

// ── JSON scanner (POMIIGS pattern — decimal values only) ─────────────────
inline void skipWs(const char*& p) { while (*p==' '||*p=='\t'||*p=='\n'||*p=='\r') ++p; }
inline bool eat(const char*& p, char c) { skipWs(p); if (*p==c) { ++p; return true; } return false; }
inline uint64_t parseUint(const char*& p) { skipWs(p); uint64_t v=0; while (*p>='0'&&*p<='9'){v=v*10u+uint64_t(*p-'0');++p;} return v; }
inline void skipString(const char*& p) { skipWs(p); if (*p!='"') return; ++p; while (*p&&*p!='"') ++p; if (*p=='"') ++p; }

void skipValue(const char*& p) {
    skipWs(p);
    if (*p=='"') { skipString(p); return; }
    if (*p=='['||*p=='{') {
        const char open=*p, close=(open=='[')?']':'}'; int depth=0;
        while (*p) { if (*p=='"'){skipString(p);continue;} if (*p==open)++depth; else if (*p==close){if(--depth==0){++p;return;}} ++p; }
        return;
    }
    while (*p&&*p!=','&&*p!='}'&&*p!=']') ++p;
}

void parseRam(const char*& p, std::vector<RamCell>& out) {
    out.clear(); eat(p,'['); skipWs(p);
    if (*p==']'){++p;return;}
    while (true) {
        eat(p,'['); const uint32_t a=uint32_t(parseUint(p)); eat(p,','); const uint32_t v=uint32_t(parseUint(p)); eat(p,']');
        out.push_back({a, uint8_t(v)});
        skipWs(p);
        if (*p==','){++p;continue;} if (*p==']'){++p;break;} break;
    }
}

// "transactions": [["n",2],["r",4,6,3076,".w",1657],["t",10,5,addr,".b",v],…]
void parseBus(const char*& p, std::vector<BusCycle>& out) {
    out.clear(); eat(p,'['); skipWs(p);
    if (*p==']'){++p;return;}
    long at = 0;
    while (true) {
        eat(p,'['); skipWs(p);
        char kind = 0;
        if (*p=='"') { kind = p[1]; skipString(p); }
        eat(p,','); const long cyc = long(parseUint(p));
        if (kind == 'n') {
            at += cyc;
        } else {
            BusCycle b{}; b.kind = kind; b.start = at;
            eat(p,','); b.fc = uint8_t(parseUint(p));
            eat(p,','); b.addr = uint32_t(parseUint(p)) & 0xFFFFFF;
            eat(p,','); skipWs(p); b.size = (*p=='"') ? p[2] : 'w'; skipString(p);
            eat(p,','); b.val = uint32_t(parseUint(p));
            out.push_back(b);
            at += cyc;
        }
        eat(p,']'); skipWs(p);
        if (*p==','){++p;continue;} if (*p==']'){++p;break;} break;
    }
}

void parsePrefetch(const char*& p, uint16_t out[2]) {
    eat(p,'['); out[0]=uint16_t(parseUint(p)); eat(p,','); out[1]=uint16_t(parseUint(p)); eat(p,']');
}

void parseState(const char*& p, CpuState& st) {
    eat(p,'{');
    while (true) {
        skipWs(p);
        if (*p=='}'){++p;break;}
        if (*p=='"') {
            ++p; const char* k=p; while (*p&&*p!='"') ++p; const size_t kl=size_t(p-k);
            if (*p=='"') ++p;
            eat(p,':');
            auto key = [&](const char* s){ return kl==std::strlen(s) && !std::strncmp(k,s,kl); };
            if      (kl==2 && k[0]=='d' && k[1]>='0'&&k[1]<='7') st.d[k[1]-'0'] = uint32_t(parseUint(p));
            else if (kl==2 && k[0]=='a' && k[1]>='0'&&k[1]<='6') st.a[k[1]-'0'] = uint32_t(parseUint(p));
            else if (key("usp"))      st.usp = uint32_t(parseUint(p));
            else if (key("ssp"))      st.ssp = uint32_t(parseUint(p));
            else if (key("sr"))       st.sr  = uint16_t(parseUint(p));
            else if (key("pc"))       st.pc  = uint32_t(parseUint(p));
            else if (key("prefetch")) parsePrefetch(p, st.prefetch);
            else if (key("ram"))      parseRam(p, st.ram);
            else skipValue(p);
        }
        skipWs(p);
        if (*p==','){++p;continue;} if (*p=='}'){++p;break;}
    }
}

bool parseVector(const char*& p, Vector& v) {
    skipWs(p); if (*p!='{') return false; ++p;
    v.name.clear(); v.length=-1;
    while (true) {
        skipWs(p);
        if (*p=='}'){++p;break;}
        if (*p=='"') {
            ++p; const char* k=p; while (*p&&*p!='"') ++p; const size_t kl=size_t(p-k); const char* key=k;
            if (*p=='"') ++p;
            eat(p,':');
            if (kl==4 && !std::strncmp(key,"name",4)) { skipWs(p); if (*p=='"'){++p;const char* s=p;while(*p&&*p!='"')++p;v.name.assign(s,size_t(p-s));if(*p=='"')++p;} }
            else if (kl==7 && !std::strncmp(key,"initial",7)) parseState(p, v.initial);
            else if (kl==5 && !std::strncmp(key,"final",5))   parseState(p, v.fin);
            else if (kl==6 && !std::strncmp(key,"length",6))  v.length = long(parseUint(p));
            else if (kl==12 && !std::strncmp(key,"transactions",12)) parseBus(p, v.bus);
            else skipValue(p);                                // transactions
        }
        skipWs(p);
        if (*p==','){++p;continue;} if (*p=='}'){++p;break;}
    }
    return true;
}

void loadState(TestCpu& cpu, const CpuState& s) {
    cpu.setSR(s.sr);                       // first: selects the active stack
    cpu.setISP(s.ssp);
    cpu.setUSP(s.usp);
    for (int i = 0; i < 8; i++) cpu.setD(i, s.d[i]);
    for (int i = 0; i < 7; i++) cpu.setA(i, s.a[i]);
    cpu.setPC(s.pc);
    cpu.setPC0(s.pc);
    cpu.setIRD(s.prefetch[0]);
    cpu.setIRC(s.prefetch[1]);
}

// Bus verdict for one vector. `seq`: same bus cycles in the same order (kind,
// function code, address, size, value). `timed`: each also starts on the
// corpus's cycle. Moira calls the host two cycles into a four-cycle bus cycle
// (read<>/write<>: SYNC(2), callback, SYNC(2)), hence kBusStamp.
struct TxVerdict { bool seq = false; bool timed = false; };
constexpr long kBusStamp = 2;

// [B] M68000PRM-1992 §2, §2.2.11: a PC-relative operand read is a program
// reference. The corpus records it in data space (FC 1/5); the core, ruled
// by the manual, reads it in program space (FC 2/6) — MoiraDataflow_cpp.h
// readM. On a vector whose source is PC-relative, a READ whose only
// difference is that FC pair is counted here and accepted; everything else
// about the cycle (kind, size, address, value, start) is still compared.
long gPcRelFcRuled = 0;

bool pcRelativeFcRuling(bool pcRelative, const BusCycle& e, const BusCycle& r) {
    return pcRelative && e.kind == 'r' && r.kind == 'r' &&
           (e.fc == 1 || e.fc == 5) && r.fc == e.fc + 1;
}

// The same ruling, seen in the group-0 frame of an address error on that
// read: the status word's FC field (bits 2-0) is 1/5 in the corpus and 2/6
// here, every other bit equal. Applies to the low byte of that word, on
// the bus write and in final RAM. 1→2 and 5→6 both flip bits 1-0.
bool pcRelativeStatusRuling(bool pcRelative, unsigned want, unsigned got) {
    return pcRelative && ((want & 7) == 1 || (want & 7) == 5) &&
           (want ^ got) == 3;
}

TxVerdict compareBus(const std::vector<BusCycle>& want,
                     const std::vector<BusCycle>& got, long c0,
                     bool pcRelative) {
    TxVerdict out;
    bool timed = true;
    size_t j = 0;
    for (const BusCycle& e : want) {
        if (j >= got.size()) return out;
        const BusCycle& r = got[j];
        if (e.kind == 't') {           // TAS: one indivisible read + write
            if (j + 1 >= got.size()) return out;
            const BusCycle& w = got[j + 1];
            if (r.kind != 'r' || w.kind != 'w' || r.size != 'b' ||
                w.size != 'b' || r.addr != e.addr || w.addr != e.addr ||
                r.fc != e.fc)
                return out;
            j += 2;
        } else {
            const uint32_t mask = e.size == 'b' ? 0xFFu : 0xFFFFu;
            const bool ruled = r.fc != e.fc && pcRelativeFcRuling(pcRelative, e, r);
            const bool ruledWord = e.kind == 'w' && (r.val & mask) != (e.val & mask) &&
                pcRelativeStatusRuling(pcRelative, e.val & mask, r.val & mask);
            if (r.kind != e.kind || r.size != e.size || r.addr != e.addr ||
                (r.fc != e.fc && !ruled) ||
                ((r.val & mask) != (e.val & mask) && !ruledWord))
                return out;
            if (ruled || ruledWord) gPcRelFcRuled++;
            j += 1;
        }
        if (r.start - c0 - kBusStamp != e.start) timed = false;
    }
    if (j != got.size()) return out;
    out.seq = true;
    out.timed = timed;
    return out;
}

TxVerdict gLastTx;

bool runVector(TestCpu& cpu, const Vector& v, bool checkCycles, std::string& why) {
    cpu.reset();                           // clears STOP/halt state from prior vectors
    loadState(cpu, v.initial);
    for (const RamCell& c : v.initial.ram) cpu.mem[c.addr & 0xFFFFFF] = c.val;

    cpu.bus.clear();
    cpu.record = gTxOn;
    const moira::i64 c0 = cpu.getClock();
    cpu.execute();                         // one instruction (+ its exception, if any)
    const long cyc = long(cpu.getClock() - c0);
    cpu.record = false;
    if (gTxOn) gLastTx = compareBus(v.bus, cpu.bus, long(c0),
                                    v.name.find(", PC") != std::string::npos);
    if (gTxOn && gDump && !(gLastTx.seq && gLastTx.timed)) {
        std::printf("      ~~ %s\n         want:", v.name.c_str());
        for (const BusCycle& b : v.bus)
            std::printf(" %c%c fc%d %06X=%04X @%ld", b.kind, b.size, b.fc, b.addr, b.val, b.start);
        std::printf("\n         got: ");
        for (const BusCycle& b : cpu.bus)
            std::printf(" %c%c fc%d %06X=%04X @%ld", b.kind, b.size, b.fc, b.addr, b.val, b.start - long(c0) - kBusStamp);
        std::printf("\n");
    }

    bool ok = true; char buf[256];
    auto fail = [&](const char* what, unsigned long got, unsigned long want) {
        if (ok) { std::snprintf(buf,sizeof buf,"%s got $%lX want $%lX",what,got,want); why=buf; } ok=false;
    };
    for (int i = 0; i < 8; i++) if (cpu.getD(i) != v.fin.d[i]) fail("D?", cpu.getD(i), v.fin.d[i]);
    for (int i = 0; i < 7; i++) if (cpu.getA(i) != v.fin.a[i]) fail("A?", cpu.getA(i), v.fin.a[i]);
    if (cpu.getUSP() != v.fin.usp) fail("USP", cpu.getUSP(), v.fin.usp);
    if (cpu.getISP() != v.fin.ssp) fail("SSP", cpu.getISP(), v.fin.ssp);
    if (cpu.getSR()  != v.fin.sr)  fail("SR",  cpu.getSR(),  v.fin.sr);
    if (cpu.getPC0() != v.fin.pc)  fail("PC",  cpu.getPC0(), v.fin.pc);
    if (cpu.getIRD() != v.fin.prefetch[0]) fail("IRD", cpu.getIRD(), v.fin.prefetch[0]);
    if (cpu.getIRC() != v.fin.prefetch[1]) fail("IRC", cpu.getIRC(), v.fin.prefetch[1]);
    if (checkCycles && v.length >= 0 && cyc != v.length) {
        if (ok) { std::snprintf(buf,sizeof buf,"cycles got %ld want %ld",cyc,v.length); why=buf; } ok=false;
    }
    if (gTxOn && !(gLastTx.seq && gLastTx.timed)) {
        if (ok) why = gLastTx.seq ? "bus cycles start on other cycles than the corpus's"
                                  : "bus cycles differ in order or content";
        ok=false;
    }
    const bool pcRelative = v.name.find(", PC") != std::string::npos;
    for (const RamCell& c : v.fin.ram) {
        const uint8_t got = cpu.mem[c.addr & 0xFFFFFF];
        if (got != c.val && (c.addr & 1) &&
            pcRelativeStatusRuling(pcRelative, c.val, got)) continue;
        if (got != c.val) { if (ok){std::snprintf(buf,sizeof buf,"RAM[$%06X] got $%02X want $%02X",c.addr,got,c.val);why=buf;} ok=false; }
    }
    if (!ok && gDump) {
        std::printf("      == %s\n", v.name.c_str());
        std::printf("         PC got %06X want %06X | SR got %04X want %04X | cyc got %ld want %ld\n",
                    cpu.getPC0(), v.fin.pc, cpu.getSR(), v.fin.sr, cyc, v.length);
        for (int i=0;i<8;i++) if (cpu.getD(i)!=v.fin.d[i]) std::printf("         D%d got %08X want %08X\n", i, cpu.getD(i), v.fin.d[i]);
        for (int i=0;i<7;i++) if (cpu.getA(i)!=v.fin.a[i]) std::printf("         A%d got %08X want %08X (init %08X)\n", i, cpu.getA(i), v.fin.a[i], v.initial.a[i]);
        if (cpu.getUSP()!=v.fin.usp) std::printf("         USP got %08X want %08X\n", cpu.getUSP(), v.fin.usp);
        if (cpu.getISP()!=v.fin.ssp) std::printf("         SSP got %08X want %08X (init %08X)\n", cpu.getISP(), v.fin.ssp, v.initial.ssp);
        for (const RamCell& c : v.fin.ram) {
            const uint8_t got = cpu.mem[c.addr & 0xFFFFFF];
            if (got != c.val) std::printf("         RAM[%06X] got %02X want %02X\n", c.addr, got, c.val);
        }
    }
    for (const RamCell& c : v.initial.ram) cpu.mem[c.addr & 0xFFFFFF] = 0;
    for (const RamCell& c : v.fin.ram)     cpu.mem[c.addr & 0xFFFFFF] = 0;
    for (uint32_t a : cpu.dirty) cpu.mem[a] = 0;
    cpu.dirty.clear();
    return ok;
}

std::set<std::string> parseNameList(const char* s) {
    std::set<std::string> out;
    while (s && *s) {
        while (*s==','||*s==' ') ++s;
        const char* b=s; while (*s&&*s!=',') ++s;
        if (s>b) out.insert(std::string(b,size_t(s-b)));
    }
    return out;
}

}  // namespace

int main(int argc, char** argv) {
    if (argc < 2) { std::fprintf(stderr,"usage: %s <dir> [--max N] [--only NAME,..] [--skip NAME,..] [--no-cycles] [--no-transactions] [--known FILE] [--floors FILE | --write-floors FILE] [--verbose] [--examples K]\n",argv[0]); return 2; }
    const std::string dir = argv[1];
    long maxPerFile=-1; int examples=3; bool verbose=false, checkCycles=true;
    std::set<std::string> only, skip;
    sstknown::List known;
    sstfloors::Floors floors;               // --floors FILE (tests/SstFloors.h)
    std::string writeFloors;                // --write-floors FILE
    for (int i=2;i<argc;++i){ std::string a=argv[i];
        if      (a=="--max"&&i+1<argc) maxPerFile=std::strtol(argv[++i],nullptr,10);
        else if (a=="--examples"&&i+1<argc) examples=int(std::strtol(argv[++i],nullptr,10));
        else if (a=="--only"&&i+1<argc) only=parseNameList(argv[++i]);
        else if (a=="--skip"&&i+1<argc) skip=parseNameList(argv[++i]);
        else if (a=="--no-cycles") checkCycles=false;
        else if (a=="--verbose") verbose=true;
        else if (a=="--dump") gDump=true;
        else if (a=="--no-transactions") gTxOn=false;
        else if (a=="--known"&&i+1<argc) {
            if (!known.load(argv[++i]) || !known.onlySrRules()) { std::fprintf(stderr,"cannot load --known '%s' (@SR lines only)\n",argv[i]); return 2; }
        }
        else if (a=="--floors"&&i+1<argc) { if (!floors.load(argv[++i])) return 2; }
        else if (a=="--write-floors"&&i+1<argc) writeFloors=argv[++i];
        else { std::fprintf(stderr,"unknown arg '%s'\n",a.c_str()); return 2; }
    }

    const bool judged = floors.active() || !writeFloors.empty();
    if (judged && (floors.active() == !writeFloors.empty() || !only.empty() || !skip.empty() || maxPerFile>=0)) {
        std::fprintf(stderr,"--floors / --write-floors: one of them, on a whole-corpus run\n"); return 2;
    }

    namespace fs = std::filesystem;
    if (!fs::exists(dir)||!fs::is_directory(dir)) { std::fprintf(stderr,"SKIP: [sst68000] no data at '%s' (run tests/fetch_sst_68000.sh)\n",dir.c_str()); return 0; }
    std::vector<fs::path> files;
    for (const auto& e : fs::directory_iterator(dir)) if (e.is_regular_file()&&e.path().extension()==".json") files.push_back(e.path());
    std::sort(files.begin(),files.end());
    if (files.empty()) { std::fprintf(stderr,"SKIP: [sst68000] '%s' holds no .json\n",dir.c_str()); return 0; }

    TestCpu cpu;
    std::printf("[sst68000] dir=%s files=%zu cycles=%s bus=%s\n", dir.c_str(), files.size(), checkCycles?"on":"off", gTxOn?"on":"off");

    long grandTotal=0, grandPass=0, grandRuled=0, filesRun=0; bool anyFail=false;
    long txSeqAll=0, txTimedAll=0, violations=0;
    std::vector<sstfloors::FileTally> tallies;
    for (const fs::path& f : files) {
        const std::string stem=f.stem().string();
        if (!only.empty()&&!only.count(stem)) continue;
        if (skip.count(stem)) { std::printf("  %-12s : SKIP\n",stem.c_str()); continue; }
        std::ifstream in(f,std::ios::binary);
        if (!in){ std::fprintf(stderr,"  %-12s: cannot open\n",stem.c_str()); anyFail=true; ++violations; continue; }
        std::string buf((std::istreambuf_iterator<char>(in)),std::istreambuf_iterator<char>());
        const char* p=buf.c_str(); eat(p,'[');
        long total=0,passed=0,ruled=0,broken=0; std::vector<std::pair<std::string,std::string>> firstFew; Vector v;
        long txSeq=0, txTimed=0;
        while (true) {
            skipWs(p); if (*p==']'||*p=='\0') break;
            if (!parseVector(p,v)) break;
            if (knownBad(v.name)) { skipWs(p); if (*p==','){++p;} continue; }
            std::string why; bool ruleOk=true;
            const auto manualSr = known.srRule(v.name);
            if (manualSr) {
                known.listed(v.name);
                const unsigned delta = unsigned(v.fin.sr ^ *manualSr);
                ruleOk = delta && !(delta & ~0x11u);  // X and C only
                if (ruleOk) v.fin.sr = *manualSr;
            }
            bool ok=runVector(cpu,v,checkCycles,why);
            if (!ruleOk) { ok=false; ++broken; why="@SR must differ from the corpus in X/C only (stale or invalid rule)"; }
            ++total; if (ok&&manualSr) ++ruled; else if (ok) ++passed; else if (int(firstFew.size())<examples || (judged && !ruleOk)) firstFew.push_back({v.name,why});
            if (gTxOn) { if (gLastTx.seq) ++txSeq; if (gLastTx.timed) ++txTimed; }
            if (maxPerFile>0&&total>=maxPerFile) break;
            skipWs(p); if (*p==','){++p;continue;} if (*p==']'){++p;break;}
        }
        ++filesRun; grandTotal+=total; grandPass+=passed; grandRuled+=ruled;
        const sstfloors::FileTally tally{stem,total,passed,ruled,broken};
        tallies.push_back(tally);
        const bool fileOk = floors.active() ? floors.fileOk(tally)
                          : judged ? broken==0 : (passed+ruled==total);
        if (!fileOk) anyFail=true;
        std::printf("  %-12s : %6ld/%-6ld %s%s\n",stem.c_str(),passed,total,
                    floors.active()?floors.mark(tally).c_str():fileOk?"OK":"FAIL",
                    ruled?(" (+"+std::to_string(ruled)+" ruled)").c_str():"");
        if (gTxOn) {
            txSeqAll+=txSeq; txTimedAll+=txTimed;
            if (verbose || txSeq!=total || txTimed!=total)
                std::printf("  %-12s   bus order %6ld/%-6ld timed %6ld/%-6ld\n","",txSeq,total,txTimed,total);
        }
        if (!fileOk||verbose) for (auto& mm:firstFew) std::printf("        x \"%s\"  %s\n",mm.first.c_str(),mm.second.c_str());
    }
    // A listed vector the corpus no longer holds fails a whole-corpus run.
    if (known.active() && only.empty() && skip.empty() && maxPerFile<0 && filesRun>0) {
        const long missing = known.reportMissing(examples);
        if (missing) { anyFail=true; violations+=missing; }
    }
    const std::string ruledNote = !known.active() ? std::string()
        : " (+" + std::to_string(grandRuled) + " ruled by --known)";
    if (!judged)
    std::printf("[sst68000] %s: %ld/%ld%s across %ld file(s)%s\n", anyFail?"FAIL":"OK", grandPass, grandTotal, ruledNote.c_str(), filesRun, anyFail?"  <<< MISMATCH":"");
    if (gTxOn)
        std::printf("[sst68000] bus cycles: order %ld/%ld, timing %ld/%ld; "
                    "%ld PC-relative operand read(s) in program space where "
                    "the corpus says data (M68000PRM §2.2.11)\n",
                    txSeqAll, grandTotal, txTimedAll, grandTotal, gPcRelFcRuled);
    if (filesRun==0) { std::fprintf(stderr,"SKIP: [sst68000] no files matched\n"); return 0; }
    if (!writeFloors.empty()) return sstfloors::Floors::write(writeFloors, tallies, violations) ? 0 : 1;
    if (floors.active()) return floors.judge(tallies, violations) ? 0 : 1;
    return anyFail?1:0;
}
