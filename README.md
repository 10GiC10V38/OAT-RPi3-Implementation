# OAT: Operation Execution Integrity on Raspberry Pi 3

Implementation of the OAT attestation system described in:

> **OAT: Attesting Operation Integrity of Embedded Devices**
> Zhichuang Sun, Bo Feng, Long Lu, Somesh Jha — IEEE S&P 2020
> [arXiv:1802.03462](https://arxiv.org/abs/1802.03462)

Built on Raspberry Pi 3 using ARM TrustZone via OP-TEE. Includes a port of the paper's **Syringe Pump** evaluation program with verified results matching Table III.

---

## What This Implements

OAT attests that an operation on an embedded device executed without control-flow manipulation:

- **Forward-edge CFI** — every conditional branch and indirect call is logged to a running SHA-256 hash inside the Secure World (TEE)
- **Backward-edge CFI (Shadow Stack)** — every function entry/exit is verified inside the TEE; mismatches indicate ROP and terminate the program
- **LLVM compiler pass** — automatically injects instrumentation probes at compile time; no manual source changes required

> This implementation covers CFI only. CVI (Critical Variable Integrity for data-only attacks) from the paper is not implemented.

---

## Repository Structure

```
.
├── llvm_pass/
│   └── OATPass.cpp              # LLVM IR pass — injects __oat_log, __oat_func_enter/exit,
│                                #   __oat_log_indirect into the target app at compile time
│
├── ta/oat/ta/
│   ├── oat_ta.c                 # Trusted Application (runs in TrustZone Secure World)
│   │                            #   — SHA-256 hash of all execution events
│   │                            #   — shadow stack for ROP detection
│   ├── include/oat_ta.h         # TEE command IDs and constants
│   ├── sub.mk                   # OP-TEE build source list
│   └── Makefile                 # Builds the .ta binary via TA Dev Kit
│
├── host/
│   ├── liboat.c                 # Trampoline library — bridges instrumented app to TEE
│   ├── drone_test.c             # Demo app: drone controller (indirect call CFI)
│   ├── drone_test_bad_path.c    # Demo app: simulates ROP attack
│   ├── build_rpi.sh             # Build pipeline for drone app
│   └── syringe/                 # Syringe pump (paper's evaluation target)
│       ├── syringePump.c        # Ported from paper's reference — uses __oat_* API
│       ├── util.c               # Hardware stubs (GPIO, Serial — print to stdout)
│       ├── LiquidCrystal.c      # LCD stub
│       ├── led.c                # LED stub
│       ├── lib/                 # Stub headers
│       └── build_syringe.sh     # Build pipeline for syringe app
│
├── verifier/
│   └── verify_mission.py        # Parses execution log, replays hash, verifies proof
│
├── docs/
│   ├── results.md               # Syringe pump results vs paper Table III
│   └── observations.md          # Challenges, bugs found, design decisions
│
├── screenshots/                 # Output from RPi3 runs
└── 1802.03462v3.pdf             # Reference paper
```

---

## Verification Results (Paper Table III — Syringe Pump)

| Metric | Paper | This Implementation | Status |
|---|---|---|---|
| B.Cond (conditional branches) | 488 | **488** | Exact match |
| Ret (function returns) | 1946 | **1946** | Exact match |
| Icall/Ijmp | 1 | 0 | See [docs/results.md](docs/results.md) |
| Def-Use (CVI) | 2 | 0 | Not implemented |
| Runtime overhead | 1.9% | ~2–5% | Close |

See [docs/results.md](docs/results.md) for the full analysis and [docs/observations.md](docs/observations.md) for challenges encountered during implementation.

---

## Prerequisites

**Hardware:** Raspberry Pi 3 Model B/B+, running OP-TEE

**Build machine:** Linux with:
- `clang` + `llvm-dev` (tested with LLVM 18)
- OP-TEE toolchain built for AArch64

---

## Build

### 1. Set environment variables

```bash
export CROSS_COMPILE=<optee_path>/toolchains/aarch64/bin/aarch64-none-linux-gnu-
export TA_DEV_KIT_DIR=<optee_path>/optee_os/out/arm/export-ta_arm64
export SYSROOT=<optee_path>/out-br/host/aarch64-buildroot-linux-gnu/sysroot
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
```

### 4. Build the host application

**Drone demo:**
```bash
cp llvm_pass/OATPass.so host/
cd host && ./build_rpi.sh
# Output: drone_app
```

**Syringe pump (paper evaluation):**
```bash
cp llvm_pass/OATPass.so host/
cd host/syringe && ./build_syringe.sh
# Output: syringe_app
```

---

## Deploy to Raspberry Pi

```bash
# Trusted Application must be in /lib/optee_armtz/
scp ta/oat/ta/92b192d1-9686-424a-8d18-97c118129570.ta pi@<rpi3>:/lib/optee_armtz/

# Host application
scp host/syringe/syringe_app pi@<rpi3>:/usr/bin/
```

---

## Running on RPi3

### Syringe pump

```bash
syringe_app
```

Each iteration prints the per-operation attestation proof and event counts:

```
[OAT] Secure Session Established.
...
[OAT] Final Execution Proof: 8088f9b2dd5f85f257435b3d9822103f...
[OAT] --- Instrumentation Statistics (per operation) ---
[OAT]   B.Cond  (branch logs):    488    (paper: 488)
[OAT]   Ret     (func exits):     1946    (paper: 1946)
[OAT]   Icall   (indirect calls): 0    (paper: 1)
[OAT] -------------------------------------------------
```

Running twice produces the **same hash**, confirming deterministic attestation.

### Drone demo — normal execution

```bash
drone_app 1    # Active mode
drone_app      # Idle mode — different hash confirms path tracking works
```

### Drone demo — ROP attack simulation

Build from `drone_test_bad_path.c` (calls `__oat_func_exit` with a mismatched ID):

```
[OAT-FATAL] ROP ATTACK DETECTED! TEE blocked return.
```

<img alt="ROP attack detection" src="https://github.com/user-attachments/assets/4273ff3e-f34d-42ab-87e6-8676f031df1e" />

---

## How It Works

```
Source (.c)
    │
    ▼  clang -emit-llvm
LLVM IR
    │
    ▼  opt -passes=oat-pass  (OATPass.cpp)
Instrumented IR  ←  __oat_log / __oat_func_enter/exit / __oat_log_indirect injected
    │
    ▼  llc + cross-linker + liboat.o
ARM64 Binary
    │
    ▼  runs on RPi3
Normal World (liboat.c)          Secure World (oat_ta.c)
  __oat_init()         ───►      Reset hash + log
  __oat_log(val)       ───►      Update SHA-256
  __oat_func_enter(id) ───►      Push to shadow stack
  __oat_func_exit(id)  ───►      Pop + verify (ROP check)
  __oat_log_indirect() ───►      Update SHA-256
  __oat_print_proof()  ───►      Finalize + return digest
```

---

## Technical Notes

| Item | Detail |
|---|---|
| TA UUID | `92b192d1-9686-424a-8d18-97c118129570` |
| Hash function | SHA-256 (paper uses BLAKE-2s — values differ, determinism holds) |
| Instrumentation level | LLVM IR pass (paper uses custom ARM assembly backend) |
| Shadow stack depth | 128 entries |
| Shadow stack scope | Persists for full program lifetime; only hash resets per-operation |
