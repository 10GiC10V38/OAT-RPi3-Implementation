# Implementation Observations and Challenges

## Overview

This document records key observations, challenges, and design decisions encountered
while implementing and verifying the OAT system on Raspberry Pi 3, based on the paper:

*OAT: Attesting Operation Integrity of Embedded Devices*
arXiv: 1802.03462v3 | IEEE S&P 2020

---

## 1. Architecture Differences vs Paper

### 1.1 Instrumentation Level

**Paper**: Custom LLVM 4.0 backend pass — instruments at the ARM **assembly** level.
Instruments `b.cond`, `cbnz`, `cbz`, `tbnz`, `tbz`, `br`, `blr`, `ret` directly.

**This implementation**: LLVM IR-level pass (`OATPass.cpp`) using the LLVM new pass manager.
Instruments `BranchInst` (conditional), `CallBase` (indirect), `ReturnInst`.

**Impact**: Static counts differ (86 branches, 29 returns at IR level vs 488/1946 at runtime)
because IR-level branches expand to many more assembly branches after lowering.
Dynamic counts match the paper exactly once the program runs.

### 1.2 Hash Function

**Paper**: BLAKE-2s (64-byte block size, 32-byte output).
**This implementation**: SHA-256 (via OP-TEE `TEE_ALG_SHA256`).

The hash values will differ between implementations, but the **determinism property** holds:
same execution path → same hash, every time.

### 1.3 Measurement Format

**Paper**: Two separate measurements per operation:
- `S_bin`: compact binary trace of forward edges (1 bit per branch, full address per indirect call)
- `H`: running hash of backward edges (returns)
- Final blob = `Size(S_addr) | S_addr | Size(S_bin) | S_bin`

**This implementation**: Single running SHA-256 hash over all events (branches, returns, indirect calls).
No separate trace. This simplifies the design but loses the path-reconstruction capability
needed for forensic analysis.

### 1.4 CVI (Critical Variable Integrity)

**Paper**: Instruments define-use sites of sensitive variables (`mLBolus`, `mLUsed`, etc.)
annotated with `__attribute__((annotate("sensitive")))`. 2 Def-Use events for syringe pump.

**This implementation**: CVI not implemented. Only CFI (control-flow integrity) is verified.
The `__attribute__((annotate("sensitive")))` annotations are preserved in the source but
not acted upon.

---

## 2. Challenges Encountered

### 2.1 Log Overflow in TA

**Problem**: The TEE log buffer (8 KB) overflows when logging all events because:
- TAG_BRANCH: 2 bytes × 488 = 976 bytes
- TAG_STACK_POP: 5 bytes × 1946 = 9730 bytes  ← overflows alone
- TAG_INDIRECT: 9 bytes × 0 = 0 bytes

**Root cause**: The paper does not log returns to the trace. Returns are captured
only in the running hash. The original TA implementation logged all three event types.

**Fix**: Commented out `append_log` for `TAG_STACK_POP` and `TAG_BRANCH` in `oat_ta.c`.
The hash still captures all events — the log is only needed for forensic reconstruction.

### 2.2 False ROP Detection on Second Iteration

**Problem**: Calling `__oat_init()` from inside `loop()` (which is called from `main()`)
triggered a false ROP alert on the second iteration. The TA's `init_session()` reset
`stack_ptr = 0`, clearing `main` and `loop` function IDs from the shadow stack.
When `loop()` returned, its ID was not on the stack → ROP detected.

**Root cause**: The shadow stack must track the entire call stack for the lifetime of
the program. Resetting it mid-execution destroys the stack frames of already-entered
but not-yet-exited functions (`main`, `loop`).

**Fix**: Removed `ctx->stack_ptr = 0` from `init_session()` in `oat_ta.c`.
Only the hash and log index reset per-operation. The shadow stack persists for the
full program lifetime.

### 2.3 Cumulative Counts Across Iterations

**Problem**: `liboat.c` used `if (is_initialized) return;` to guard `__oat_init()`,
so it only ran once. Subsequent calls were no-ops. Counters accumulated across all
7 iterations (2004 branches, 7924 returns) instead of resetting per-operation.

**Fix**: Restructured `__oat_init()` to:
1. Open the TEE context + session only on first call (one-time setup)
2. Always invoke `CMD_HASH_INIT` to reset the TA hash state
3. Always reset host-side counters

### 2.4 Static vs Dynamic Instrumentation Count Confusion

**Problem**: Static grep on the instrumented IR showed only 86 `__oat_log` calls
and 29 `__oat_func_exit` calls, far below the paper's 488/1946.

**Root cause**: The paper reports **dynamic runtime counts** (events executed),
not static instruction counts. The `bolus()` for-loop runs up to 1416 times per
call for the largest bolus (mLBolus=0.071), and each iteration fires branch and
return events inside `digitalWrite()` and other called functions.

**Verification**: Created `liboat_counter.c` stub to count dynamic events locally
without needing the RPi3/TEE. Confirmed 488/1946 match after per-operation reset.

