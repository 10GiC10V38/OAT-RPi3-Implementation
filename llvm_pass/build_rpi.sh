#!/bin/bash

# --- CONFIGURATION ---
# 1. The root of the Target Filesystem (The "Sysroot")
#    This is where libc, libm, and other standard libraries live.
SYSROOT="/home/rajesh/latest_optee/optee_rpi3/out-br/host/aarch64-buildroot-linux-gnu/sysroot"

# 2. OP-TEE specific paths (inside the sysroot)
OPTEE_CLIENT_PATH="$SYSROOT/usr"

# 3. The Cross Compiler
CROSS_CC="/home/rajesh/latest_optee/optee_rpi3/toolchains/aarch64/bin/aarch64-none-linux-gnu-gcc"

# --- BUILD STEPS ---

# 1. Compile Helper Library (Trampoline)
#    Added --sysroot so it finds standard headers correctly
echo "[1] Building liboat (Trampoline)..."
$CROSS_CC --sysroot=$SYSROOT -c liboat.c -o liboat.o \
    -I$OPTEE_CLIENT_PATH/include

# 2. Generate ARM LLVM IR for Drone App
echo "[2] Generating ARM IR..."
clang -S -emit-llvm -O0 -Xclang -disable-O0-optnone \
    --target=aarch64-linux-gnu \
    --sysroot=$SYSROOT \
    -I$OPTEE_CLIENT_PATH/include \
    drone_test.c -o drone.ll

# 3. Run the "Ghost Editor" (OAT Pass)
echo "[3] Running OAT Pass..."
opt -load-pass-plugin=./OATPass.so -passes=oat-pass drone.ll -S -o drone_instrumented.ll

# 4. Convert IR to ARM Assembly
echo "[4] Converting to ARM Assembly..."
llc -march=aarch64 -filetype=obj drone_instrumented.ll -o drone.o

# 5. Link Final Binary
#    CRITICAL FIX: Added --sysroot here so the linker finds libc.so.6
echo "[5] Linking Final Binary..."
$CROSS_CC --sysroot=$SYSROOT drone.o liboat.o -o drone_app \
    -L$OPTEE_CLIENT_PATH/lib -lteec

echo "DONE! Copy 'drone_app' to your Raspberry Pi."
