#!/bin/bash
set -e

# --- CONFIGURATION ---
SYSROOT="/home/rajesh/latest_optee/optee_rpi3/out-br/host/aarch64-buildroot-linux-gnu/sysroot"
OPTEE_CLIENT_PATH="$SYSROOT/usr"
CROSS_CC="/home/rajesh/latest_optee/optee_rpi3/toolchains/aarch64/bin/aarch64-none-linux-gnu-gcc"
OAT_PASS="../OATPass.so"

# --- BUILD STEPS ---

# 1. Compile liboat.c (trampoline) with cross-compiler
echo "[1/6] Building liboat (Trampoline)..."
$CROSS_CC --sysroot=$SYSROOT -c ../liboat.c -o liboat.o \
    -I$OPTEE_CLIENT_PATH/include

# 2. Generate LLVM IR for each source file
echo "[2/6] Generating LLVM IR for all source files..."
CLANG_FLAGS="-S -emit-llvm -O0 -Xclang -disable-O0-optnone \
    --target=aarch64-linux-gnu \
    --sysroot=$SYSROOT \
    -I$OPTEE_CLIENT_PATH/include"

clang $CLANG_FLAGS syringePump.c -o syringePump.ll
clang $CLANG_FLAGS util.c -o util.ll
clang $CLANG_FLAGS LiquidCrystal.c -o LiquidCrystal.ll
clang $CLANG_FLAGS led.c -o led.ll

# 3. Link all IR into a single module (OAT pass needs whole-program view)
echo "[3/6] Linking IR modules..."
llvm-link syringePump.ll util.ll LiquidCrystal.ll led.ll -S -o syringe_combined.ll

# 4. Run OAT Pass on combined IR
echo "[4/6] Running OAT Pass (instrumentation)..."
opt -load-pass-plugin=$OAT_PASS -passes=oat-pass \
    syringe_combined.ll -S -o syringe_instrumented.ll

# 5. Lower to ARM64 object file
echo "[5/6] Compiling to ARM64 object..."
llc -march=aarch64 -filetype=obj syringe_instrumented.ll -o syringe.o

# 6. Link final binary
echo "[6/6] Linking final binary..."
$CROSS_CC --sysroot=$SYSROOT syringe.o liboat.o -o syringe_app \
    -L$OPTEE_CLIENT_PATH/lib -lteec -lm

echo ""
echo "=== BUILD COMPLETE ==="
echo "Binary: syringe_app"
echo ""
echo "--- Instrumentation Statistics ---"
echo "To verify against paper Table III (SP row):"
echo "  B.Cond (conditional branches):  grep -c 'call.*__oat_log' syringe_instrumented.ll"
echo "  Ret (shadow stack pops):        grep -c 'call.*__oat_func_exit' syringe_instrumented.ll"
echo "  Icall/Ijmp (indirect calls):    grep -c 'call.*__oat_log_indirect' syringe_instrumented.ll"
echo "  Func entries (shadow stack):    grep -c 'call.*__oat_func_enter' syringe_instrumented.ll"
echo ""
echo "Paper expected: B.Cond=488, Ret=1946, Icall/Ijmp=1, Def-Use=2"
echo ""

# Print actual counts
echo "--- Actual Counts from Instrumented IR ---"
BCOND=$(grep -c 'call.*__oat_log(' syringe_instrumented.ll || true)
RET=$(grep -c 'call.*__oat_func_exit' syringe_instrumented.ll || true)
ICALL=$(grep -c 'call.*__oat_log_indirect' syringe_instrumented.ll || true)
ENTER=$(grep -c 'call.*__oat_func_enter' syringe_instrumented.ll || true)

echo "  B.Cond (branch logs):     $BCOND"
echo "  Ret (func exits):         $RET"
echo "  Icall/Ijmp (indirect):    $ICALL"
echo "  Func entries:             $ENTER"
echo ""
echo "Copy 'syringe_app' to your Raspberry Pi to run."
