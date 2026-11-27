// Minimal ELF32 little-endian loader for the harness (no libelf).
//
// Rule (same as the ISS): every PT_LOAD segment is copied to its PHYSICAL address
// p_paddr (the LMA): p_filesz bytes from the file, then zeros up to p_memsz.
// With sw/link.ld that puts .text, .rodata and the initial image of .data in ITCM;
// crt0 copies .data to DTCM and clears .bss itself.
#pragma once
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <functional>
#include <string>
#include <vector>

// poke(addr, byte) returns false if addr is not backed by memory.
using PokeFn = std::function<bool(uint32_t, uint8_t)>;

static inline bool read_file(const std::string &path, std::vector<uint8_t> &out) {
  FILE *f = fopen(path.c_str(), "rb");
  if (!f) return false;
  uint8_t buf[65536];
  size_t n;
  while ((n = fread(buf, 1, sizeof buf, f)) > 0) out.insert(out.end(), buf, buf + n);
  fclose(f);
  return true;
}

// Returns an empty string on success, else an error message.
static inline std::string load_elf(const std::string &path, const PokeFn &poke, uint32_t *entry) {
  std::vector<uint8_t> f;
  if (!read_file(path, f)) return "cannot read " + path;
  auto u16 = [&](size_t o) { return (uint32_t)f[o] | (uint32_t)f[o + 1] << 8; };
  auto u32 = [&](size_t o) { return u16(o) | u16(o + 2) << 16; };

  if (f.size() < 52 || memcmp(f.data(), "\x7f" "ELF", 4) != 0) return path + ": not an ELF file";
  if (f[4] != 1 || f[5] != 1) return path + ": not a 32-bit little-endian ELF";
  if (u16(18) != 243) return path + ": not a RISC-V ELF";

  *entry = u32(24);
  uint32_t phoff = u32(28), phentsize = u16(42), phnum = u16(44);
  for (uint32_t i = 0; i < phnum; i++) {
    size_t ph = phoff + (size_t)i * phentsize;
    if (ph + 32 > f.size()) return path + ": truncated program header";
    if (u32(ph) != 1) continue;                      // PT_LOAD only
    uint32_t off = u32(ph + 4), paddr = u32(ph + 12), filesz = u32(ph + 16), memsz = u32(ph + 20);
    if ((size_t)off + filesz > f.size()) return path + ": truncated segment";
    for (uint32_t b = 0; b < memsz; b++) {
      uint8_t v = b < filesz ? f[off + b] : 0;
      if (!poke(paddr + b, v)) {
        char msg[128];
        snprintf(msg, sizeof msg, ": segment byte at 0x%08x is outside ITCM/DTCM", paddr + b);
        return path + msg;
      }
    }
  }
  return "";
}
