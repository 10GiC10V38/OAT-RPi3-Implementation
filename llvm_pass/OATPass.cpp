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

    // --- SETUP: Get Function Signatures ---
    // 1. For Branches: void __oat_log(int val)
    FunctionCallee logFunc = F.getParent()->getOrInsertFunction(
        "__oat_log", Type::getVoidTy(Ctx), Type::getInt32Ty(Ctx));

    // 2. For Shadow Stack: void __oat_func_enter(int id), void __oat_func_exit(int id)
    FunctionCallee enterFunc = F.getParent()->getOrInsertFunction(
        "__oat_func_enter", Type::getVoidTy(Ctx), Type::getInt32Ty(Ctx));

    FunctionCallee exitFunc = F.getParent()->getOrInsertFunction(
        "__oat_func_exit", Type::getVoidTy(Ctx), Type::getInt32Ty(Ctx));

    // --- PART 1: BACKWARD-EDGE (Shadow Stack) ---
    
    // Generate a Unique ID for this function (Simple Hash of name)
    uint32_t funcID = 0;
    StringRef name = F.getName();
    for(char c : name) funcID += c; 

    // Inject ENTRY Probe (Push ID)
    BasicBlock &EntryBB = F.getEntryBlock();
    IRBuilder<> BuilderEntry(&*EntryBB.getFirstInsertionPt());
    BuilderEntry.CreateCall(enterFunc, {BuilderEntry.getInt32(funcID)});
    modified = true;

    // Inject EXIT Probe (Pop ID) - Scan for Returns
    for (auto &BB : F) {
      if (ReturnInst *RI = dyn_cast<ReturnInst>(BB.getTerminator())) {
        IRBuilder<> BuilderExit(RI); 
        BuilderExit.CreateCall(exitFunc, {BuilderExit.getInt32(funcID)});
      }
      
      // --- PART 2: FORWARD-EDGE (Branches) ---
      // This is your existing logic, kept intact
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
    }

    return modified ? PreservedAnalyses::none() : PreservedAnalyses::all();
  }
};

} // namespace

// Registration
extern "C" LLVM_ATTRIBUTE_WEAK ::llvm::PassPluginLibraryInfo
llvmGetPassPluginInfo() {
  return {
    LLVM_PLUGIN_API_VERSION, "OATPass", "v0.2",
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
