//===-- ObjCARC.cpp -------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file implements common infrastructure for libLLVMObjCARCOpts.a, which
// implements several scalar transformations over the LLVM intermediate
// representation, including the C bindings for that library.
//
//===----------------------------------------------------------------------===//

#include "ObjCARC.h"
#include "llvm-c/Initialization.h"
#include "llvm/Analysis/ObjCARCRVAttr.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/InlineAsm.h"
#include "llvm/IR/Instructions.h"
#include "llvm/InitializePasses.h"
#include "llvm/Transforms/Utils/BasicBlockUtils.h"

namespace llvm {
  class PassRegistry;
}

using namespace llvm;
using namespace llvm::objcarc;

/// initializeObjCARCOptsPasses - Initialize all passes linked into the
/// ObjCARCOpts library.
void llvm::initializeObjCARCOpts(PassRegistry &Registry) {
  initializeObjCARCAAWrapperPassPass(Registry);
  initializeObjCARCAPElimPass(Registry);
  initializeObjCARCExpandPass(Registry);
  initializeObjCARCContractLegacyPassPass(Registry);
  initializeObjCARCOptLegacyPassPass(Registry);
  initializePAEvalPass(Registry);
}

void LLVMInitializeObjCARCOpts(LLVMPassRegistryRef R) {
  initializeObjCARCOpts(*unwrap(R));
}

CallInst *llvm::objcarc::createCallInstWithColors(
    FunctionCallee Func, ArrayRef<Value *> Args, const Twine &NameStr,
    Instruction *InsertBefore,
    const DenseMap<BasicBlock *, ColorVector> &BlockColors) {
  FunctionType *FTy = Func.getFunctionType();
  Value *Callee = Func.getCallee();
  SmallVector<OperandBundleDef, 1> OpBundles;

  if (!BlockColors.empty()) {
    const ColorVector &CV = BlockColors.find(InsertBefore->getParent())->second;
    assert(CV.size() == 1 && "non-unique color for block!");
    Instruction *EHPad = CV.front()->getFirstNonPHI();
    if (EHPad->isEHPad())
      OpBundles.emplace_back("funclet", EHPad);
  }

  return CallInst::Create(FTy, Callee, Args, OpBundles, NameStr, InsertBefore);
}

std::pair<bool, bool>
BundledRetainClaimRVs::insertAfterInvokes(Function &F, DominatorTree *DT) {
  bool Changed = false, CFGChanged = false;

  for (BasicBlock &BB : F) {
    auto *I = dyn_cast<InvokeInst>(BB.getTerminator());

    if (!I)
      continue;

    if (!objcarc::hasRetainRVOrClaimRVAttr(I))
      continue;

    BasicBlock *DestBB = I->getNormalDest();

    if (!DestBB->getSinglePredecessor()) {
      assert(I->getSuccessor(0) == DestBB &&
             "the normal dest is expected to be the first successor");
      DestBB = llvm::SplitCriticalEdge(I, 0, CriticalEdgeSplittingOptions(DT));
      CFGChanged = true;
    }

    // We don't have to call insertRVCallWithColors since DestBB is the normal
    // destination of the invoke.
    insertRVCall(&*DestBB->getFirstInsertionPt(), I);
    Changed = true;
  }

  return std::make_pair(Changed, CFGChanged);
}

CallInst *BundledRetainClaimRVs::insertRVCall(Instruction *InsertPt,
                                              CallBase *AnnotatedCall) {
  DenseMap<BasicBlock *, ColorVector> BlockColors;
  return insertRVCallWithColors(InsertPt, AnnotatedCall, BlockColors);
}

CallInst *BundledRetainClaimRVs::insertRVCallWithColors(
    Instruction *InsertPt, CallBase *AnnotatedCall,
    const DenseMap<BasicBlock *, ColorVector> &BlockColors) {
  IRBuilder<> Builder(InsertPt);
  bool IsRetainRV = objcarc::hasRetainRVAttr(AnnotatedCall);
  Function *Func = EP.get(IsRetainRV ? ARCRuntimeEntryPointKind::RetainRV
                                     : ARCRuntimeEntryPointKind::ClaimRV);
  Type *ParamTy = Func->getArg(0)->getType();
  Value *CallArg = Builder.CreateBitCast(AnnotatedCall, ParamTy);
  auto *Call =
      createCallInstWithColors(Func, CallArg, "", InsertPt, BlockColors);
  RVCalls[Call] = AnnotatedCall;
  return Call;
}

BundledRetainClaimRVs::~BundledRetainClaimRVs() {
  if (ContractPass) {
    // At this point, we know that the annotated calls can't be tail calls as
    // they are followed by marker instructions and retainRV/claimRV calls. Mark
    // them as notail, so that the backend knows these calls can't be tail
    // calls.
    for (auto P : RVCalls)
      if (auto *CI = dyn_cast<CallInst>(P.second))
        CI->setTailCallKind(CallInst::TCK_NoTail);
  } else {
    for (auto P : RVCalls)
      EraseInstruction(P.first);
  }

  RVCalls.clear();
}
