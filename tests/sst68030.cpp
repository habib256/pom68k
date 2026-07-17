// POM68K — Macintosh 68k emulator
// VERHILLE Arnaud — Copyright (C) 2026 — GPLv3 (see LICENSE)
//
// SST030 replay harness — the error signal of the Phase-2 improve loop
// (TODO.md § Phase 2, O4). Replays oracle-agreed 68030 vectors produced by
// oracle/fuzz/fuzz030.py against Moira (Model::M68030) on the same flat
// 16 MB bus as tests/sst68000.cpp. Differences vs the 68000 harness:
//
//   * SST030 format (oracle/fuzz/sst030.py): a7 explicit + usp/isp/msp,
//     control regs (vbr sfc dfc cacr caar), MMU regs (crp srp tc tt0 tt1
//     mmusr, 64-bit decimals for the root pointers), "stopped"; no
//     prefetch field — the pipe is refilled from RAM at pc.
//   * cycles are ADVISORY (functional accuracy is the LC II target):
//     compared only with --cycles.
//   * MMU registers are loaded and compared (O4). Since the bus-translation
//     slice (O4 slice 3) vectors with address translation enabled (initial
//     tc bit 31 = E) replay first-class — TT/ATC/table walks, U/M updates
//     and $A/$B bus-fault frames included. --skip-translation restores the
//     old skip for debugging.
//   * O5: FPU state. Vectors carrying "fp0".."fp7" (each a list of exactly
//     3 u32 words: [0]=(sign|15-bit exp)<<16, [1]=mantissa 63..32,
//     [2]=mantissa 31..0 — CONTRACT with oracle/fuzz, fixed) plus scalars
//     "fpcr"/"fpsr"/"fpiar" replay with a 68882 attached (the LC II PDS
//     FPU); vectors without the keys replay exactly as before, FPU
//     detached => F2xx opcodes take the F-line trap.
//
// Usage: sst68030 <dir> [--max N] [--only NAME,..] [--skip NAME,..]
//                 [--cycles] [--skip-translation] [--verbose] [--dump] [--examples K]
// Exit 0 = all matched / no data (soft skip); 1 = mismatch; 2 = usage.

#include "Moira.h"

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

bool gDump = false;

struct CpuState {
    uint32_t d[8] = {}, a[8] = {};
    uint32_t usp = 0, isp = 0, msp = 0, pc = 0;
    uint16_t sr = 0;
    uint32_t vbr = 0, sfc = 0, dfc = 0, cacr = 0, caar = 0;
    uint64_t crp = 0, srp = 0;
    uint32_t tc = 0, tt0 = 0, tt1 = 0;
    uint16_t mmusr = 0;
    uint8_t  stopped = 0;
    // O5: 68882 state; hasFp is set as soon as one FP key appears
    uint32_t fp[8][3] = {};
    uint32_t fpcr = 0, fpsr = 0, fpiar = 0;
    bool hasFp = false;
    std::vector<RamCell> ram;

    // O4 slice 3: translation-enabled vectors replay first-class; the
    // predicate only serves the --skip-translation debugging flag.
    bool needsTranslation() const { return (tc >> 31) & 1; }
};

struct Vector {
    std::string name;
    CpuState initial, fin;
    long length = -1;
};

// ── Flat 16 MB test bus (no Mac hardware, no contention) ─────────────────
class TestCpu : public moira::Moira {
public:
    TestCpu() : mem(1 << 24, 0) { setModel(moira::Model::M68030); }
    std::vector<uint8_t> mem;

private:
    moira::u8  read8(moira::u32 a) const override { return mem[a & 0xFFFFFF]; }
    moira::u16 read16(moira::u32 a) const override {
        return moira::u16((mem[a & 0xFFFFFF] << 8) | mem[(a + 1) & 0xFFFFFF]);
    }
    void write8(moira::u32 a, moira::u8 v) const override {
        const_cast<TestCpu*>(this)->mem[a & 0xFFFFFF] = v;
    }
    void write16(moira::u32 a, moira::u16 v) const override {
        write8(a, moira::u8(v >> 8)); write8(a + 1, moira::u8(v));
    }
};

