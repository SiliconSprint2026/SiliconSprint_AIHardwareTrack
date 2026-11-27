# TENSOR-CORE: build a tensor coprocessor for a tiny GPT


**Problem Statement in brief : [Problem statements_Hardware_forAI.pdf](https://github.com/user-attachments/files/35512546/Problem.statements_Hardware_forAI.pdf)**

You get a small RISC-V chip (PicoRV32) with an **empty coprocessor slot**, and a tiny int8
GPT that writes Shakespeare-like text. Your job:

1. **Phase 1: make it work.** Implement the 12 custom instructions in
   [`docs/ISA_SPEC.md`](docs/ISA_SPEC.md) inside **`rtl/xtc_coproc.sv`**, so the model
   prints exactly the expected text.
2. **Phase 2: make it fast.** Make the model run in less time (cycles x clock period),
   by changing your hardware and the firmware, while staying correct and under the area cap.

---

## 1. Setup (once)

1. Install **Docker Desktop** (Windows/macOS) or Docker Engine (Linux), and **Git**
   (on Windows, Git for Windows, which includes *Git Bash*). Start Docker.
2. On GitHub, click **"Use this template" → Create a new repository**. Make it **private**.
3. In your new repo: **Settings → Collaborators → add the judging account** (given at kickoff).
   Teams that have not added it by the deadline cannot be scored.
4. Clone your repo and open a terminal in it (on Windows: Git Bash).
5. Build the tools and check everything works:
   ```sh
   bash docker/run.sh make test
   ```
   The first run builds the toolchain container (about 15 minutes, 3.4 GB, needs internet).
   After that it starts in seconds and works offline.

Every command below runs inside that container. Either prefix each one with
`bash docker/run.sh`, or open a shell in the container once with `bash docker/run.sh` and
type the commands there.

## 2. What to do

```
edit rtl/xtc_coproc.sv  ->  make test  ->  read which test fails  ->  fix  ->  make test ...
```

With the empty slot you get `0/5 passed`. When all five pass, Phase 1 is done.

| command | what it does | when to use it |
| --- | --- | --- |
| **`make test`** | builds your RTL and the firmware, runs the 5 verified tests, prints PASS/FAIL | all the time |
| **`make run`** | runs the model on your RTL: the text, the cycle count, cycles per kernel | to see what is slow |
| **`make perf`** | cycles, clock period, **T = cycles x period**, area vs the cap, estimated points | Phase 2 |
| `make synth` | area (um^2) and clock period (ns) only, takes 1 to 3 minutes | Phase 2, to check the area cap |

### The 5 tests

| # | test | checks |
| --- | --- | --- |
| 1 | instructions (`unit_tests`) | every instruction on its edge cases; tells you which of the 12 fail |
| 2 | one attention head | a short real computation (scores, softmax, weighted sum) |
| 3 | full model (`tiny_gpt`) | the whole model, output compared byte for byte |
| 4 | load/store ordering | your loads/stores and the core's stay in program order |
| 5 | reserved encodings | every invalid encoding traps (do not answer it, see the header of `rtl/xtc_coproc.sv`) |

Program output is in `build/test/<test>.out`, simulator messages in `build/test/<test>.log`.

### When a test fails (optional tools)

You do not need these to finish, but they find bugs fast:

* **`make trace ELF=tests/elf/unit_tests.elf`** runs the program on your RTL and on a
  software model of the same chip (`iss/`) and prints the **first instruction where they
  differ**, with the register values. This is the quickest way to find a wrong instruction.
* **`make wave ELF=tests/elf/unit_tests.elf`** writes a waveform to `build/wave/`. Open it
  on your own machine with GTKWave or Surfer (the container has no display).
* `make lint` checks your Verilog for common mistakes.

## 3. What you may change

| path | | |
| --- | --- | --- |
| `rtl/xtc_coproc.sv` | **your coprocessor** | this is the main job; add more `.sv` files in `rtl/` if you like |
| `rtl/soc_top.sv`, `rtl/mmio.sv`, `rtl/picorv32.v` | the rest of the chip | allowed (Phase 2), at your own risk |
| `sw/` | the firmware: `model.c`, `kernels/*.c`, `xtc.h` | allowed (Phase 2) |
| `docs/` | the spec | read it |
| `ref/model_int.py` | the reference model in Python | read it; it defines the right answer |

> **DO NOT MODIFY `tests/`, `sim/`, `synth/`, `docker/`, `ref/`, `iss/` or `rtl/tcm.sv`.**
> **You do not need to understand how they work.** They are the test bench and the
> measuring tools. When we score you, we replace all of them with our own copies, so any
> change there does nothing, except hide a bug from you.

## 4. The chip

```
     PicoRV32 (RV32IM) ---PCPI---> xtc_coproc   <- yours
        | 32-bit bus                   | 128-bit port
        +--> ITCM 64 KB  @0x0000_0000  |
        +--> DTCM 256 KB @0x1000_0000 <+   (weights at 0x1001_0000)
        +--> MMIO        @0x2000_0000      (UART, HALT, cycle counter, PROFILE)
```

* PicoRV32 hands every instruction it does not know to your coprocessor over **PCPI** and
  waits. The header of `rtl/xtc_coproc.sv` explains the handshake. Read it first,
  especially the **16-cycle timeout**.
* Your coprocessor has its own 128-bit port into DTCM: one aligned 16-byte row per cycle,
  data one cycle after the request.
* The firmware finds the weights, the prompt and the output length in the weight blob
  ([`docs/BLOB_FORMAT.md`](docs/BLOB_FORMAT.md)).

## 5. The model

TinyChar-GPT: 64-character vocabulary, context 64, 2 layers, d = 64, 4 heads, FFN 256,
everything in int8/int32 integer arithmetic. `ref/model_int.py` builds the whole model from
Python versions of the 12 instructions, so you can read it next to the firmware:

```sh
python3 ref/model_int.py                              # the public prompt
python3 ref/model_int.py --prompt "To be, or not" --n 40
```

The public run is a 16-character prompt plus 48 generated characters. Judging also uses
hidden prompts of 8 to 24 characters, so do not special-case the public one.

## 6. Scoring

**Phase 1 (35 points)**

| test | points |
| --- | --- |
| 1 instructions | 10 x (passing groups / 12) |
| 2 attention head | 5 |
| 3 full model, public **and hidden** prompts | 15 (all or nothing) |
| 4 ordering | 3 (only if test 1 fully passes) |
| 5 reserved encodings | 2 (only if test 1 fully passes) |

**Phase 2 (40 points).** Let `T = cycles of the public run × clock period from make synth`.

The performance score is:

$$
P = \min\left(40,\ 35 \times \frac{\ln(T_0/T)}{\ln(T_0/T_1)}\right)
$$

- A design matching `T0` earns **0 points**.
- A design matching `T1` earns **35 points**.
- Designs faster than `T1` can earn up to **40 points**.

The score is **0** if performance is slower than `T0`, the area exceeds the area cap, or test 3 fails on any prompt.

`make perf` prints your estimated score.

| | |
| --- | --- |
| `T0` (a straightforward design) | 442.94 ms |
| `T1` (a well-optimized design, worth 35 points) | 32.22 ms |
| area cap | 1,063,495 um^2 |

For scale: the chip with the empty slot is about 110,500 um^2 at 17.2 ns; that clock is set
by PicoRV32 itself.

**Phase 2 ideas:** the firmware in `sw/kernels/` has a plain-C and an Xtc version of every
kernel; `make run` shows where the cycles go. You may add your own instructions in the free
opcodes **CUSTOM-2 (`0x5B`) and CUSTOM-3 (`0x7B`)** (CUSTOM-0/1 must stay exactly as
specified), change the firmware, or speed up the core.

## 7. Rules

* All RTL is hand-written Verilog/SystemVerilog. No Chisel, SpinalHDL, HLS or other generators.
* **Feature switches.** Every Phase 2 optimization must be switchable so judges can measure
  it: feature `FOO` is turned off by `XTC_NO_FOO`. Wrap it in `` `ifndef XTC_NO_FOO `` in RTL
  and `#ifndef XTC_NO_FOO` in C, and list your feature names in `FEATURES.md`.
  `make perf DEFINES=XTC_NO_FOO` shows the effect of turning it off. With any feature off the
  design must still pass `make test`.
* List anything you did not write yourself (code, IP, papers you followed closely) in
  `ATTRIBUTION.md`. RTL is compared against open-source designs and other teams.
* **Submission:** the commit you enter in the submission form before the deadline. Later
  pushes are not scored.
* **Defense:** a judge types a prompt of their choice and runs your design on it; it must
  print exactly what `ref/model_int.py` prints.
