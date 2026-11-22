// POM68K — Macintosh 68k emulator
// VERHILLE Arnaud — Copyright (C) 2026 — MIT (see LICENSE-MIT)
//
// Replays POM68K's WinUAE-oracle 68030/68040 vectors (tests/data/sst680x0*)
// against Dirk Hoffmann's Moira FPU branch. Adapted from POM68K
// tests/sst68030.cpp and tests/sst68040.cpp (MIT): same JSON reader, same
// unit of execution (one instruction plus its exception; a staged trace is
// taken in the same step), same flat 16 MB bus cleared between vectors, same
// --strict-ram rule. Only the core API differs.
//
// Usage: sst_dirk --cpu 30|40 [--fpu] <dir> [--known FILE] [--strict-ram]
//                 [--only STEM,..] [--max N] [--examples K] [--tsv FILE]

#include "Moira.h"
#include "SstKnown.h"

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <map>
#include <set>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

using namespace moira;

namespace {

struct RamCell { uint32_t addr; uint8_t val; };

struct CpuState {
    uint32_t d[8] = {}, a[8] = {};
    uint32_t usp = 0, isp = 0, msp = 0, pc = 0;
    uint16_t sr = 0;
    uint32_t vbr = 0, sfc = 0, dfc = 0, cacr = 0, caar = 0;
    uint64_t crp = 0, srp = 0;
    uint32_t tc = 0, tt0 = 0, tt1 = 0, mmusr = 0;
    uint32_t urp040 = 0, srp040 = 0, tc040 = 0;
    uint32_t itt0 = 0, itt1 = 0, dtt0 = 0, dtt1 = 0, mmusr040 = 0;
    uint8_t stopped = 0;
    uint32_t fp[8][3] = {};
    uint32_t fpcr = 0, fpsr = 0, fpiar = 0;
    bool hasFp = false;
    std::vector<RamCell> ram;
};

struct Vector {
    std::string name;
    CpuState initial, fin;
    long length = -1;
    long steps = 1;
};

class TestCpu : public Moira {
public:
    std::vector<uint8_t> mem = std::vector<uint8_t>(1 << 24, 0);
    std::vector<uint32_t> dirty;
    unsigned long exceptions = 0;

    void clearDirty() { for (uint32_t a : dirty) mem[a] = 0; dirty.clear(); }
    bool traceStaged() const { return flags & State::TRACE_EXC; }
    void dropStagedTrace() { flags &= ~State::TRACE_EXC; }
    void loadQueue(u16 ird, u16 irc) { queue.ird = ird; queue.irc = irc; }

    using Moira::willExecute;
    void willExecute(M68kException, u16) override { exceptions++; }

