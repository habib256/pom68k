# POM68K — Macintosh 68k emulator
# VERHILLE Arnaud — Copyright (C) 2026 — GPLv3 (see LICENSE)
"""Focused 68030 translation probes; ordinary corpus recipes stay in gen030."""


def put32(ram, address, value):
    for offset in range(4):
        ram[(address + offset) & 0xFFFFFF] = (value >> (24 - offset * 8)) & 255


def memory_probe(rng, fc):
    """MOVES.L or PTEST with an explicit FC, either access direction."""
    write = rng.getrandbits(1)
    if rng.getrandbits(1):
        return [0x0E90, write << 11]
    return [0xF010, 0x8130 | fc | rng.randrange(1, 8) << 10 | (not write) << 9]


def shelter(st, ram):
    """Translate only the operands; high-alias infrastructure matches TT1."""
    st.tt1 = 0x80000000 | (1 << 15) | (1 << 8) | 7
    st.sr |= 0x2000
    st.pc = (st.pc & ~7) | 0x80000000
    st.vbr |= 0x80000000
    st.isp |= 0x80000000
    st.msp |= 0x80000000
    st.a[7] = st.msp if st.sr & 0x1000 else st.isp
    for vector in range(256):
        put32(ram, vector * 4, 0x80008000 + vector * 0x20)


def attributes(rng, long_descriptor):
    return (rng.getrandbits(3) << 2) | (rng.getrandbits(1) << 6) | \
        ((rng.getrandbits(1) << 8) if long_descriptor else 0)


def page_descriptor(ram, address, stride, physical, flags):
    put32(ram, address, flags if stride == 8 else physical | flags)
    if stride == 8:
        put32(ram, address + 4, physical)


def sre_probe(st, ram, rng, root, stride):
    """Distinct CRP/SRP data maps, with either root bounded in either direction.

    A high logical alias matches TT1 for code, stacks, vectors and handlers.
    Operand addresses do not match TT1, so a lower SRP bound cannot prevent
    the probe or its exception handler from running.
    """
    supervisor_root = 0x20A000
    for offset in range(64 * stride):
        ram[supervisor_root + offset] = ram[root + offset]
    bound_root = rng.getrandbits(1)
    lower = rng.getrandbits(1)
    limit = rng.choice([i for i in range(2, 63) if i not in (7, 8, 9)])
    group = limit + rng.choice([-1, 0, 1])
    index = rng.randrange(64)
    for which, address_table in enumerate((root, supervisor_root)):
        leaf = 0x20B000 + which * 0x1000
        physical = 0x400000 + which * 0x100000
        if stride == 8:
            put32(ram, address_table + group * stride, 0x7FFF0003)
            put32(ram, address_table + group * stride + 4, leaf)
        else:
            put32(ram, address_table + group * stride, leaf | 2)
        put32(ram, leaf + index * stride, 1 if stride == 8 else physical | 1)
        if stride == 8:
            put32(ram, leaf + index * stride + 4, physical)
        put32(ram, physical + 0x100, 0xC0000000 | which << 24 | rng.getrandbits(24))
    pointers = []
    for which, address_table in enumerate((root, supervisor_root)):
        flags = (lower << 31 | limit << 16) if which == bound_root else 0x7FFF0000
        pointers.append(((flags | (3 if stride == 8 else 2)) << 32) | address_table)
    st.crp, st.srp = pointers
    st.tc |= 1 << 25
    shelter(st, ram)
    fc = rng.choice([1, 5])
    st.a[0], st.sfc, st.dfc = (group << 18) | (index << 12) | 0x100, fc, fc
    return memory_probe(rng, fc)


def early_probe(st, ram, rng, root, stride):
    """A page descriptor in the root or first address table, with bounds."""
    shelter(st, ram)
    level = rng.randrange(2)
    lower = rng.getrandbits(1)
    limit = rng.choice([i for i in range(2, 20) if i not in (7, 8, 9)])
    boundary_index = limit + rng.choice([-1, 0, 1])
    group, index = (boundary_index, rng.randrange(64)) if level == 0 else (12, boundary_index)
    logical = (group << 18) | (index << 12) | 0x100
    long_descriptor = level == 0 or stride == 8
    flags = 1 | attributes(rng, long_descriptor)
    if long_descriptor:
        flags |= (lower << 31) | (limit << 16)
    base = 0x400000
    if level == 0:
        st.crp = st.srp = ((flags & ~0xFFFC) << 32) | base
    else:
        page_descriptor(ram, root + group * stride, stride, base, flags)
    physical = base + (logical & (0xFFFFFF if level == 0 else 0x3FFFF))
    put32(ram, physical, 0xE0000000 | level << 24 | rng.getrandbits(24))
    fc = rng.choice([1, 5])
    st.a[0], st.sfc, st.dfc = logical, fc, fc
    return memory_probe(rng, fc)


