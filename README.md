
---

# OAT: Operation Execution Integrity on Raspberry Pi 3 (OP-TEE)

## Overview
This repository contains a full implementation of **OAT (Operation Execution Integrity)** for the Raspberry Pi 3 using OP-TEE TrustZone.

The system enforces **Control Flow Integrity (CFI)** on a "Drone Controller" application using a hybrid hardware-software approach:
1.  **Forward-Edge Protection:** Tracks execution paths (branches) to ensure the application follows valid logic (e.g., Active vs. Idle modes).
2.  **Backward-Edge Protection (Shadow Stack):** Verifies function return addresses in the Secure World to prevent Return-Oriented Programming (ROP) attacks.
3.  **LLVM Instrumentation:** Automatically injects security probes into the C source code during compilation.

## Repository Structure
```text
.
├── llvm_pass/              # The "Ghost Editor"
│   └── OATPass.cpp         # LLVM optimization pass that instruments user code
├── ta/                     # The Trusted Application (Secure World)
│   └── oat/ta/             # Source code for the Shadow Stack & Hashing Logic
├── host/                   # The User Application (Normal World)
│   ├── drone_test.c        # The main drone logic (Target Application)
│   ├── liboat.c            # The Trampoline Library (Talks to TrustZone)
│   └── build_rpi.sh        # Cross-compilation script
└── screenshots/            # Proof of execution and attack simulation


## Prerequisites

* **Hardware:** Raspberry Pi 3 Model B/B+
* **OS:** Linux (Buildroot or Raspbian) running OP-TEE.
* **Host Machine:** Linux (Ubuntu 20.04/22.04 recommended) with the OP-TEE build environment installed.

---

## Build Instructions

### 1. Setup Environment Variables

Before building, you must export the paths to your OP-TEE toolchains. Adjust these paths to match your specific installation.

```bash
# 1. The ARM Cross-Compiler (AArch64)
export CROSS_COMPILE=$HOME/optee_rpi3/toolchains/aarch64/bin/aarch64-none-linux-gnu-

# 2. The Trusted Application Dev Kit
# Located in: optee_os/out/arm/export-ta_arm64
export TA_DEV_KIT_DIR=$HOME/optee_rpi3/optee_os/out/arm/export-ta_arm64

# 3. The OP-TEE Client Sysroot (For Host App headers)
export OPTEE_CLIENT_PATH=$HOME/optee_rpi3/out-br/host/aarch64-buildroot-linux-gnu/sysroot/usr

```

### 2. Build the Trusted Application (Secure World)

This builds the binary that runs inside TrustZone.

```bash
cd ta/oat/ta
make clean
make

# Output Artifact: 92b192d1-9686-424a-8d18-97c118129570.ta

```

### 3. Build the LLVM Pass

This builds the compiler plugin used to instrument the host code.

```bash
cd ../../../llvm_pass
# Ensure clang and llvm-dev are installed
clang++ -shared -fPIC -o OATPass.so OATPass.cpp `llvm-config --cxxflags --ldflags --libs`

```

### 4. Build the Host Application (Normal World)

This script cross-compiles the drone logic, runs the LLVM Pass to inject probes, and links it with the OAT library.

1. Open `host/build_rpi.sh` and ensure the `SYSROOT` variable matches your `OPTEE_CLIENT_PATH`.
2. Run the build script:

```bash
cd ../host
chmod +x build_rpi.sh
./build_rpi.sh

# Output Artifact: drone_app (ARM64 Binary)

```

---

## Deployment & Usage

### 1. Transfer Files to Raspberry Pi

Copy the compiled binaries to your Raspberry Pi's root filesystem (SD card).

* **Trusted App (`.ta`):** Must be placed in `/lib/optee_armtz/`
* **Host App (`drone_app`):** Can be placed anywhere (e.g., `/usr/bin/`)

```bash
sudo cp ../ta/oat/ta/92b192d1-9686-424a-8d18-97c118129570.ta /media/$USER/rootfs/lib/optee_armtz/
sudo cp drone_app /media/$USER/rootfs/usr/bin/
sync

```

### 2. Verification (The "Happy Path")

Boot the Raspberry Pi and run the application. The OAT system will silently verify the Control Flow Graph (CFG) in the background.

```bash
# Run in "Active Mode"
drone_app 1

```

**Expected Output:**

```text
[OAT] Secure Session Established.
Mode: ACTIVE flight
...
[OAT] Final Execution Proof: <HASH_VALUE_A>

```

Running `drone_app` (without arguments) will trigger the "Idle Mode" path, producing a **different** Final Execution Proof hash, confirming that TrustZone is tracking the decision logic.

### 3. Attack Simulation (The ROP Attack)

To demonstrate the security, we can simulate a stack hijacking attempt where a function tries to return with a mismatched ID.

1. Edit `host/drone_test.c` and uncomment the `attempt_hack()` function call.
2. Rebuild using `./build_rpi.sh`.
3. Run on the Pi.

**Expected Output (Security Block):**

```text
[!!!] SIMULATING ROP ATTACK [!!!]
Attacker tries to force a return with WRONG ID...

# [OAT-FATAL] ROP ATTACK DETECTED! TEE blocked return.

```
<img width="1139" height="639" alt="attack" src="https://github.com/user-attachments/assets/4273ff3e-f34d-42ab-87e6-8676f031df1e" />


*The application is immediately terminated by the secure monitor.*

---

## Technical Details

* **UUID:** `92b192d1-9686-424a-8d18-97c118129570`
* **Shadow Stack:** Implemented inside the TA. Pushes Function IDs on entry, pops and compares on exit.
* **Branch Profiling:** Every `if/else` decision updates a running SHA-256 hash inside the TA.

