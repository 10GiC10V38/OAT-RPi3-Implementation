/* llvm_pass/OATPass.cpp */
#include "llvm/IR/PassManager.h"
#include "llvm/Passes/PassBuilder.h"
#include "llvm/Passes/PassPlugin.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/Instructions.h"
#include "llvm/Support/raw_ostream.h"

using namespace llvm;

namespace {

struct OATPass : public PassInfoMixin<OATPass> {
  
  PreservedAnalyses run(Function &F, FunctionAnalysisManager &FAM) {
    LLVMContext &Ctx = F.getContext();
    bool modified = false;

    // --- 1. Register Helper Functions ---
    
    // void __oat_log(int val)
    FunctionCallee logFunc = F.getParent()->getOrInsertFunction(
        "__oat_log", Type::getVoidTy(Ctx), Type::getInt32Ty(Ctx));

    // void __oat_log_indirect(uint64_t addr)
    FunctionCallee logIndirectFunc = F.getParent()->getOrInsertFunction(
        "__oat_log_indirect", Type::getVoidTy(Ctx), Type::getInt64Ty(Ctx));

    // Shadow Stack: enter/exit
    FunctionCallee enterFunc = F.getParent()->getOrInsertFunction(
        "__oat_func_enter", Type::getVoidTy(Ctx), Type::getInt32Ty(Ctx));

    FunctionCallee exitFunc = F.getParent()->getOrInsertFunction(
        "__oat_func_exit", Type::getVoidTy(Ctx), Type::getInt32Ty(Ctx));

    // --- 2. Instrument Entry (Shadow Stack Push) ---
    uint32_t funcID = 0;
    StringRef name = F.getName();
    for(char c : name) funcID += c; // Simple ID generation

    BasicBlock &EntryBB = F.getEntryBlock();
    IRBuilder<> BuilderEntry(&*EntryBB.getFirstInsertionPt());
    BuilderEntry.CreateCall(enterFunc, {BuilderEntry.getInt32(funcID)});
    modified = true;

    // --- 3. Scan Body for Returns and Indirect Calls ---
    for (auto &BB : F) {
      
      // A. Shadow Stack Pop (Before Returns)
      if (ReturnInst *RI = dyn_cast<ReturnInst>(BB.getTerminator())) {
        IRBuilder<> BuilderExit(RI); 
        BuilderExit.CreateCall(exitFunc, {BuilderExit.getInt32(funcID)});
      }
      
      // B. Branch Logging (Forward Edge)
      Instruction *Term = BB.getTerminator();
      if (BranchInst *BI = dyn_cast<BranchInst>(Term)) {
        if (BI->isConditional()) {
          BasicBlock *TrueDest = BI->getSuccessor(0);
          IRBuilder<> BuilderTrue(&*TrueDest->getFirstInsertionPt());
          BuilderTrue.CreateCall(logFunc, {BuilderTrue.getInt32(1)});

          BasicBlock *FalseDest = BI->getSuccessor(1);
          IRBuilder<> BuilderFalse(&*FalseDest->getFirstInsertionPt());
          BuilderFalse.CreateCall(logFunc, {BuilderFalse.getInt32(0)});
        }
      }

      // C. Indirect Call Logging (Forward Edge - Indirect)
      for (auto &I : BB) {
        if (auto *CI = dyn_cast<CallBase>(&I)) {
            // Check if it is an Indirect Call (Function Pointer)
            if (CI->isIndirectCall()) {
                IRBuilder<> Builder(CI);
                
                // Get the target pointer (where it's jumping)
                Value *targetPtr = CI->getCalledOperand();
                
                // Cast pointer to Int64
                Value *targetInt = Builder.CreatePtrToInt(targetPtr, Builder.getInt64Ty());
                
                // Inject __oat_log_indirect(target_addr) BEFORE the call
                Builder.CreateCall(logIndirectFunc, {targetInt});
                modified = true;
            }
        }
      }
    }

    return modified ? PreservedAnalyses::none() : PreservedAnalyses::all();
  }
};

} // namespace

extern "C" LLVM_ATTRIBUTE_WEAK ::llvm::PassPluginLibraryInfo
llvmGetPassPluginInfo() {
  return {
    LLVM_PLUGIN_API_VERSION, "OATPass", "v0.3",
    [](PassBuilder &PB) {
      PB.registerPipelineParsingCallback(
        [](StringRef Name, FunctionPassManager &FPM,
           ArrayRef<PassBuilder::PipelineElement>) {
          if (Name == "oat-pass") {
            FPM.addPass(OATPass());
            return true;
          }
          return false;
        });
    }
  };
}