def indirect_probe(st, ram, rng, root, stride):
    """Last-level indirection, including mixed descriptor sizes and rejects."""
    shelter(st, ram)
    group, index = 12, rng.randrange(64)
    logical = (group << 18) | (index << 12) | 0x100
    indirect_stride = rng.choice([4, 8])
    target = 0x20A000 + rng.choice([0, 8, 24] if indirect_stride == 8 else [0, 4, 12, 28])
    leaves = 0x201000
    entry = leaves + (group * 64 + index) * stride
    page_descriptor(ram, entry, stride, target, 3 if indirect_stride == 8 else 2)
    kind = rng.choice([0, 1, 1, 1, 2, 3])
    flags = kind | attributes(rng, indirect_stride == 8)
    page_descriptor(ram, target, indirect_stride, 0x400000, flags)
    put32(ram, 0x400100, rng.getrandbits(32))
    fc = rng.choice([1, 5])
    st.a[0], st.sfc, st.dfc = logical, fc, fc
    return memory_probe(rng, fc)


def early_fcl_probe(st, ram, rng, root, stride):
    """Early pages under FCL, including a bounded direct root (UM §9.7.1)."""
    words = early_probe(st, ram, rng, root, stride)
    st.tc |= 1 << 24
    if (st.crp >> 32) & 3 != 1:
        # A table root gains an FC lookup before its existing address tree.
        # A direct root has no FC lookup and retains its own limit.
        fc_root = 0x20A000
        for fc in range(8):
            page_descriptor(ram, fc_root + fc * stride, stride, root,
                            0x7FFF0003 if stride == 8 else 2)
        st.crp = st.srp = (st.crp & 0xFFFFFFFF00000000) | fc_root
    return words


def mixed_probe(st, ram, rng, root, stride):
    """A short table selects long leaves, or a long table selects short leaves."""
    shelter(st, ram)
    group, index = 12, rng.randrange(64)
    other_stride = 12 - stride
    leaves = 0x20A000
    flags = (3 if other_stride == 8 else 2) | (rng.getrandbits(2) << 2)
    if stride == 8:
        flags |= (rng.getrandbits(1) << 8) | 0x7FFF0000
    page_descriptor(ram, root + group * stride, stride, leaves, flags)
    kind = rng.choice([0, 1, 1, 1])
    flags = kind | attributes(rng, other_stride == 8)
    page_descriptor(ram, leaves + index * other_stride, other_stride, 0x400000, flags)
    put32(ram, 0x400100, rng.getrandbits(32))
    fc = rng.choice([1, 5])
    st.a[0], st.sfc, st.dfc = (group << 18) | (index << 12) | 0x100, fc, fc
    return memory_probe(rng, fc)


def tt_probe(st, ram, rng, root, stride):
    """TT0 hits/misses differ observably from the paged operand mapping."""
    shelter(st, ram)
    group, index = 12, rng.randrange(64)
    high = rng.choice([0x20, 0x30, 0x60])
    logical = (high << 24) | (group << 18) | (index << 12) | 0x100
    opcode = rng.randrange(6)
    fc = 5 if opcode >= 3 else rng.choice([1, 2, 5, 6, 7])
    kind = rng.randrange(3)
    flags = (1, 5, 0)[kind]
    entry = 0x201000 + (group * 64 + index) * stride
    page_descriptor(ram, entry, stride, 0x400000, flags)
    put32(ram, logical, 0x12345678)
    put32(ram, 0x400100, 0x87654321)
    address_base = rng.choice([high, high ^ 0x40])
    address_mask = rng.choice([0, 0x40, 0xFF, rng.randrange(256)])
    fc_base = rng.choice([fc, fc ^ 4])
    fc_mask = rng.choice([0, 4, 7, rng.randrange(8)])
    st.tt0 = address_base << 24 | address_mask << 16 | \
        rng.choice([0, 0x8000, 0x8000, 0x8000]) | \
        rng.getrandbits(3) << 8 | fc_base << 4 | fc_mask
    st.a[0], st.sfc, st.dfc = logical, fc, fc
    if opcode == 0:
        return [0x0E90, rng.getrandbits(1) << 11]
    if opcode == 1:
        return [0xF010, 0x8010 | fc | rng.getrandbits(1) << 9]  # PTEST level zero, no A
    if opcode == 2:
        return [0xF010, 0x8130 | fc | rng.randrange(1, 8) << 10 | rng.getrandbits(1) << 9]
    if opcode == 3:
        return [0x4AD0]  # TAS (A0): both phases need RWM for a TT hit
    if opcode == 5:
        return [rng.choice([0x2010, 0x2080])]  # ordinary MOVE.L, native data-window path
    st.d[0] = rng.choice([0x1234, 0x8765, rng.getrandbits(16)])
    return [0x0CD0, 1 << 6]  # CAS.W D0,D1,(A0)


PROBES = {"sre": sre_probe, "early": early_probe,
          "indirect": indirect_probe, "mixed": mixed_probe, "ttprobe": tt_probe,
          "earlyfcl": early_fcl_probe}