// ── JSON scanner (same conventions as sst68000.cpp — decimal only) ───────
inline void skipWs(const char*& p) { while (*p==' '||*p=='\t'||*p=='\n'||*p=='\r') ++p; }
inline bool eat(const char*& p, char c) { skipWs(p); if (*p==c) { ++p; return true; } return false; }
// Accepts an optional '-' (the fuzzer emits "length":-1 when the oracle
// could not step); negative values two's-complement into the uint64.
inline uint64_t parseUint(const char*& p) { skipWs(p); bool neg=false; if (*p=='-'){neg=true;++p;} uint64_t v=0; while (*p>='0'&&*p<='9'){v=v*10u+uint64_t(*p-'0');++p;} return neg?~v+1u:v; }
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

// O5: "fpN": [w0, w1, w2] — exactly 3 u32 words (see the format contract
// in the header comment)
void parseFpReg(const char*& p, uint32_t w[3]) {
    eat(p,'[');
    for (int i = 0; i < 3; i++) {
        w[i] = uint32_t(parseUint(p));
        skipWs(p);
        if (*p==',') ++p;
    }
    skipWs(p);
    if (*p==']') ++p;
}

void parseState(const char*& p, CpuState& st) {
    eat(p,'{');
    while (true) {
        skipWs(p);
        if (*p=='}'){++p;break;}
        if (*p=='"') {
            ++p; const char* k=p; while (*p&&*p!='"') ++p; const size_t kl=size_t(p-k);
            if (*p=='"') ++p; eat(p,':');
            auto key = [&](const char* s){ return kl==std::strlen(s) && !std::strncmp(k,s,kl); };
            if      (kl==2 && k[0]=='d' && k[1]>='0'&&k[1]<='7') st.d[k[1]-'0'] = uint32_t(parseUint(p));
            else if (kl==2 && k[0]=='a' && k[1]>='0'&&k[1]<='7') st.a[k[1]-'0'] = uint32_t(parseUint(p));
            else if (key("usp"))     st.usp   = uint32_t(parseUint(p));
            else if (key("isp"))     st.isp   = uint32_t(parseUint(p));
            else if (key("msp"))     st.msp   = uint32_t(parseUint(p));
            else if (key("sr"))      st.sr    = uint16_t(parseUint(p));
            else if (key("pc"))      st.pc    = uint32_t(parseUint(p));
            else if (key("vbr"))     st.vbr   = uint32_t(parseUint(p));
            else if (key("sfc"))     st.sfc   = uint32_t(parseUint(p));
            else if (key("dfc"))     st.dfc   = uint32_t(parseUint(p));
            else if (key("cacr"))    st.cacr  = uint32_t(parseUint(p));
            else if (key("caar"))    st.caar  = uint32_t(parseUint(p));
            else if (key("crp"))     st.crp   = parseUint(p);
            else if (key("srp"))     st.srp   = parseUint(p);
            else if (key("tc"))      st.tc    = uint32_t(parseUint(p));
            else if (key("tt0"))     st.tt0   = uint32_t(parseUint(p));
            else if (key("tt1"))     st.tt1   = uint32_t(parseUint(p));
            else if (key("mmusr"))   st.mmusr = uint16_t(parseUint(p));
            else if (key("stopped")) st.stopped = uint8_t(parseUint(p));
            else if (kl==3 && k[0]=='f' && k[1]=='p' && k[2]>='0'&&k[2]<='7') { parseFpReg(p, st.fp[k[2]-'0']); st.hasFp = true; }
            else if (key("fpcr"))    { st.fpcr  = uint32_t(parseUint(p)); st.hasFp = true; }
            else if (key("fpsr"))    { st.fpsr  = uint32_t(parseUint(p)); st.hasFp = true; }
            else if (key("fpiar"))   { st.fpiar = uint32_t(parseUint(p)); st.hasFp = true; }
            else if (key("ram"))     parseRam(p, st.ram);
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
            if (*p=='"') ++p; eat(p,':');
            if (kl==4 && !std::strncmp(key,"name",4)) { skipWs(p); if (*p=='"'){++p;const char* s=p;while(*p&&*p!='"')++p;v.name.assign(s,size_t(p-s));if(*p=='"')++p;} }
            else if (kl==7 && !std::strncmp(key,"initial",7)) parseState(p, v.initial);
            else if (kl==5 && !std::strncmp(key,"final",5))   parseState(p, v.fin);
            else if (kl==6 && !std::strncmp(key,"length",6))  v.length = long(parseUint(p));
            else skipValue(p);
        }
        skipWs(p);
        if (*p==','){++p;continue;} if (*p=='}'){++p;break;}
    }
    return true;
}

