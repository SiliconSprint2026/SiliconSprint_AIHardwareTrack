#pragma once
#include <cstdint>
#include <map>
#include <string>
#include <vector>

#include "mem.h"

struct ElfInfo {
    uint32_t entry = 0;
    std::map<std::string, uint32_t> symbols;
};

std::vector<uint8_t> read_file(const std::string& path);    // throws Fault
ElfInfo load_elf(Mem& mem, const std::string& path);        // throws Fault
