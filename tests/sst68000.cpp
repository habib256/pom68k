// POM68K — Macintosh 68k emulator
// VERHILLE Arnaud — Copyright (C) 2026 — GPLv3 (see LICENSE)
//
// SingleStepTests/680x0 68000 harness — the Moira-config gate (M4.5).
// Validates our vendored Moira build (PRECISE_TIMING, address errors, no
// Musashi mimicry) against 1M+ single-instruction vectors on a bare flat
// 16 MB bus: registers, SR, USP/SSP, PC + prefetch queue, touched RAM and
// exact cycle counts. Same hand-rolled JSON scanner as POMIIGS/POM2.
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
};

// ── Flat 16 MB test bus (no Mac hardware, no contention) ─────────────────
class TestCpu : public moira::Moira {
public:
    TestCpu() : mem(1 << 24, 0) { setModel(moira::Model::M68000); }
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

// ── JSON scanner (POM2/POMIIGS pattern — decimal values only) ────────────
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
            if (*p=='"') ++p; eat(p,':');
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
            if (*p=='"') ++p; eat(p,':');
            if (kl==4 && !std::strncmp(key,"name",4)) { skipWs(p); if (*p=='"'){++p;const char* s=p;while(*p&&*p!='"')++p;v.name.assign(s,size_t(p-s));if(*p=='"')++p;} }
            else if (kl==7 && !std::strncmp(key,"initial",7)) parseState(p, v.initial);
            else if (kl==5 && !std::strncmp(key,"final",5))   parseState(p, v.fin);
            else if (kl==6 && !std::strncmp(key,"length",6))  v.length = long(parseUint(p));
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

bool runVector(TestCpu& cpu, const Vector& v, bool checkCycles, std::string& why) {
    cpu.reset();                           // clears STOP/halt state from prior vectors
    loadState(cpu, v.initial);
    for (const RamCell& c : v.initial.ram) cpu.mem[c.addr & 0xFFFFFF] = c.val;

    const moira::i64 c0 = cpu.getClock();
    cpu.execute();                         // one instruction (+ its exception, if any)
    const long cyc = long(cpu.getClock() - c0);

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
    for (const RamCell& c : v.fin.ram) {
        const uint8_t got = cpu.mem[c.addr & 0xFFFFFF];
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
    if (argc < 2) { std::fprintf(stderr,"usage: %s <dir> [--max N] [--only NAME,..] [--skip NAME,..] [--no-cycles] [--verbose] [--examples K]\n",argv[0]); return 2; }
    const std::string dir = argv[1];
    long maxPerFile=-1; int examples=3; bool verbose=false, checkCycles=true;
    std::set<std::string> only, skip;
    for (int i=2;i<argc;++i){ std::string a=argv[i];
        if      (a=="--max"&&i+1<argc) maxPerFile=std::strtol(argv[++i],nullptr,10);
        else if (a=="--examples"&&i+1<argc) examples=int(std::strtol(argv[++i],nullptr,10));
        else if (a=="--only"&&i+1<argc) only=parseNameList(argv[++i]);
        else if (a=="--skip"&&i+1<argc) skip=parseNameList(argv[++i]);
        else if (a=="--no-cycles") checkCycles=false;
        else if (a=="--verbose") verbose=true;
        else if (a=="--dump") gDump=true;
        else { std::fprintf(stderr,"unknown arg '%s'\n",a.c_str()); return 2; }
    }

    namespace fs = std::filesystem;
    if (!fs::exists(dir)||!fs::is_directory(dir)) { std::fprintf(stderr,"[sst68000] no data at '%s' — soft skip (run tests/fetch_sst_68000.sh)\n",dir.c_str()); return 0; }
    std::vector<fs::path> files;
    for (const auto& e : fs::directory_iterator(dir)) if (e.is_regular_file()&&e.path().extension()==".json") files.push_back(e.path());
    std::sort(files.begin(),files.end());
    if (files.empty()) { std::fprintf(stderr,"[sst68000] '%s' holds no .json — soft skip\n",dir.c_str()); return 0; }

    TestCpu cpu;
    std::printf("[sst68000] dir=%s files=%zu cycles=%s\n", dir.c_str(), files.size(), checkCycles?"on":"off");

    long grandTotal=0, grandPass=0, filesRun=0; bool anyFail=false;
    for (const fs::path& f : files) {
        const std::string stem=f.stem().string();
        if (!only.empty()&&!only.count(stem)) continue;
        if (skip.count(stem)) { std::printf("  %-12s : SKIP\n",stem.c_str()); continue; }
        std::ifstream in(f,std::ios::binary);
        if (!in){ std::fprintf(stderr,"  %-12s: cannot open\n",stem.c_str()); anyFail=true; continue; }
        std::string buf((std::istreambuf_iterator<char>(in)),std::istreambuf_iterator<char>());
        const char* p=buf.c_str(); eat(p,'[');
        long total=0,passed=0; std::vector<std::pair<std::string,std::string>> firstFew; Vector v;
        while (true) {
            skipWs(p); if (*p==']'||*p=='\0') break;
            if (!parseVector(p,v)) break;
            if (knownBad(v.name)) { skipWs(p); if (*p==','){++p;} continue; }
            std::string why; const bool ok=runVector(cpu,v,checkCycles,why);
            ++total; if (ok)++passed; else if (int(firstFew.size())<examples) firstFew.push_back({v.name,why});
            if (maxPerFile>0&&total>=maxPerFile) break;
            skipWs(p); if (*p==','){++p;continue;} if (*p==']'){++p;break;}
        }
        ++filesRun; grandTotal+=total; grandPass+=passed;
        const bool fileOk=(passed==total); if (!fileOk) anyFail=true;
        std::printf("  %-12s : %6ld/%-6ld %s\n",stem.c_str(),passed,total,fileOk?"OK":"FAIL");
        if (!fileOk||verbose) for (auto& mm:firstFew) std::printf("        x \"%s\"  %s\n",mm.first.c_str(),mm.second.c_str());
    }
    std::printf("[sst68000] %s: %ld/%ld across %ld file(s)%s\n", anyFail?"FAIL":"OK", grandPass, grandTotal, filesRun, anyFail?"  <<< MISMATCH":"");
    if (filesRun==0) { std::fprintf(stderr,"[sst68000] no files matched — soft skip\n"); return 0; }
    return anyFail?1:0;
}