    u8 read8(u32 a) const override { return mem[a & 0xFFFFFF]; }
    u16 read16(u32 a) const override {
        return u16(mem[a & 0xFFFFFF] << 8 | mem[(a + 1) & 0xFFFFFF]);
    }
    u32 read32(u32 a) const override { return u32(read16(a)) << 16 | read16(a + 2); }
    void write8(u32 a, u8 v) const override {
        auto& self = *const_cast<TestCpu*>(this);
        self.mem[a & 0xFFFFFF] = v;
        self.dirty.push_back(a & 0xFFFFFF);
    }
    void write16(u32 a, u16 v) const override { write8(a, u8(v >> 8)); write8(a + 1, u8(v)); }
    void write32(u32 a, u32 v) const override { write16(a, u16(v >> 16)); write16(a + 2, u16(v)); }
};

// ── JSON scanner (POM68K tests/sst68030.cpp) ─────────────────────────────
inline void skipWs(const char*& p) { while (*p==' '||*p=='\t'||*p=='\n'||*p=='\r') ++p; }
inline bool eat(const char*& p, char c) { skipWs(p); if (*p==c) { ++p; return true; } return false; }
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
void parseFpReg(const char*& p, uint32_t w[3]) {
    eat(p,'[');
    for (int i = 0; i < 3; i++) { w[i] = uint32_t(parseUint(p)); skipWs(p); if (*p==',') ++p; }
    skipWs(p); if (*p==']') ++p;
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
            else if (kl==2 && k[0]=='a' && k[1]>='0'&&k[1]<='7') st.a[k[1]-'0'] = uint32_t(parseUint(p));
            else if (key("usp"))      st.usp = uint32_t(parseUint(p));
            else if (key("isp"))      st.isp = uint32_t(parseUint(p));
            else if (key("msp"))      st.msp = uint32_t(parseUint(p));
            else if (key("sr"))       st.sr = uint16_t(parseUint(p));
            else if (key("pc"))       st.pc = uint32_t(parseUint(p));
            else if (key("vbr"))      st.vbr = uint32_t(parseUint(p));
            else if (key("sfc"))      st.sfc = uint32_t(parseUint(p));
            else if (key("dfc"))      st.dfc = uint32_t(parseUint(p));
            else if (key("cacr"))     st.cacr = uint32_t(parseUint(p));
            else if (key("caar"))     st.caar = uint32_t(parseUint(p));
            else if (key("crp"))      st.crp = parseUint(p);
            else if (key("srp"))      st.srp = parseUint(p);
            else if (key("tc"))       st.tc = uint32_t(parseUint(p));
            else if (key("tt0"))      st.tt0 = uint32_t(parseUint(p));
            else if (key("tt1"))      st.tt1 = uint32_t(parseUint(p));
            else if (key("mmusr"))    st.mmusr = uint32_t(parseUint(p));
            else if (key("urp040"))   st.urp040 = uint32_t(parseUint(p));
            else if (key("srp040"))   st.srp040 = uint32_t(parseUint(p));
            else if (key("tc040"))    st.tc040 = uint32_t(parseUint(p));
            else if (key("itt0"))     st.itt0 = uint32_t(parseUint(p));
            else if (key("itt1"))     st.itt1 = uint32_t(parseUint(p));
            else if (key("dtt0"))     st.dtt0 = uint32_t(parseUint(p));
            else if (key("dtt1"))     st.dtt1 = uint32_t(parseUint(p));
            else if (key("mmusr040")) st.mmusr040 = uint32_t(parseUint(p));
            else if (key("stopped"))  st.stopped = uint8_t(parseUint(p));
            else if (kl==3 && k[0]=='f' && k[1]=='p' && k[2]>='0'&&k[2]<='7') { parseFpReg(p, st.fp[k[2]-'0']); st.hasFp = true; }
            else if (key("fpcr"))     { st.fpcr = uint32_t(parseUint(p)); st.hasFp = true; }
            else if (key("fpsr"))     { st.fpsr = uint32_t(parseUint(p)); st.hasFp = true; }
            else if (key("fpiar"))    { st.fpiar = uint32_t(parseUint(p)); st.hasFp = true; }
            else if (key("ram"))      parseRam(p, st.ram);
            else skipValue(p);
        }
        skipWs(p);
        if (*p==','){++p;continue;} if (*p=='}'){++p;break;}
    }
}
bool parseVector(const char*& p, Vector& v) {
    skipWs(p); if (*p!='{') return false; ++p;
    v = Vector{};
    while (true) {
        skipWs(p);
        if (*p=='}'){++p;break;}
        if (*p=='"') {
            ++p; const char* k=p; while (*p&&*p!='"') ++p; const size_t kl=size_t(p-k);
            if (*p=='"') ++p;
            eat(p,':');
            if (kl==4 && !std::strncmp(k,"name",4)) { skipWs(p); if (*p=='"'){++p;const char* s=p;while(*p&&*p!='"')++p;v.name.assign(s,size_t(p-s));if(*p=='"')++p;} }
            else if (kl==7 && !std::strncmp(k,"initial",7)) parseState(p, v.initial);
            else if (kl==5 && !std::strncmp(k,"final",5))   parseState(p, v.fin);
            else if (kl==6 && !std::strncmp(k,"length",6))  v.length = long(parseUint(p));
            else if (kl==5 && !std::strncmp(k,"steps",5))   v.steps = long(parseUint(p));
            else skipValue(p);
        }
        skipWs(p);
        if (*p==','){++p;continue;} if (*p=='}'){++p;break;}
    }
    return true;
}