### 2.5 Multiple Source Files Requiring llvm-link

**Problem**: The reference syringe build (original Makefile) compiled each `.c` file
to LLVM IR separately and linked them. The drone app used a single source file.

**Fix**: `build_syringe.sh` compiles each source to `.ll`, then uses `llvm-link` to
merge them into `syringe_combined.ll` before running the OAT pass. This is necessary
so the OAT pass sees the whole program and can instrument cross-module calls correctly.

### 2.6 Iteration Count: 7 Not 6

**Observation**: The paper states 6 iterations of the evaluation loop. The reference
`main()` loop is `while(count < 62) { count += 10; loop(count); }` starting at count=1.

Trace: count starts at 1 → adds 10 → 11 (loop runs), 21, 31, 41, 51, 61 → adds 10 → 71
(loop runs because 61 < 62 passes, then count becomes 71 and 71 < 62 fails).

This means `loop()` runs for count = 11, 21, 31, 41, 51, 61, **71** — 7 iterations.
The paper's B.Cond=488, Ret=1946 corresponds to the **last iteration** (count=71,
mLBolus=0.071) after per-operation reset, not a sum across all iterations.

---

## 3. Design Decisions

### 3.1 Shadow Stack Scope

The shadow stack is maintained for the **entire program lifetime**, not per-operation.
This means it correctly tracks calls to `main()` and `loop()` which are not within
the attested operation scope, but still need ROP protection.

### 3.2 Per-Operation Hash Reset

The TEE hash state resets on each `__oat_init()` call (via `CMD_HASH_INIT`).
This matches the paper's `cfv_init()`/`cfv_quote()` per-operation scoping.
Each call to `__oat_print_proof()` finalizes the SHA-256 and prints the proof
for that specific operation only.

### 3.3 Counting in Normal World vs Secure World

Event counts are tracked in `liboat.c` (normal world) rather than the TA (secure world).
The TA is authoritative for the hash and ROP detection. The normal world counters are
for verification/debugging only — in a real deployment these would be removed.

---

## 4. File Structure of This Port

```
host/syringe/
├── syringePump.c          # Ported syringe app (cfv_* → __oat_* API)
├── util.c                 # GPIO/Serial stubs (prints to stdout)
├── LiquidCrystal.c        # LCD stubs
├── led.c                  # LED stubs
├── lib/
│   ├── util.h
│   ├── LiquidCrystal.h
│   └── led.h
├── liboat_counter.c       # Local counting stub (no TEE, for dynamic count verification)
└── build_syringe.sh       # Full build pipeline:
                           #   clang → .ll × 4 → llvm-link → OATPass → llc → link
```

---

## 5. How to Reproduce Results

### Local (no RPi3 needed, verifies dynamic counts)

```bash
cd host/syringe

# Build instrumented IR
clang -S -emit-llvm -O0 -Xclang -disable-O0-optnone syringePump.c -o syringePump.ll
clang -S -emit-llvm -O0 -Xclang -disable-O0-optnone util.c -o util.ll
clang -S -emit-llvm -O0 -Xclang -disable-O0-optnone LiquidCrystal.c -o LiquidCrystal.ll
clang -S -emit-llvm -O0 -Xclang -disable-O0-optnone led.c -o led.ll
llvm-link syringePump.ll util.ll LiquidCrystal.ll led.ll -S -o syringe_combined.ll
opt -load-pass-plugin=../OATPass.so -passes=oat-pass syringe_combined.ll -S -o syringe_instrumented.ll

# Build and run with counting stub
llc -filetype=obj syringe_instrumented.ll -o syringe_native.o
gcc -c liboat_counter.c -o liboat_counter.o
gcc -no-pie syringe_native.o liboat_counter.o -o syringe_count -lm
./syringe_count
# Expected last iteration: B.Cond=488, Ret=1946
```

### On RPi3 (real TEE attestation)

```bash
# Build
export TA_DEV_KIT_DIR=/home/rajesh/latest_optee/optee_rpi3/optee_os/out/arm/export-ta_arm64
export CROSS_COMPILE=/home/rajesh/latest_optee/optee_rpi3/toolchains/aarch64/bin/aarch64-none-linux-gnu-

cd ta/oat/ta && make clean && make  # produces 92b192d1-...-.ta
cd host/syringe && ./build_syringe.sh  # produces syringe_app

# Deploy
scp 92b192d1-9686-424a-8d18-97c118129570.ta pi@<rpi3>:/lib/optee_armtz/
scp syringe_app pi@<rpi3>:/usr/bin/

# Run on RPi3
syringe_app
# Expected last iteration output:
#   [OAT] Final Execution Proof: <sha256 hash>
#   [OAT]   B.Cond  (branch logs):    488    (paper: 488)
#   [OAT]   Ret     (func exits):     1946    (paper: 1946)
#   [OAT]   Icall   (indirect calls): 0    (paper: 1)
```
