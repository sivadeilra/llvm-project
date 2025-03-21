//===------ WindowsHotPatch.cpp - Support for Windows hotpatching ---------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Marks functions with the `marked_for_windows_hot_patching` attribute.
//
//===----------------------------------------------------------------------===//

#include "llvm/ADT/SmallSet.h"
#include "llvm/CodeGen/Passes.h"
#include "llvm/InitializePasses.h"
#include "llvm/IR/Attributes.h"
#include "llvm/IR/DIBuilder.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/InstIterator.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/Module.h"
#include "llvm/Pass.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/LineIterator.h"
#include "llvm/Support/MemoryBuffer.h"

using namespace llvm;

#define DEBUG_TYPE "windows-hot-patch"

// A file containing list of mangled function names to mark for hot patching.
static cl::opt<std::string>
    HotPatchFunctionsFile("windows-hot-patch-function-file", cl::value_desc("filename"),
            cl::desc("A file containing list of mangled function names to mark for hot patching"));

// A list of mangled function names to mark for hot patching.
static cl::list<std::string>
    HotPatchFunctionsList("windows-hot-patch-function-list", cl::value_desc("list"),
            cl::desc("A list of mangled function names to mark for hot patching"), cl::CommaSeparated);

namespace {

class WindowsHotPatch : public ModulePass {
  struct GlobalVariableUse {
    GlobalVariable *GV;
    Instruction *User;
    unsigned Op;
  };

public:
  static char ID;

  WindowsHotPatch() : ModulePass(ID) {
    initializeWindowsHotPatchPass(*PassRegistry::getPassRegistry());
  }

  void getAnalysisUsage(AnalysisUsage &AU) const override {
    AU.setPreservesCFG();
  }

  bool runOnModule(Module &M) override;

private:
  void runOnFunction(Function &F, SmallDenseMap<GlobalVariable *, GlobalVariable *> &RefMapping);
  void replaceGlobalVariableUses(Function &F, SmallVectorImpl<GlobalVariableUse> &GVUses, SmallDenseMap<GlobalVariable *, GlobalVariable *> &RefMapping, DIBuilder &DebugInfo);
};

} // end anonymous namespace

char WindowsHotPatch::ID = 0;

INITIALIZE_PASS(WindowsHotPatch, "windows-hot-patch",
                "Windows hot patch support", false, false)
ModulePass *llvm::createWindowsHotPatch() { return new WindowsHotPatch(); }

bool WindowsHotPatch::runOnModule(Module &M) {
  bool MadeChanges = false;

  // Process the command line options to find functions to process for hot patching.
  if (!HotPatchFunctionsList.empty() || !HotPatchFunctionsFile.empty()) {
    std::shared_ptr<MemoryBuffer> Buf;
    SmallSet<StringRef, 32> HotPatchFunctions;

    for (auto FunctionName : HotPatchFunctionsList)
      HotPatchFunctions.insert(FunctionName);

    if (!HotPatchFunctionsFile.empty()) {
      ErrorOr<std::unique_ptr<MemoryBuffer>> BufOrErr =
          MemoryBuffer::getFile(HotPatchFunctionsFile);
      if (BufOrErr) {
        Buf = std::move(*BufOrErr);
        for (line_iterator I(Buf.get()->getMemBufferRef(), true), E; I != E; ++I)
          HotPatchFunctions.insert(*I);
      } else {
        report_fatal_error(Twine("Windows hot patching couldn't load file '").concat(HotPatchFunctionsFile).concat("': ").concat(BufOrErr.getError().message()).concat(".\n"));
      }
    }

    // Scan through all of the functions and process any who's names match.
    SmallDenseMap<GlobalVariable *, GlobalVariable *> RefMapping;
    for (auto &F : M.functions()) {
      if (HotPatchFunctions.contains(F.getName())) {
        // Mark for hotpatching: this will cause a S_HOTPATCHFUNC debug symbol to be emitted.
        F.addFnAttr(Attribute::MarkedForWindowsHotPatching);

        runOnFunction(F, RefMapping);
        MadeChanges = true;
      }
    }
  }
  return MadeChanges;
}

void WindowsHotPatch::runOnFunction(Function &F, SmallDenseMap<GlobalVariable *, GlobalVariable *> &RefMapping) {
  SmallVector<GlobalVariableUse, 32> GVUses;
  for (auto &I : instructions(F)) {
    for (auto &U : I.operands()) {
      // Discover all uses of GlobalVariable, these will need to be replaced.
      GlobalVariable *GV = dyn_cast<GlobalVariable>(&U);
      if ((GV != nullptr) && !GV->hasAttribute(Attribute::AllowDirectAccessInHotPatchFunction)) {
        unsigned OpNo = &U - I.op_begin();
        GVUses.push_back({ GV, &I, OpNo });
      }
    }
  }

  if (!GVUses.empty()) {
    DIBuilder DebugInfo{*F.getParent(), true, F.getSubprogram()->getUnit()};
    replaceGlobalVariableUses(F, GVUses, RefMapping, DebugInfo);
    DebugInfo.finalize();
  }
}

void WindowsHotPatch::replaceGlobalVariableUses(Function &F, SmallVectorImpl<GlobalVariableUse> &GVUses, SmallDenseMap<GlobalVariable *, GlobalVariable *> &RefMapping, DIBuilder &DebugInfo) {
  for (auto& GVUse : GVUses) {
    IRBuilder<> Builder(GVUse.User);

    // Get or create a new global variable that points to the old one and who's
    // name begins with `__ref_`.
    GlobalVariable *&ReplaceWithRefGV = RefMapping.try_emplace(GVUse.GV).first->second;
    if (ReplaceWithRefGV == nullptr) {
      Constant *AddrOfOldGV = ConstantExpr::getGetElementPtr(Builder.getPtrTy(), GVUse.GV, ArrayRef<Value *>{});
      ReplaceWithRefGV = new GlobalVariable(
          *F.getParent(), Builder.getPtrTy(), true, GlobalValue::InternalLinkage, AddrOfOldGV,
          Twine("__ref_").concat(GVUse.GV->getName()), nullptr, GlobalVariable::NotThreadLocal);

      // Create debug info for the replacement global variable.
      DISubprogram *SP = F.getSubprogram();
      DataLayout Layout = F.getParent()->getDataLayout();
      DIType *DebugType = DebugInfo.createPointerType(nullptr, Layout.getTypeSizeInBits(GVUse.GV->getValueType()));
      DIGlobalVariableExpression *GVE = DebugInfo.createGlobalVariableExpression(SP->getUnit(), ReplaceWithRefGV->getName(), StringRef{}, SP->getFile(), /*LineNo*/ 0, DebugType, /*IsLocalToUnit*/false);
      ReplaceWithRefGV->addDebugInfo(GVE);
    }

    // Now replace the use of that global variable with the new one (via a load
    // since it is a pointer to the old global variable).
    LoadInst *LoadedRefGV = Builder.CreateLoad(ReplaceWithRefGV->getValueType(), ReplaceWithRefGV);
    GVUse.User->setOperand(GVUse.Op, LoadedRefGV);
  }
}