void loadState(TestCpu& cpu, const CpuState& s) {
    cpu.setSR(s.sr);                       // first: selects the active stack
    cpu.setISP(s.isp);
    cpu.setMSP(s.msp);
    cpu.setUSP(s.usp);
    for (int i = 0; i < 8; i++) cpu.setD(i, s.d[i]);
    for (int i = 0; i < 7; i++) cpu.setA(i, s.a[i]);
    cpu.setSP(s.a[7]);                     // a7 is authoritative for the active stack
    cpu.setPC(s.pc);
    cpu.setPC0(s.pc);
    // No prefetch in SST030: refill the pipe from RAM at pc.
    const auto r16 = [&](uint32_t a){ return uint16_t((cpu.mem[a&0xFFFFFF]<<8)|cpu.mem[(a+1)&0xFFFFFF]); };
    cpu.setIRD(r16(s.pc));
    cpu.setIRC(r16(s.pc + 2));
}

bool runVector(TestCpu& cpu, const Vector& v, bool checkCycles, std::string& why) {
    // O5: attach the 68882 for FP vectors, detach otherwise (byte-identical
    // legacy behaviour: F2xx = F-line). setFPUModel no-ops when unchanged,
    // so the jump table is only rebuilt at FPU/non-FPU file boundaries.
    cpu.setFPUModel(v.initial.hasFp ? moira::FPUModel::M68882
                                    : moira::FPUModel::NONE);
    cpu.reset();                           // clears STOP/halt state from prior vectors
    for (const RamCell& c : v.initial.ram) cpu.mem[c.addr & 0xFFFFFF] = c.val;
    loadState(cpu, v.initial);             // after RAM: prefetch refill reads it
    if (v.initial.hasFp) {
        for (int i = 0; i < 8; i++) cpu.setFP(i, v.initial.fp[i]);
        cpu.setFPCR(v.initial.fpcr);
        cpu.setFPSR(v.initial.fpsr);
        cpu.setFPIAR(v.initial.fpiar);
    }
    cpu.setVBR(v.initial.vbr);
    cpu.setSFC(v.initial.sfc);
    cpu.setDFC(v.initial.dfc);
    cpu.setCACR(v.initial.cacr);
    cpu.setCAAR(v.initial.caar);
    cpu.setCRP(v.initial.crp);             // O4: MMU registers are live
    cpu.setSRP(v.initial.srp);
    cpu.setTC(v.initial.tc);
    cpu.setTT0(v.initial.tt0);
    cpu.setTT1(v.initial.tt1);
    cpu.setMMUSR(v.initial.mmusr);

    const moira::i64 c0 = cpu.getClock();
    cpu.execute();                         // one instruction (+ its exception, if any)
    const long cyc = long(cpu.getClock() - c0);

    bool ok = true; char buf[256];
    auto fail = [&](const char* what, unsigned long got, unsigned long want) {
        if (ok) { std::snprintf(buf,sizeof buf,"%s got $%lX want $%lX",what,got,want); why=buf; } ok=false;
    };
    for (int i = 0; i < 8; i++) if (cpu.getD(i) != v.fin.d[i]) fail("D?", cpu.getD(i), v.fin.d[i]);
    for (int i = 0; i < 7; i++) if (cpu.getA(i) != v.fin.a[i]) fail("A?", cpu.getA(i), v.fin.a[i]);
    if (cpu.getSP()  != v.fin.a[7]) fail("A7",  cpu.getSP(),  v.fin.a[7]);
    if (cpu.getUSP() != v.fin.usp)  fail("USP", cpu.getUSP(), v.fin.usp);
    if (cpu.getISP() != v.fin.isp)  fail("ISP", cpu.getISP(), v.fin.isp);
    if (cpu.getMSP() != v.fin.msp)  fail("MSP", cpu.getMSP(), v.fin.msp);
    if (cpu.getSR()  != v.fin.sr)   fail("SR",  cpu.getSR(),  v.fin.sr);
    if (cpu.getPC0() != v.fin.pc)   fail("PC",  cpu.getPC0(), v.fin.pc);
    if (cpu.getVBR() != v.fin.vbr)  fail("VBR", cpu.getVBR(), v.fin.vbr);
    if (cpu.getSFC() != v.fin.sfc)  fail("SFC", cpu.getSFC(), v.fin.sfc);
    if (cpu.getDFC() != v.fin.dfc)  fail("DFC", cpu.getDFC(), v.fin.dfc);
    if (cpu.getCACR()!= v.fin.cacr) fail("CACR",cpu.getCACR(),v.fin.cacr);
    if (cpu.getCAAR()!= v.fin.caar) fail("CAAR",cpu.getCAAR(),v.fin.caar);
    if (cpu.getCRP() != v.fin.crp)  fail("CRP", (unsigned long)cpu.getCRP(), (unsigned long)v.fin.crp);
    if (cpu.getSRP() != v.fin.srp)  fail("SRP", (unsigned long)cpu.getSRP(), (unsigned long)v.fin.srp);
    if (cpu.getTC()  != v.fin.tc)   fail("TC",  cpu.getTC(),  v.fin.tc);
    if (cpu.getTT0() != v.fin.tt0)  fail("TT0", cpu.getTT0(), v.fin.tt0);
    if (cpu.getTT1() != v.fin.tt1)  fail("TT1", cpu.getTT1(), v.fin.tt1);
    if (cpu.getMMUSR()!=v.fin.mmusr)fail("MMUSR",cpu.getMMUSR(),v.fin.mmusr);
    if (v.initial.hasFp) {                 // O5: FP state compares
        for (int i = 0; i < 8; i++) {
            uint32_t w[3]; cpu.getFP(i, w);
            for (int j = 0; j < 3; j++) if (w[j] != v.fin.fp[i][j]) {
                char what[8]; std::snprintf(what,sizeof what,"FP%d.%d",i,j);
                fail(what, w[j], v.fin.fp[i][j]);
            }
        }
        if (cpu.getFPCR() != v.fin.fpcr)  fail("FPCR",  cpu.getFPCR(),  v.fin.fpcr);
        if (cpu.getFPSR() != v.fin.fpsr)  fail("FPSR",  cpu.getFPSR(),  v.fin.fpsr);
        if (cpu.getFPIAR()!= v.fin.fpiar) fail("FPIAR", cpu.getFPIAR(), v.fin.fpiar);
    }
    if (checkCycles && v.length >= 0 && cyc != v.length) {
        if (ok) { std::snprintf(buf,sizeof buf,"cycles got %ld want %ld",cyc,v.length); why=buf; } ok=false;
    }
    for (const RamCell& c : v.fin.ram) {
        const uint8_t got = cpu.mem[c.addr & 0xFFFFFF];
        if (got != c.val) { if (ok){std::snprintf(buf,sizeof buf,"RAM[$%06X] got $%02X want $%02X",c.addr,got,c.val);why=buf;} ok=false; }
    }
    if (!ok && gDump) {
        std::printf("      == %s\n", v.name.c_str());
        std::printf("         PC got %06X want %06X | SR got %04X want %04X | cyc got %ld want %ld\n",
                    cpu.getPC0(), v.fin.pc, cpu.getSR(), v.fin.sr, cyc, v.length);
        for (int i=0;i<8;i++) if (cpu.getD(i)!=v.fin.d[i]) std::printf("         D%d got %08X want %08X\n", i, cpu.getD(i), v.fin.d[i]);
        for (int i=0;i<8;i++) {
            const uint32_t got = (i<7) ? cpu.getA(i) : cpu.getSP();
            if (got!=v.fin.a[i]) std::printf("         A%d got %08X want %08X (init %08X)\n", i, got, v.fin.a[i], v.initial.a[i]);
        }
        for (const RamCell& c : v.fin.ram) {
            const uint8_t got = cpu.mem[c.addr & 0xFFFFFF];
            if (got != c.val) std::printf("         RAM[%06X] got %02X want %02X\n", c.addr, got, c.val);
        }
    }
    for (const RamCell& c : v.initial.ram) cpu.mem[c.addr & 0xFFFFFF] = 0;
    for (const RamCell& c : v.fin.ram)     cpu.mem[c.addr & 0xFFFFFF] = 0;
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
    if (argc < 2) { std::fprintf(stderr,"usage: %s <dir> [--max N] [--only NAME,..] [--skip NAME,..] [--cycles] [--skip-translation] [--verbose] [--dump] [--examples K]\n",argv[0]); return 2; }
    const std::string dir = argv[1];
    long maxPerFile=-1; int examples=3; bool verbose=false, checkCycles=false, skipTranslation=false;
    std::set<std::string> only, skip;
    for (int i=2;i<argc;++i){ std::string a=argv[i];
        if      (a=="--max"&&i+1<argc) maxPerFile=std::strtol(argv[++i],nullptr,10);
        else if (a=="--examples"&&i+1<argc) examples=int(std::strtol(argv[++i],nullptr,10));
        else if (a=="--only"&&i+1<argc) only=parseNameList(argv[++i]);
        else if (a=="--skip"&&i+1<argc) skip=parseNameList(argv[++i]);
        else if (a=="--cycles") checkCycles=true;
        else if (a=="--strict-mmu") { /* obsolete: translation is the default now */ }
        else if (a=="--skip-translation") skipTranslation=true;
        else if (a=="--verbose") verbose=true;
        else if (a=="--dump") gDump=true;
        else { std::fprintf(stderr,"unknown arg '%s'\n",a.c_str()); return 2; }
    }

    namespace fs = std::filesystem;
    if (!fs::exists(dir)||!fs::is_directory(dir)) { std::fprintf(stderr,"[sst68030] no data at '%s' — soft skip (run oracle/fuzz/fuzz030.py)\n",dir.c_str()); return 0; }
    std::vector<fs::path> files;
    for (const auto& e : fs::directory_iterator(dir)) if (e.is_regular_file()&&e.path().extension()==".json") files.push_back(e.path());
    std::sort(files.begin(),files.end());
    if (files.empty()) { std::fprintf(stderr,"[sst68030] '%s' holds no .json — soft skip\n",dir.c_str()); return 0; }

    TestCpu cpu;
    std::printf("[sst68030] dir=%s files=%zu cycles=%s mmu=%s\n", dir.c_str(), files.size(),
                checkCycles?"on":"off(advisory)", skipTranslation?"SKIP-TRANSLATION(debug)":"full");

    long grandTotal=0, grandPass=0, grandMmuSkip=0, filesRun=0; bool anyFail=false;
    for (const fs::path& f : files) {
        const std::string stem=f.stem().string();
        if (!only.empty()&&!only.count(stem)) continue;
        if (skip.count(stem)) { std::printf("  %-16s : SKIP\n",stem.c_str()); continue; }
        std::ifstream in(f,std::ios::binary);
        if (!in){ std::fprintf(stderr,"  %-16s: cannot open\n",stem.c_str()); anyFail=true; continue; }
        std::string buf((std::istreambuf_iterator<char>(in)),std::istreambuf_iterator<char>());
        const char* p=buf.c_str(); eat(p,'[');
        long total=0,passed=0,mmuSkip=0; std::vector<std::pair<std::string,std::string>> firstFew; Vector v;
        while (true) {
            skipWs(p); if (*p==']'||*p=='\0') break;
            if (!parseVector(p,v)) break;
            if (skipTranslation && v.initial.needsTranslation()) {
                ++mmuSkip;
                skipWs(p); if (*p==','){++p;continue;} if (*p==']'){++p;break;}
                continue;
            }
            std::string why; bool ok;
            try { ok = runVector(cpu,v,checkCycles,why); }
            catch (const std::exception& e) { ok=false; why=std::string("threw: ")+e.what(); }
            ++total; if (ok)++passed; else if (int(firstFew.size())<examples) firstFew.push_back({v.name,why});
            if (maxPerFile>0&&total>=maxPerFile) break;
            skipWs(p); if (*p==','){++p;continue;} if (*p==']'){++p;break;}
        }
        ++filesRun; grandTotal+=total; grandPass+=passed; grandMmuSkip+=mmuSkip;
        const bool fileOk=(passed==total); if (!fileOk) anyFail=true;
        std::printf("  %-16s : %6ld/%-6ld %s%s\n",stem.c_str(),passed,total,fileOk?"OK":"FAIL",
                    mmuSkip?(" (+"+std::to_string(mmuSkip)+" mmu-skipped)").c_str():"");
        if (!fileOk||verbose) for (auto& mm:firstFew) std::printf("        x \"%s\"  %s\n",mm.first.c_str(),mm.second.c_str());
    }
    std::printf("[sst68030] %s: %ld/%ld across %ld file(s), %ld mmu-skipped%s\n",
                anyFail?"FAIL":"OK", grandPass, grandTotal, filesRun, grandMmuSkip,
                anyFail?"  <<< MISMATCH":"");
    if (filesRun==0) { std::fprintf(stderr,"[sst68030] no files matched — soft skip\n"); return 0; }
    return anyFail?1:0;
}
