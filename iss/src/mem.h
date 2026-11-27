// Memory map of the TENSOR-CORE SoC, as seen by the ISS.
//
//   0x0000_0000  ITCM  64 KB   code (and read-only data); the ELF is loaded here
//   0x1000_0000  DTCM 256 KB   data; the weight blob is loaded at 0x1001_0000
//   0x2000_0000  MMIO          +0 UART TX, +4 HALT, +8 cycle counter, +C PROFILE
//   0x8000_0000  RAM   1 MB    only with --riscv-tests (the riscv-tests "p" env links here)
//
// Any access outside these regions throws Fault, which stops the simulation.
#pragma once
#include <cstdint>
#include <map>
#include <stdexcept>
#include <string>
#include <vector>

struct Fault : std::runtime_error {
    using std::runtime_error::runtime_error;
};

constexpr uint32_t ITCM_BASE = 0x00000000, ITCM_SIZE = 64 * 1024;
constexpr uint32_t DTCM_BASE = 0x10000000, DTCM_SIZE = 256 * 1024;
constexpr uint32_t MMIO_BASE = 0x20000000, MMIO_SIZE = 16;
constexpr uint32_t BLOB_ADDR = 0x10010000;
constexpr uint32_t TEST_BASE = 0x80000000, TEST_SIZE = 1024 * 1024;

struct Mem {
    std::vector<uint8_t> itcm = std::vector<uint8_t>(ITCM_SIZE);
    std::vector<uint8_t> dtcm = std::vector<uint8_t>(DTCM_SIZE);
    std::vector<uint8_t> test_ram;          // empty unless --riscv-tests

    // MMIO state
    const uint64_t* instret = nullptr;      // the cycle counter reads this
    bool halted = false;
    uint32_t halt_code = 0;
    uint32_t profile_id = 0;
    std::map<uint32_t, uint64_t> profile;   // kernel id -> retired instructions

    // riscv-tests: the first store to `tohost` ends the test
    uint32_t tohost = 0;
    bool tohost_written = false;
    uint32_t tohost_value = 0;

    // Pointer to n bytes of RAM at addr, or nullptr if they are not all in one RAM region.
    uint8_t* ram(uint32_t addr, uint32_t n);

    // Scalar accesses (n = 1, 2, 4). Misaligned or unmapped accesses throw Fault.
    uint32_t load(uint32_t addr, int n);
    void store(uint32_t addr, int n, uint32_t value);
    uint32_t fetch(uint32_t pc);

    // 16-byte coprocessor accesses (addr already 16-byte aligned). DTCM only.
    void load16(uint32_t addr, uint8_t out[16]);
    void store16(uint32_t addr, const uint8_t in[16]);
};

std::string hex32(uint32_t v);