int gCpu = 30;
std::string gName;
bool gFpu = false, gStrictRam = false, gAll = false;

MFloat80 toF80(const uint32_t w[3]) {
    return MFloat80(u16(w[0] >> 16), u64(w[1]) << 32 | w[2]);
}

void setupVector(TestCpu& cpu, const Vector& v) {
    if (gCpu == 30) cpu.setCPModel(v.initial.hasFp ? CPModel::M68882 : CPModel::NONE);
    cpu.reset();
    cpu.clearDirty();
    cpu.exceptions = 0;
    for (const RamCell& c : v.initial.ram) { cpu.mem[c.addr & 0xFFFFFF] = c.val; cpu.dirty.push_back(c.addr & 0xFFFFFF); }
    const CpuState& s = v.initial;
    cpu.setSR(s.sr);
    cpu.setISP(s.isp); cpu.setMSP(s.msp); cpu.setUSP(s.usp);
    for (int i = 0; i < 8; i++) cpu.setD(i, s.d[i]);
    for (int i = 0; i < 7; i++) cpu.setA(i, s.a[i]);
    cpu.setSP(s.a[7]);
    cpu.setPC(s.pc);
    cpu.setPC0(s.pc);
    const auto r16 = [&](uint32_t a){ return u16(cpu.mem[a & 0xFFFFFF] << 8 | cpu.mem[(a + 1) & 0xFFFFFF]); };
    cpu.loadQueue(r16(s.pc), r16(s.pc + 2));
    cpu.setVBR(s.vbr); cpu.setSFC(u8(s.sfc)); cpu.setDFC(u8(s.dfc));
    cpu.setCACR(s.cacr); cpu.setCAAR(s.caar);
    if (gCpu == 30) {
        auto& m = cpu.mmu.mmu030;
        m.crp = s.crp; m.srp = s.srp;
        m.setTC(s.tc); m.setTT0(s.tt0); m.setTT1(s.tt1);
        m.mmusr = s.mmusr;
    } else {
        auto& m = cpu.mmu.mmu040;
        m.urp = s.urp040; m.srp = s.srp040;
        m.setTC(s.tc040);
        m.setITT0(s.itt0); m.setITT1(s.itt1); m.setDTT0(s.dtt0); m.setDTT1(s.dtt1);
        m.mmusr = s.mmusr040;
    }
    if ((gCpu == 30 && s.hasFp) || (gCpu == 40 && gFpu)) {
        for (int i = 0; i < 8; i++) cpu.fpu.fpr[i] = toF80(s.fp[i]);
        cpu.fpu.setFPCR(s.fpcr); cpu.fpu.setFPSR(s.fpsr); cpu.fpu.setFPIAR(s.fpiar);
    }
}

