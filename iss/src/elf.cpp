// Minimal ELF32 little-endian RISC-V loader.
// Every PT_LOAD segment is copied to its physical address (p_paddr, the LMA) and
// zero-filled up to p_memsz, the same as the RTL harness. With the kit's link.ld all
// loadable bytes land in ITCM (.data is copied to DTCM by crt0, .bss is NOLOAD).
#include <cstring>
#include <fstream>
#include <iterator>

#include "elf.h"

namespace {

template <class T> T rd(const std::vector<uint8_t>& f, size_t off) {
    if (off + sizeof(T) > f.size()) throw Fault("truncated ELF file");
    T v;
    std::memcpy(&v, f.data() + off, sizeof v);
    return v;
}

}  // namespace

std::vector<uint8_t> read_file(const std::string& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) throw Fault("cannot open " + path);
    return std::vector<uint8_t>(std::istreambuf_iterator<char>(in), {});
}

ElfInfo load_elf(Mem& mem, const std::string& path) {
    std::vector<uint8_t> f = read_file(path);
    if (f.size() < 52 || std::memcmp(f.data(), "\x7f" "ELF", 4) != 0) throw Fault(path + ": not an ELF file");
    if (f[4] != 1 || f[5] != 1 || rd<uint16_t>(f, 18) != 243)
        throw Fault(path + ": not a 32-bit little-endian RISC-V ELF");

    ElfInfo info;
    info.entry = rd<uint32_t>(f, 24);
    const uint32_t phoff = rd<uint32_t>(f, 28), shoff = rd<uint32_t>(f, 32);
    const uint16_t phentsize = rd<uint16_t>(f, 42), phnum = rd<uint16_t>(f, 44);
    const uint16_t shentsize = rd<uint16_t>(f, 46), shnum = rd<uint16_t>(f, 48);

    for (int i = 0; i < phnum; i++) {
        size_t ph = phoff + size_t(i) * phentsize;
        if (rd<uint32_t>(f, ph) != 1) continue;                        // PT_LOAD only
        uint32_t off = rd<uint32_t>(f, ph + 4), paddr = rd<uint32_t>(f, ph + 12);
        uint32_t filesz = rd<uint32_t>(f, ph + 16), memsz = rd<uint32_t>(f, ph + 20);
        if (memsz == 0) continue;
        uint8_t* dst = mem.ram(paddr, memsz);
        if (!dst) throw Fault(path + ": segment at " + hex32(paddr) + " (" + std::to_string(memsz) +
                              " bytes) does not fit in a memory region");
        if (size_t(off) + filesz > f.size()) throw Fault(path + ": segment past end of file");
        std::memcpy(dst, f.data() + off, filesz);
        std::memset(dst + filesz, 0, memsz - filesz);
    }

    // Symbols: only needed for riscv-tests (`tohost`).
    for (int i = 0; i < shnum; i++) {
        size_t sh = shoff + size_t(i) * shentsize;
        if (rd<uint32_t>(f, sh + 4) != 2) continue;                    // SHT_SYMTAB
        uint32_t off = rd<uint32_t>(f, sh + 16), size = rd<uint32_t>(f, sh + 20);
        uint32_t link = rd<uint32_t>(f, sh + 24), entsize = rd<uint32_t>(f, sh + 36);
        uint32_t stroff = rd<uint32_t>(f, shoff + size_t(link) * shentsize + 16);
        for (uint32_t s = 0; entsize && s + entsize <= size; s += entsize) {
            uint32_t name = rd<uint32_t>(f, off + s);
            const char* p = reinterpret_cast<const char*>(f.data()) + stroff + name;
            if (stroff + name < f.size()) info.symbols[std::string(p, strnlen(p, f.size() - stroff - name))] =
                rd<uint32_t>(f, off + s + 4);
        }
    }
    return info;
}
