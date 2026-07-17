// POM68K — Macintosh 68k emulator
// VERHILLE Arnaud — Copyright (C) 2026 — GPLv3 (see LICENSE)
//
// Oracle memory backend replacing Hatari's memory.c: every physical access
// of the vendored WinUAE core (incl. MMU table walks, which go through
// x_phys_get_* -> phys_get_* -> get_* -> memory_get_*) lands in the flat
// big-endian buffer owned by the fuzzing harness (oracle_api.h).

#include "main.h"
#include "sysconfig.h"
#include "sysdeps.h"
#include "maccess.h"
#include "memory.h"
#include "newcpu.h"

// --- oracle buffer (bound by oracle_init) -------------------------------
uae_u8  *pom_mem  = NULL;
uae_u32  pom_mask = 0;
// While set, reads return 0 and writes are dropped: used during the initial
// m68k_go() boot pass so it cannot mutate the harness's RAM image.
int      pom_mem_frozen = 0;

// --- flat big-endian accessors ------------------------------------------
uae_u32 memory_get_long(uaecptr addr)
{
	if (pom_mem_frozen) return 0;
	uae_u32 a0 = addr & pom_mask, a1 = (addr + 1) & pom_mask;
	uae_u32 a2 = (addr + 2) & pom_mask, a3 = (addr + 3) & pom_mask;
	return ((uae_u32)pom_mem[a0] << 24) | ((uae_u32)pom_mem[a1] << 16) |
	       ((uae_u32)pom_mem[a2] << 8)  |  (uae_u32)pom_mem[a3];
}
uae_u32 memory_get_word(uaecptr addr)
{
	if (pom_mem_frozen) return 0;
	uae_u32 a0 = addr & pom_mask, a1 = (addr + 1) & pom_mask;
	return ((uae_u32)pom_mem[a0] << 8) | (uae_u32)pom_mem[a1];
}
uae_u32 memory_get_byte(uaecptr addr)
{
	if (pom_mem_frozen) return 0;
	return pom_mem[addr & pom_mask];
}
uae_u32 memory_get_longi(uaecptr addr) { return memory_get_long(addr); }
uae_u32 memory_get_wordi(uaecptr addr) { return memory_get_word(addr); }

void memory_put_long(uaecptr addr, uae_u32 v)
{
	if (pom_mem_frozen) return;
	pom_mem[addr & pom_mask]       = (uae_u8)(v >> 24);
	pom_mem[(addr + 1) & pom_mask] = (uae_u8)(v >> 16);
	pom_mem[(addr + 2) & pom_mask] = (uae_u8)(v >> 8);
	pom_mem[(addr + 3) & pom_mask] = (uae_u8)v;
}
void memory_put_word(uaecptr addr, uae_u32 v)
{
	if (pom_mem_frozen) return;
	pom_mem[addr & pom_mask]       = (uae_u8)(v >> 8);
	pom_mem[(addr + 1) & pom_mask] = (uae_u8)v;
}
void memory_put_byte(uaecptr addr, uae_u32 v)
{
	if (pom_mem_frozen) return;
	pom_mem[addr & pom_mask] = (uae_u8)v;
}

// --- addrbank plumbing (some core paths poke banks directly) -------------
static uae_u32 REGPARAM3 pom_lget(uaecptr addr) REGPARAM { return memory_get_long(addr); }
static uae_u32 REGPARAM3 pom_wget(uaecptr addr) REGPARAM { return memory_get_word(addr); }
static uae_u32 REGPARAM3 pom_bget(uaecptr addr) REGPARAM { return memory_get_byte(addr); }
static void REGPARAM3 pom_lput(uaecptr addr, uae_u32 v) REGPARAM { memory_put_long(addr, v); }
static void REGPARAM3 pom_wput(uaecptr addr, uae_u32 v) REGPARAM { memory_put_word(addr, v); }
static void REGPARAM3 pom_bput(uaecptr addr, uae_u32 v) REGPARAM { memory_put_byte(addr, v); }
static uae_u8 *REGPARAM3 pom_xlate(uaecptr addr) REGPARAM { return pom_mem + (addr & pom_mask); }
static int REGPARAM3 pom_check(uaecptr addr, uae_u32 size) REGPARAM { (void)addr; (void)size; return 1; }

addrbank pom_bank = {
	pom_lget, pom_wget, pom_bget,
	pom_lput, pom_wput, pom_bput,
	pom_xlate, pom_check,
	NULL,                    /* baseaddr */
	_T("oracle"), _T("oracle RAM"),
	pom_lget, pom_wget,      /* lgeti, wgeti */
	ABFLAG_RAM, 0, 0,
	NULL, 0xffffffff, 0, 0, 0, 0,
	NULL, NULL, 0, false, 0
};
addrbank dummy_bank = {
	pom_lget, pom_wget, pom_bget,
	pom_lput, pom_wput, pom_bput,
	pom_xlate, pom_check,
	NULL,
	_T("dummy"), _T("oracle RAM (dummy)"),
	pom_lget, pom_wget,
	ABFLAG_RAM, 0, 0,
	NULL, 0xffffffff, 0, 0, 0, 0,
	NULL, NULL, 0, false, 0
};

addrbank *mem_banks[MEMORY_BANKS];
uae_u8 *baseaddr[MEMORY_BANKS];
uae_u8 ce_banktype[65536];
uae_u8 ce_cachable[65536];

// Referenced by the core; harmless defaults.
bool canbang = false;
bool jit_direct_compatible_memory = false;

void pom_memory_init(uae_u8 *mem, uae_u32 size)
{
	pom_mem = mem;
	pom_mask = size - 1;
	for (int i = 0; i < MEMORY_BANKS; i++) {
		mem_banks[i] = &pom_bank;
		baseaddr[i] = NULL;
	}
	memset(ce_banktype, 0, sizeof ce_banktype);
	memset(ce_cachable, 0, sizeof ce_cachable);
}

// --- misc memory API the core links against ------------------------------
void memory_reset(void) {}
void memory_clear(void) {} // hardreset must NOT wipe the harness's buffer
void memory_map_dump(void) {}
uae_u8 *restore_memory(uae_u8 *src) { return src; }
void memory_restore(void) {}
int memory_waitstate_cycles(int bank_type, int cycles) { (void)bank_type; (void)cycles; return 0; }

bool real_address_allowed(void) { return true; }
uae_u8 *memory_get_real_address(uaecptr addr) { return pom_mem + (addr & pom_mask); }
int memory_valid_address(uaecptr addr, uae_u32 size) { (void)addr; (void)size; return 1; }

bool init_shm(void) { return true; }
void free_shm(void) {}
bool preinit_shm(void) { return true; }