bool runVector(TestCpu& cpu, const Vector& v, std::string& why, std::string& field) {
    setupVector(cpu, v);
    const bool traced = (v.initial.sr & 0xC000) != 0;
    for (long step = 0; step < (v.steps > 0 ? v.steps : 1); ++step) {
        const unsigned long exc0 = cpu.exceptions;
        cpu.execute();
        if (traced && step == 0 && cpu.exceptions == exc0) cpu.dropStagedTrace();
        else if (cpu.traceStaged()) cpu.execute();
    }
    bool ok = true; char buf[256];
    auto fail = [&](const char* what, unsigned long long got, unsigned long long want) {
        if (ok) { std::snprintf(buf, sizeof buf, "%s got $%llX want $%llX", what, got, want); why = buf; field = what; }
        if (gAll) std::printf("      %-6s got %llX want %llX\n", what, got, want);
        ok = false;
    };
    const CpuState& f = v.fin;
    for (int i = 0; i < 8; i++) if (cpu.getD(i) != f.d[i]) { char w[4] = {'D', char('0'+i), 0}; fail(w, cpu.getD(i), f.d[i]); }
    for (int i = 0; i < 7; i++) if (cpu.getA(i) != f.a[i]) { char w[4] = {'A', char('0'+i), 0}; fail(w, cpu.getA(i), f.a[i]); }
    if (cpu.getSP() != f.a[7]) fail("A7", cpu.getSP(), f.a[7]);
    if (cpu.getUSP() != f.usp) fail("USP", cpu.getUSP(), f.usp);
    if (cpu.getISP() != f.isp) fail("ISP", cpu.getISP(), f.isp);
    if (cpu.getMSP() != f.msp) fail("MSP", cpu.getMSP(), f.msp);
    if (cpu.getSR() != f.sr) fail("SR", cpu.getSR(), f.sr);
    if (cpu.getPC0() != f.pc) fail("PC", cpu.getPC0(), f.pc);
    if (cpu.getVBR() != f.vbr) fail("VBR", cpu.getVBR(), f.vbr);
    if (cpu.getSFC() != f.sfc) fail("SFC", cpu.getSFC(), f.sfc);
    if (cpu.getDFC() != f.dfc) fail("DFC", cpu.getDFC(), f.dfc);
    if (cpu.getCACR() != f.cacr) fail("CACR", cpu.getCACR(), f.cacr);
    if (cpu.getCAAR() != f.caar) fail("CAAR", cpu.getCAAR(), f.caar);
    if (gCpu == 30) {
        const auto& m = cpu.mmu.mmu030;
        if (m.crp != f.crp) fail("CRP", m.crp, f.crp);
        if (m.srp != f.srp) fail("SRP", m.srp, f.srp);
        if (m.tc != f.tc) fail("TC", m.tc, f.tc);
        if (m.tt0 != f.tt0) fail("TT0", m.tt0, f.tt0);
        if (m.tt1 != f.tt1) fail("TT1", m.tt1, f.tt1);
        if ((m.mmusr & 0xFFFF) != f.mmusr) fail("MMUSR", m.mmusr, f.mmusr);
    } else {
        const auto& m = cpu.mmu.mmu040;
        if (m.urp != f.urp040) fail("URP", m.urp, f.urp040);
        if (m.srp != f.srp040) fail("SRP", m.srp, f.srp040);
        if (m.tc != f.tc040) fail("TC", m.tc, f.tc040);
        if (m.itt0 != f.itt0) fail("ITT0", m.itt0, f.itt0);
        if (m.itt1 != f.itt1) fail("ITT1", m.itt1, f.itt1);
        if (m.dtt0 != f.dtt0) fail("DTT0", m.dtt0, f.dtt0);
        if (m.dtt1 != f.dtt1) fail("DTT1", m.dtt1, f.dtt1);
        if (m.mmusr != f.mmusr040) fail("MMUSR", m.mmusr, f.mmusr040);
    }
    if ((gCpu == 30 && v.initial.hasFp) || (gCpu == 40 && gFpu)) {
        for (int i = 0; i < 8; i++) {
            const MFloat80 r = cpu.fpu.fpr[i];
            const MFloat80 w = toF80(f.fp[i]);
            if (r.signExp() != w.signExp() || r.mantissa() != w.mantissa()) {
                char what[8]; std::snprintf(what, sizeof what, "FP%d", i);
                fail(what, (unsigned long long)r.mantissa(), (unsigned long long)w.mantissa());
            }
        }
        if (cpu.fpu.getFPCR() != f.fpcr) fail("FPCR", cpu.fpu.getFPCR(), f.fpcr);
        if (cpu.fpu.getFPSR() != f.fpsr) fail("FPSR", cpu.fpu.getFPSR(), f.fpsr);
        if (cpu.fpu.getFPIAR() != f.fpiar) fail("FPIAR", cpu.fpu.getFPIAR(), f.fpiar);
    }
    for (const RamCell& c : f.ram) {
        const uint8_t got = cpu.mem[c.addr & 0xFFFFFF];
        if (got != c.val && gAll) std::printf("      RAM[%06X] got %02X want %02X\n", c.addr, got, c.val);
        if (got != c.val) { if (ok) { std::snprintf(buf, sizeof buf, "RAM[$%06X] got $%02X want $%02X", c.addr, got, c.val); why = buf; field = "RAM"; } ok = false; }
    }
    if (gStrictRam) {
        std::unordered_map<uint32_t, uint8_t> init;
        for (const RamCell& c : v.initial.ram) init[c.addr & 0xFFFFFF] = c.val;
        std::unordered_set<uint32_t> listed;
        for (const RamCell& c : f.ram) listed.insert(c.addr & 0xFFFFFF);
        for (uint32_t a : cpu.dirty) {
            if (listed.count(a)) continue;
            const auto it = init.find(a);
            const uint8_t want = it == init.end() ? 0 : it->second;
            if (cpu.mem[a] != want && gAll) std::printf("      RAM[%06X] got %02X want %02X (unlisted)\n", a, cpu.mem[a], want);
            if (cpu.mem[a] != want) {
                if (ok) { std::snprintf(buf, sizeof buf, "RAM[$%06X] got $%02X want $%02X (unlisted)", a, cpu.mem[a], want); why = buf; field = "RAM-unlisted"; }
                ok = false;
            }
        }
    }
    for (const RamCell& c : v.initial.ram) cpu.mem[c.addr & 0xFFFFFF] = 0;
    for (const RamCell& c : f.ram) cpu.mem[c.addr & 0xFFFFFF] = 0;
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

// The opcode word at the initial PC: groups mismatches by instruction.
uint16_t opcodeOf(const Vector& v) {
    uint8_t hi = 0, lo = 0;
    for (const RamCell& c : v.initial.ram) {
        if ((c.addr & 0xFFFFFF) == (v.initial.pc & 0xFFFFFF)) hi = c.val;
        if ((c.addr & 0xFFFFFF) == ((v.initial.pc + 1) & 0xFFFFFF)) lo = c.val;
    }
    return uint16_t(hi << 8 | lo);
}

}  // namespace

