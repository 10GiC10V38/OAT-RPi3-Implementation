# OAT: Operation Execution Integrity on Raspberry Pi 3

A prototype implementation of **OAT** (Operation Execution Integrity Attestation) on Raspberry Pi 3B using **ARM TrustZone** (OP-TEE) and **LLVM** compile-time instrumentation.

OAT goes beyond static attestation — it verifies that an operation on an embedded device executed **without control-flow hijacking or data corruption**, not just that benign code is loaded. This detects ROP attacks and data-only attacks that alter program behavior without modifying code.

> Based on: *OAT: Attesting Operation Integrity of Embedded Devices* — Sun et al., IEEE S&P 2020
> arXiv: [1802.03462](https://arxiv.org/abs/1802.03462)

**Detailed docs**: [docs/results.md](docs/results.md) · [docs/observations.md](docs/observations.md)

---

## Architecture Overview

```
┌──────────────────────────────────────────────────────────────────┐
│                    Normal World (Linux / RPi3)                    │
│                                                                    │
│  ┌──────────────────────┐       ┌────────────────────────────┐   │
│  │   Instrumented App   │       │     liboat.c  (runtime)    │   │
│  │  (e.g. syringe_app)  │──────▶│  __oat_init()             │   │
│  │                      │       │  __oat_log(val)            │   │
│  │  [OATPass.cpp inserts│       │  __oat_func_enter(id)      │   │
│  │   hooks at every     │       │  __oat_func_exit(id)       │   │
│  │   branch and return  │       │  __oat_log_indirect(addr)  │   │
│  │   at compile time]   │       │  __oat_print_proof()       │   │
│  └──────────────────────┘       └──────────────┬─────────────┘   │
│                                                 │ TEEC_InvokeCommand│
└─────────────────────────────────────────────────┼────────────────┘
                                                  │ SMC instruction
┌─────────────────────────────────────────────────┼────────────────┐
│                  Secure World (OP-TEE)           │                 │
│                                                  ▼                 │
│                        ┌──────────────────────────────────────┐   │
│                        │         oat_ta.c  (Trusted App)       │   │
│                        │                                        │   │
│                        │  SHA-256 hash chain:                   │   │
│                        │  H_new = SHA256(H_prev || event)       │   │
│                        │                                        │   │
│                        │  Shadow stack:                         │   │
│                        │  push on func_enter, pop+verify        │   │
│                        │  on func_exit — detects ROP            │   │
│                        └──────────────────────────────────────┘   │
└──────────────────────────────────────────────────────────────────┘
```

Every `TEEC_InvokeCommand` call crosses the Normal → Secure World boundary. The final SHA-256 hash is the attestation proof for the operation.

---

## Repository Structure

```
.
├── llvm_pass/
│   └── OATPass.cpp              # LLVM IR pass — inserts __oat_* hooks at compile time
│
├── ta/oat/ta/
│   ├── oat_ta.c                 # Trusted Application (Secure World)
│   │                            #   SHA-256 hash of all control-flow events
│   │                            #   Shadow stack for ROP detection
│   ├── include/oat_ta.h         # TEE command IDs and constants
│   ├── sub.mk                   # OP-TEE build source list
│   └── Makefile                 # Builds .ta binary via TA Dev Kit
│
├── host/
│   ├── liboat.c                 # Runtime trampoline — wraps TEE calls
│   ├── drone_test.c             # Demo: drone controller (indirect call CFI)
│   ├── drone_test_bad_path.c    # Demo: ROP attack simulation
│   ├── build_rpi.sh             # Build pipeline for drone app
│   └── syringe/                 # Syringe pump — paper's evaluation target
│       ├── syringePump.c        # Ported from paper reference, uses __oat_* API
│       ├── util.c               # Hardware stubs (GPIO, Serial → stdout)
│       ├── LiquidCrystal.c      # LCD stub
│       ├── led.c                # LED stub
│       ├── lib/                 # Stub headers
│       └── build_syringe.sh     # Build pipeline for syringe_app
│
├── verifier/
│   └── verify_mission.py        # Parses execution log, replays hash, verifies proof
│
├── docs/
│   ├── results.md               # Syringe pump results vs paper Table III
│   └── observations.md          # Bugs found, design decisions, reproduction steps
│
├── screenshots/                 # Output screenshots from RPi3 runs
└── 1802.03462v3.pdf             # Reference paper
```

---

## Build Pipeline

Source code goes through four stages:

```
program.c
    │
    │  clang -emit-llvm -O0
    ▼
program.ll                     ← LLVM Intermediate Representation
    │
    │  opt -load-pass-plugin OATPass.so -passes="oat-pass"
    ▼
program_instrumented.ll        ← IR with __oat_* calls at every branch,
    │                            return, and indirect call
    │  llc -march=aarch64 -filetype=obj
    ▼
program.o                      ← AArch64 ELF object
    │
    │  aarch64-gcc + liboat.o + libteec
    ▼
program_app                    ← ARM64 binary, ready to deploy on RPi3
```

For the syringe pump (multiple source files), all `.ll` files are merged with `llvm-link` before the pass runs so the instrumentation sees the full program.

---

## What the LLVM Pass Instruments

`OATPass.cpp` runs once per function at compile time and inserts three types of hooks:

| Event | Hook Inserted | Where |
|---|---|---|
| Conditional branch | `__oat_log(1)` or `__oat_log(0)` | Entry of true/false destination block |
| Indirect call/jump | `__oat_log_indirect(target_addr)` | Before the indirect call |
| Function entry | `__oat_func_enter(func_id)` | First instruction of function |
| Function return | `__oat_func_exit(func_id)` | Before every `ret` instruction |

`func_id` is computed as the sum of ASCII values of the function name — a lightweight, collision-tolerant identifier.

---

## What the TA Measures

`oat_ta.c` maintains two security mechanisms:

**1. Rolling hash chain** — every event updates the SHA-256 state:
```
H_new = SHA256(H_prev || event_data)
```
The final digest uniquely identifies the exact sequence of branches, returns, and indirect calls executed during the operation. Same path → same hash, every run.

**2. Shadow stack** — tracks function entry/exit IDs:
```
func_enter(id)  →  push id onto shadow stack
func_exit(id)   →  pop expected_id, compare with id
                   if mismatch → SECURITY ALERT, return TEE_ERROR_SECURITY
```
A mismatch means a return address was corrupted — the signature of a ROP attack.

---

## Syringe Pump Case Study

The syringe pump (`host/syringe/`) is the primary evaluation target, matching the paper's SP benchmark (Table III). The motor stepping loop runs exactly `mLBolus × ustepsPerML` steps per bolus operation:

| Iteration | mLBolus | Motor Steps | B.Cond | Ret |
|---|---|---|---|---|
| count=11 | 0.011 mL | 75 | 79 | 310 |
| count=21 | 0.021 mL | 143 | 147 | 582 |
| count=31 | 0.031 mL | 211 | 215 | 854 |
| count=41 | 0.041 mL | 279 | 283 | 1126 |
| count=51 | 0.051 mL | 348 | 352 | 1402 |
| count=61 | 0.061 mL | 416 | 420 | 1674 |
| count=71 | 0.071 mL | 1416\* | **488** | **1946** |

\* Steps scale with `ustepsPerML = 20480`; the final iteration drives the count to the paper's reported values.

**Expected output on RPi3 (last iteration):**
```
[OAT] Final Execution Proof: 8088f9b2dd5f85f257435b3d9822103f...
[OAT] --- Instrumentation Statistics (per operation) ---
[OAT]   B.Cond  (branch logs):    488    (paper: 488)
[OAT]   Ret     (func exits):     1946    (paper: 1946)
[OAT]   Icall   (indirect calls): 0    (paper: 1)
[OAT] -------------------------------------------------
```

Running the same operation twice produces the **same hash** — deterministic attestation.

---

## Setup & Build

### Prerequisites

- LLVM/Clang ≥ 14
- OP-TEE developer environment for AArch64
- AArch64 cross-compiler (`aarch64-none-linux-gnu-gcc`)
- Raspberry Pi 3B running OP-TEE

### Environment Variables

```bash
export CROSS_COMPILE=<optee_path>/toolchains/aarch64/bin/aarch64-none-linux-gnu-
export TA_DEV_KIT_DIR=<optee_path>/optee_os/out/arm/export-ta_arm64
export SYSROOT=<optee_path>/out-br/host/aarch64-buildroot-linux-gnu/sysroot
```

### Build the Trusted Application

```bash
cd ta/oat/ta
make clean && make
# Output: 92b192d1-9686-424a-8d18-97c118129570.ta
```

### Build the LLVM Pass

```bash
cd llvm_pass
clang++ -shared -fPIC -o OATPass.so OATPass.cpp $(llvm-config --cxxflags --ldflags) -fno-rtti
cp OATPass.so ../host/
```

### Build the Host Application

**Syringe pump (paper evaluation):**
```bash
cd host/syringe && ./build_syringe.sh
# Output: syringe_app
```

**Drone demo:**
```bash
cd host && ./build_rpi.sh
# Output: drone_app
```

### Deploy to RPi3

```bash
scp ta/oat/ta/92b192d1-9686-424a-8d18-97c118129570.ta pi@<rpi3>:/lib/optee_armtz/
scp host/syringe/syringe_app pi@<rpi3>:/usr/bin/
```

---

## Running on RPi3

### Syringe pump

```bash
syringe_app
```

### Drone — normal execution

```bash
drone_app 1    # Active mode
drone_app      # Idle mode — produces a different hash
```

### Drone — ROP attack simulation

Build from `drone_test_bad_path.c` which calls `__oat_func_exit` with a wrong ID:

```
[OAT-FATAL] ROP ATTACK DETECTED! TEE blocked return.
```

<img alt="ROP attack detection" src="https://github.com/user-attachments/assets/4273ff3e-f34d-42ab-87e6-8676f031df1e" />

---

## Bugs Found and Fixed

### Bug 1 — Cumulative counts across operations

**File**: `host/liboat.c`
**Symptom**: Instrumentation counters reported 2004 branches and 7924 returns instead of 488/1946.
**Root cause**: `__oat_init()` guarded with `if (is_initialized) return` — ran only once, so counters accumulated across all 7 iterations instead of resetting per-operation.
**Fix**: Separated one-time TEE setup from per-operation reset. `__oat_init()` now always sends `CMD_HASH_INIT` to reset the TA and clears host-side counters on every call.

### Bug 2 — False ROP detection on second iteration

**File**: `ta/oat/ta/oat_ta.c`
**Symptom**: `[OAT-FATAL] ROP ATTACK DETECTED!` triggered after completing the first iteration.
**Root cause**: `init_session()` reset `stack_ptr = 0`, clearing `main()` and `loop()` from the shadow stack mid-execution. When `loop()` returned after the second `__oat_init()`, its expected ID was gone.
**Fix**: Removed `stack_ptr = 0` from `init_session()`. The shadow stack now persists for the full program lifetime; only the hash and log reset per-operation.

### Bug 3 — TA log buffer overflow

**File**: `ta/oat/ta/oat_ta.c`
**Symptom**: `OAT Log Overflow! Dropping event.` — hundreds of overflow errors per run.
**Root cause**: Logging all three event types: TAG_BRANCH (2 B × 488) + TAG_STACK_POP (5 B × 1946) = ~10.7 KB, exceeding the 8 KB buffer.
**Root cause (paper design)**: The paper records returns only in the hash, not the trace. Returns are too frequent to store.
**Fix**: Disabled `append_log` for `TAG_STACK_POP` and `TAG_BRANCH`. Returns and branches are captured in the SHA-256 hash; the log buffer is reserved for indirect calls only.

---

## Comparison with Reference OAT

| Aspect | Reference OAT (Paper) | This Implementation |
|---|---|---|
| Instrumentation level | Custom LLVM 4.0 assembly backend | LLVM IR pass (new pass manager) |
| Hash function | BLAKE-2s | SHA-256 |
| Measurement format | Forward trace + backward hash | Single running hash |
| CVI (data integrity) | Yes — 74% fewer sites than DFI | Not implemented |
| Platform | HiKey (ARM Cortex-A53) | Raspberry Pi 3 (ARM Cortex-A53) |
| TEE interface | Direct world-switch trampolines | TEEC Client API |
