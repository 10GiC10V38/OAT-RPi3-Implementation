# OAT: Operation Execution Integrity on Raspberry Pi 3

An implementation of the OAT attestation system from the paper:

> **OAT: Attesting Operation Integrity of Embedded Devices**
> Zhichuang Sun, Bo Feng, Long Lu, Somesh Jha — IEEE S&P 2020
> [arXiv:1802.03462](https://arxiv.org/abs/1802.03462)

This repository implements OAT on a Raspberry Pi 3 using ARM TrustZone via OP-TEE. It includes a verification of the paper's syringe pump evaluation results (Table III).

---

## What This Implements

OAT attests that an operation on an embedded device executed without control-flow hijacking. It does this by:

1. **Forward-Edge CFI** — every conditional branch and indirect call is logged to a running SHA-256 hash inside the Secure World (TEE)
2. **Backward-Edge CFI (Shadow Stack)** — every function entry/exit is verified against a shadow stack in the TEE; mismatches indicate a ROP attack and immediately terminate the program
3. **Compiler Instrumentation** — an LLVM pass automatically injects probes at compile time; no manual source annotation required (beyond operation entry/exit markers)

The paper also defines CVI (Critical Variable Integrity) for data-only attacks. **This implementation covers CFI only.**

---

## Repository Structure

```
.
├── llvm_pass/
│   └── OATPass.cpp              # LLVM IR pass — injects __oat_log, __oat_func_enter/exit,
│                                #   __oat_log_indirect into target application at compile time
│
├── ta/oat/ta/
│   ├── oat_ta.c                 # Trusted Application (runs in TrustZone Secure World)
│   │                            #   — maintains SHA-256 hash of execution events
│   │                            #   — shadow stack for ROP detection
│   ├── include/oat_ta.h         # Command IDs and log tag definitions
│   └── Makefile                 # Builds .ta binary via OP-TEE TA Dev Kit
│
├── host/
│   ├── liboat.c                 # Trampoline library — bridges instrumented app to TEE
│   ├── drone_test.c             # Demo: drone controller (indirect call CFI + ROP demo)
│   ├── drone_test_bad_path.c    # Demo: simulates ROP attack
│   ├── build_rpi.sh             # Build pipeline for drone_test
│   └── syringe/                 # Syringe pump port (paper evaluation target)
│       ├── syringePump.c        # Ported from paper's reference — uses __oat_* API
│       ├── liboat_counter.c     # Counting stub for local verification (no TEE needed)
│       ├── build_syringe.sh     # Build pipeline for syringe_app
│       └── lib/                 # Hardware stubs (LCD, GPIO, Serial — print to stdout)
│
├── verifier/
│   └── verify_mission.py        # Parses binary execution log, replays hash, verifies proof
│
├── docs/
│   ├── results.md               # Verification results mapped to paper Table III
│   └── observations.md          # Challenges, design decisions, reproduction steps
│
└── screenshots/                 # Output screenshots from RPi3 runs
```

---

## Verification Against the Paper

The paper evaluates OAT on 5 embedded programs (Table III). This repo verifies the **Syringe Pump (SP)** row.

| Metric | Paper (SP) | This Implementation | Status |
|---|---|---|---|
| B.Cond (conditional branches) | 488 | **488** | Exact match |
| Ret (function returns) | 1946 | **1946** | Exact match |
| Icall/Ijmp (indirect calls) | 1 | 0 | See note |
| Def-Use (CVI data checks) | 2 | 0 | Not implemented |
| Runtime overhead | 1.9% | ~2–5% | Close |

**Icall note**: The paper's 1 indirect call is inside the `cfv_bellman.c` trampoline library. This implementation uses direct TEE calls in `liboat.c` — no indirect calls in the trampoline path.

See [docs/results.md](docs/results.md) for full details and [docs/observations.md](docs/observations.md) for challenges encountered.

---

## Prerequisites

**Hardware:** Raspberry Pi 3 Model B/B+
**On RPi3:** OP-TEE running (optee_os + optee_client built for RPi3)
**Build machine:** Linux with:
- `clang` + `llvm-dev` (tested with LLVM 18)
- OP-TEE toolchain and sysroot built for AArch64

---

## Build Instructions

### 1. Export environment variables

```bash
export CROSS_COMPILE=<path>/toolchains/aarch64/bin/aarch64-none-linux-gnu-
export TA_DEV_KIT_DIR=<path>/optee_os/out/arm/export-ta_arm64
export SYSROOT=<path>/out-br/host/aarch64-buildroot-linux-gnu/sysroot
```

### 2. Build the Trusted Application

```bash
cd ta/oat/ta
make clean && make
# Output: 92b192d1-9686-424a-8d18-97c118129570.ta
```

### 3. Build the LLVM Pass

```bash
cd llvm_pass
clang++ -shared -fPIC -o OATPass.so OATPass.cpp $(llvm-config --cxxflags --ldflags) -fno-rtti
cp OATPass.so ../host/
```

### 4a. Build the drone demo app

```bash
cd host
./build_rpi.sh
# Output: drone_app
```

### 4b. Build the syringe pump app (paper evaluation)

```bash
cd host/syringe
./build_syringe.sh
# Output: syringe_app
```

---

## Deployment

```bash
# Copy TA to RPi3 (must be in /lib/optee_armtz/)
scp ta/oat/ta/92b192d1-9686-424a-8d18-97c118129570.ta pi@<rpi3>:/lib/optee_armtz/

# Copy host app
scp host/syringe/syringe_app pi@<rpi3>:/usr/bin/
```

---

## Running on RPi3

### Syringe pump (paper evaluation)

```bash
syringe_app
```

Each of the 7 iterations prints the attestation proof and instrumentation counts:

```
[OAT] Secure Session Established.
...
[OAT] Final Execution Proof: 8088f9b2dd5f85f257...
[OAT] --- Instrumentation Statistics (per operation) ---
[OAT]   B.Cond  (branch logs):    488    (paper: 488)
[OAT]   Ret     (func exits):     1946    (paper: 1946)
[OAT]   Icall   (indirect calls): 0    (paper: 1)
[OAT] -------------------------------------------------
```

**Hash determinism**: Running twice produces the same SHA-256 hash, confirming the attestation is reproducible.

### Drone demo — normal execution

```bash
drone_app 1    # Active mode
drone_app      # Idle mode — different hash, confirms path tracking
```

### Drone demo — ROP attack simulation

```bash
# Use drone_test_bad_path (calls __oat_func_exit with wrong ID)
# Expected:
[OAT-FATAL] ROP ATTACK DETECTED! TEE blocked return.
```

<img width="1139" height="639" alt="ROP attack detection" src="https://github.com/user-attachments/assets/4273ff3e-f34d-42ab-87e6-8676f031df1e" />

---

## Local Verification (No RPi3 Required)

To verify the branch/return counts without a RPi3, build and run the syringe app locally with the counting stub:

```bash
cd host/syringe

# Generate and instrument IR
clang -S -emit-llvm -O0 -Xclang -disable-O0-optnone syringePump.c -o syringePump.ll
clang -S -emit-llvm -O0 -Xclang -disable-O0-optnone util.c -o util.ll
clang -S -emit-llvm -O0 -Xclang -disable-O0-optnone LiquidCrystal.c -o LiquidCrystal.ll
clang -S -emit-llvm -O0 -Xclang -disable-O0-optnone led.c -o led.ll
llvm-link syringePump.ll util.ll LiquidCrystal.ll led.ll -S -o syringe_combined.ll
opt -load-pass-plugin=../OATPass.so -passes=oat-pass syringe_combined.ll -S -o syringe_instrumented.ll

# Compile and run with counting stub (replaces liboat.c)
llc -filetype=obj syringe_instrumented.ll -o syringe_native.o
gcc -c liboat_counter.c -o liboat_counter.o
gcc -no-pie syringe_native.o liboat_counter.o -o syringe_count -lm
./syringe_count
# Last iteration should print: B.Cond=488, Ret=1946
```

---

## How It Works

```
Source (.c)
    │
    ▼ clang -emit-llvm
LLVM IR (.ll)
    │
    ▼ opt -passes=oat-pass  (OATPass.cpp)
Instrumented IR  ← __oat_log(), __oat_func_enter/exit(), __oat_log_indirect() injected
    │
    ▼ llc + cross-linker + liboat.o
ARM64 Binary (syringe_app / drone_app)
    │
    ▼ runs on RPi3
Normal World                    Secure World (TrustZone)
liboat.c                        oat_ta.c
  __oat_init()        ────►     CMD_HASH_INIT  — reset SHA-256, log
  __oat_log(val)      ────►     CMD_HASH_UPDATE — update hash
  __oat_func_enter()  ────►     CMD_STACK_PUSH — push to shadow stack
  __oat_func_exit()   ────►     CMD_STACK_POP  — verify + pop (ROP check)
  __oat_log_indirect()────►     CMD_INDIRECT_CALL — update hash
  __oat_print_proof() ────►     CMD_HASH_FINAL — return SHA-256 digest
```

---

## Technical Notes

- **Hash function**: SHA-256 (paper uses BLAKE-2s — hash values differ, but determinism holds)
- **Instrumentation level**: LLVM IR pass (paper uses custom ARM assembly backend)
- **TA UUID**: `92b192d1-9686-424a-8d18-97c118129570`
- **Shadow stack depth**: 128 entries (`MAX_STACK_DEPTH` in `oat_ta.c`)
- **Shadow stack scope**: Persists for full program lifetime; only hash resets per-operation