int main(int argc, char** argv) {
    std::string dir, tsv;
    long maxPerFile = -1; int examples = 3;
    std::set<std::string> only;
    sstknown::List known;
    for (int i = 1; i < argc; ++i) {
        std::string a = argv[i];
        if (a == "--cpu" && i + 1 < argc) gCpu = std::atoi(argv[++i]);
        else if (a == "--fpu") gFpu = true;
        else if (a == "--strict-ram") gStrictRam = true;
        else if (a == "--all") gAll = true;
        else if (a == "--name" && i + 1 < argc) gName = argv[++i];
        else if (a == "--max" && i + 1 < argc) maxPerFile = std::strtol(argv[++i], nullptr, 10);
        else if (a == "--examples" && i + 1 < argc) examples = std::atoi(argv[++i]);
        else if (a == "--only" && i + 1 < argc) only = parseNameList(argv[++i]);
        else if (a == "--tsv" && i + 1 < argc) tsv = argv[++i];
        else if (a == "--known" && i + 1 < argc) { if (!known.load(argv[++i])) { std::fprintf(stderr, "cannot load %s\n", argv[i]); return 2; } }
        else if (dir.empty() && a[0] != '-') dir = a;
        else { std::fprintf(stderr, "unknown arg '%s'\n", a.c_str()); return 2; }
    }
    if (dir.empty() || (gCpu != 30 && gCpu != 40)) {
        std::fprintf(stderr, "usage: %s --cpu 30|40 [--fpu] <dir> [--known FILE] [--strict-ram] [--only STEM,..] [--max N] [--examples K] [--tsv FILE]\n", argv[0]);
        return 2;
    }
    namespace fs = std::filesystem;
    std::vector<fs::path> files;
    for (const auto& e : fs::directory_iterator(dir)) if (e.is_regular_file() && e.path().extension() == ".json") files.push_back(e.path());
    std::sort(files.begin(), files.end());
    if (files.empty()) { std::fprintf(stderr, "no .json in %s\n", dir.c_str()); return 2; }

    TestCpu cpu;
    cpu.setModel(gCpu == 30 ? CPUModel::M68030 : (gFpu ? CPUModel::M68040 : CPUModel::M68LC040));
    std::FILE* out = tsv.empty() ? nullptr : std::fopen(tsv.c_str(), "w");
    if (out) std::fprintf(out, "file\tname\topcode\tknown\tfield\twhy\n");
    std::printf("[sst_dirk] cpu=680%d%s dir=%s files=%zu\n", gCpu, gCpu == 40 ? (gFpu ? " (FPU)" : " (LC)") : "", dir.c_str(), files.size());

    long gTotal = 0, gPass = 0, gKnownFail = 0, gKnownPass = 0;
    std::map<std::string, long> byField;
    for (const fs::path& fp : files) {
        const std::string stem = fp.stem().string();
        if (!only.empty() && !only.count(stem)) continue;
        std::ifstream in(fp, std::ios::binary);
        std::string buf((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
        const char* p = buf.c_str(); eat(p, '[');
        long total = 0, passed = 0, knownFail = 0, knownPass = 0;
        std::vector<std::pair<std::string, std::string>> firstFew;
        Vector v;
        while (true) {
            skipWs(p); if (*p == ']' || *p == '\0') break;
            if (!parseVector(p, v)) break;
            if (!gName.empty() && v.name != gName) { skipWs(p); if (*p == ',') { ++p; continue; } if (*p == ']') { ++p; break; } continue; }
            if (gAll) {
                std::printf("  == %s  pc=%06X sr=%04X op=%04X", v.name.c_str(), v.initial.pc, v.initial.sr, opcodeOf(v));
                for (int k = 2; k < 8; k += 2) { uint8_t h=0,l=0; for (auto& c : v.initial.ram) { if (c.addr==v.initial.pc+k) h=c.val; if (c.addr==v.initial.pc+k+1) l=c.val; } std::printf(" %02X%02X", h, l); }
                std::printf("  a7=%08X tc=%08X\n", v.initial.a[7], gCpu==30 ? v.initial.tc : v.initial.tc040);
            }
            std::string why, field; bool ok;
            try { ok = runVector(cpu, v, why, field); }
            catch (const std::exception& e) { ok = false; why = std::string("threw: ") + e.what(); field = "threw"; }
            catch (...) { ok = false; why = "threw (non-std)"; field = "threw"; }
            ++total;
            const bool isKnown = known.listed(v.name) || known.frameRule(v.name).has_value();
            if (ok) { ++passed; if (isKnown) ++knownPass; }
            else {
                if (isKnown) ++knownFail;
                else ++byField[field];
                if (!isKnown && int(firstFew.size()) < examples) firstFew.push_back({v.name, why});
                if (out) std::fprintf(out, "%s\t%s\t%04X\t%d\t%s\t%s\n", stem.c_str(), v.name.c_str(), opcodeOf(v), isKnown ? 1 : 0, field.c_str(), why.c_str());
            }
            if (maxPerFile > 0 && total >= maxPerFile) break;
            skipWs(p); if (*p == ',') { ++p; continue; } if (*p == ']') { ++p; break; }
        }
        gTotal += total; gPass += passed; gKnownFail += knownFail; gKnownPass += knownPass;
        std::printf("  %-18s : %6ld/%-6ld%s%s\n", stem.c_str(), passed, total,
                    knownFail ? (" (" + std::to_string(knownFail) + " of the failures are POM68K-known)").c_str() : "",
                    knownPass ? (" (" + std::to_string(knownPass) + " POM68K-known pass)").c_str() : "");
        for (auto& m : firstFew) std::printf("        x \"%s\"  %s\n", m.first.c_str(), m.second.c_str());
    }
    std::printf("[sst_dirk] %ld/%ld pass (%.2f%%); failures outside POM68K's known list: %ld\n",
                gPass, gTotal, gTotal ? 100.0 * gPass / gTotal : 0.0, gTotal - gPass - gKnownFail);
    std::printf("  first differing field:");
    for (auto& [k, n] : byField) std::printf(" %s=%ld", k.c_str(), n);
    std::printf("\n");
    if (out) std::fclose(out);
    return 0;
}
