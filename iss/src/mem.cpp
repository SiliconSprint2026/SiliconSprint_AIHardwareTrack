#include "mem.h"

#include <cstdio>
#include <cstring>

std::string hex32(uint32_t v) {
    char s[16];
    std::snprintf(s, sizeof s, "0x%08x", v);
    return s;
}

static uint8_t* in(std::vector<uint8_t>& m, uint32_t base, uint32_t addr, uint32_t n) {
    uint64_t off = uint64_t(addr) - base;           // 64-bit so addr < base wraps far away
    if (addr < base || off + n > m.size()) return nullptr;
    return m.data() + off;
}

uint8_t* Mem::ram(uint32_t addr, uint32_t n) {
    if (uint8_t* p = in(itcm, ITCM_BASE, addr, n)) return p;
    if (uint8_t* p = in(dtcm, DTCM_BASE, addr, n)) return p;
    if (uint8_t* p = in(test_ram, TEST_BASE, addr, n)) return p;
    return nullptr;
}

static bool is_mmio(uint32_t addr) { return addr >= MMIO_BASE && addr < MMIO_BASE + MMIO_SIZE; }

uint32_t Mem::load(uint32_t addr, int n) {
    if (addr % n) throw Fault("misaligned " + std::to_string(n) + "-byte load from " + hex32(addr));
    if (is_mmio(addr)) {
        uint32_t reg = addr & ~3u, v = 0;
        if (reg == MMIO_BASE + 8) v = uint32_t(*instret);     // cycle counter = instructions retired so far
        return n == 4 ? v : (v >> 8 * (addr & 3)) & ((1u << 8 * n) - 1);
    }
    uint8_t* p = ram(addr, n);
    if (!p) throw Fault("load from unmapped address " + hex32(addr));
    uint32_t v = 0;
    for (int i = 0; i < n; i++) v |= uint32_t(p[i]) << 8 * i;
    return v;
}

void Mem::store(uint32_t addr, int n, uint32_t value) {
    if (addr % n) throw Fault("misaligned " + std::to_string(n) + "-byte store to " + hex32(addr));
    if (is_mmio(addr)) {
        uint32_t v = value << 8 * (addr & 3);                  // byte lanes as on a 32-bit bus
        switch (addr & ~3u) {
        case MMIO_BASE + 0: std::fputc(int(value & 0xFF), stdout); break;
        case MMIO_BASE + 4: halted = true; halt_code = v; break;
        case MMIO_BASE + 8: break;                             // cycle counter is read-only
        case MMIO_BASE + 12: profile_id = v; break;
        }
        return;
    }
    uint8_t* p = ram(addr, n);
    if (!p) throw Fault("store to unmapped address " + hex32(addr));
    for (int i = 0; i < n; i++) p[i] = uint8_t(value >> 8 * i);
    if (tohost && addr == tohost && !tohost_written) {
        tohost_written = true;
        tohost_value = value;
    }
}

uint32_t Mem::fetch(uint32_t pc) {
    if (pc & 3) throw Fault("misaligned pc " + hex32(pc));
    uint8_t* p = in(itcm, ITCM_BASE, pc, 4);
    if (!p) p = in(test_ram, TEST_BASE, pc, 4);
    if (!p) throw Fault("instruction fetch outside ITCM at pc " + hex32(pc));
    return p[0] | p[1] << 8 | p[2] << 16 | uint32_t(p[3]) << 24;
}

void Mem::load16(uint32_t addr, uint8_t out[16]) {
    uint8_t* p = in(dtcm, DTCM_BASE, addr, 16);
    if (!p) throw Fault("XLD from outside DTCM: " + hex32(addr));
    std::memcpy(out, p, 16);
}

void Mem::store16(uint32_t addr, const uint8_t data[16]) {
    uint8_t* p = in(dtcm, DTCM_BASE, addr, 16);
    if (!p) throw Fault("XST to outside DTCM: " + hex32(addr));
    std::memcpy(p, data, 16);
}
