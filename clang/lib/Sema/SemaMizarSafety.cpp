//===--- SemaMizarSafety.cpp - Demand-driven safety inference ---*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file implements demand-driven safety inference for the Mizar safe-C++
// extensions. When a safe context calls an unspecified (unannotated) function,
// the compiler walks the callee's body to determine if it is effectively safe.
//
// The algorithm:
//  1. Check explicit annotations first (Safe/Unsafe).
//  2. Check the memoization cache (avoids redundant work).
//  3. If no body is visible, return Unknown (permissive for v1).
//  4. Mark InProgress in the cache (cycle detection).
//  5. Walk the body with UnsafeOperationVisitor.
//  6. Cache and return the result.
//
// Unsafe operations detected (v1 narrow list):
//  - Raw pointer dereference (*p)
//  - reinterpret_cast
//  - Inline assembly (GCC and MS)
//
// The visitor skips:
//  - unsafe { } blocks (explicit escape hatches)
//  - unsafe(expr) expressions (explicit escape hatches)
//  - Lambda bodies (separate analysis units)
//
//===----------------------------------------------------------------------===//

#include "clang/Sema/Sema.h"
#include "clang/AST/Decl.h"
#include "clang/AST/DeclCXX.h"
#include "clang/AST/DynamicRecursiveASTVisitor.h"
#include "clang/AST/Expr.h"
#include "clang/AST/ExprCXX.h"
#include "clang/AST/Stmt.h"
#include "clang/AST/Type.h"
#include "clang/Basic/DiagnosticSema.h"
#include "clang/Basic/Specifiers.h"

using namespace clang;

//===----------------------------------------------------------------------===//
// UnsafeOperationVisitor
//===----------------------------------------------------------------------===//

namespace {

/// Walks a function body looking for operations that are inherently unsafe
/// from a memory-safety perspective. Stops at the first unsafe operation found.
///
/// The visitor respects safety boundaries:
///  - unsafe { } blocks are skipped (the programmer acknowledged the risk).
///  - unsafe(expr) expressions are skipped (same).
///  - Lambda/block bodies are skipped (they are separate analysis units).
struct UnsafeOperationVisitor : DynamicRecursiveASTVisitor {
  Sema &S;

  using ReasonKind = Sema::MizarInferenceResult::ReasonKind;

  bool FoundUnsafe = false;
  ReasonKind Reason = ReasonKind::NoReason;
  SourceLocation UnsafeLoc;
  const FunctionDecl *UnsafeCallee = nullptr;

  explicit UnsafeOperationVisitor(Sema &S) : S(S) {
    ShouldVisitImplicitCode = true;
    ShouldWalkTypesOfTypeLocs = false;
    ShouldVisitLambdaBody = false;
  }

  /// Mark the function as unsafe and stop traversal.
  void markUnsafe(ReasonKind R, SourceLocation Loc,
                  const FunctionDecl *Callee = nullptr) {
    FoundUnsafe = true;
    Reason = R;
    UnsafeLoc = Loc;
    UnsafeCallee = Callee;
  }

  /// Build the inference result from the visitor's state.
  Sema::MizarInferenceResult getResult() const {
    Sema::MizarInferenceResult R;
    R.Safety = FoundUnsafe ? InferredSafety::Unsafe : InferredSafety::Safe;
    R.Reason = Reason;
    R.UnsafeLoc = UnsafeLoc;
    R.UnsafeCallee = UnsafeCallee;
    return R;
  }

  //===--------------------------------------------------------------------===//
  // Safety boundary overrides — skip escape hatches
  //===--------------------------------------------------------------------===//

  /// Skip the body of unsafe { } blocks. The programmer has explicitly
  /// acknowledged that the code inside is unsafe.
  bool TraverseMizarSafetyStmt(MizarSafetyStmt *Stmt) override {
    if (Stmt->isUnsafe())
      return true; // Skip: unsafe block is an escape hatch.
    // For safe { } blocks, traverse normally — they don't suppress inference.
    return DynamicRecursiveASTVisitor::TraverseMizarSafetyStmt(Stmt);
  }

  /// Skip the operand of unsafe(expr). Same logic as unsafe { } blocks.
  bool TraverseMizarUnsafeExpr(MizarUnsafeExpr *E) override {
    return true; // Skip: unsafe(expr) is an escape hatch.
  }

  /// Skip lambda bodies — they are separate analysis units.
  bool TraverseLambdaExpr(LambdaExpr *Lambda) override {
    // Visit captures (they execute in the outer function's context).
    for (unsigned I = 0, N = Lambda->capture_size(); I < N; ++I)
      if (!TraverseLambdaCapture(Lambda, Lambda->capture_begin() + I,
                                 Lambda->capture_init_begin()[I]))
        return false;
    return true;
  }

  /// Skip block expression bodies.
  bool TraverseBlockExpr(BlockExpr *) override { return true; }

  //===--------------------------------------------------------------------===//
  // Unsafe operation detection
  //===--------------------------------------------------------------------===//

  /// Detect raw pointer dereference: *p where p is a pointer to data.
  bool VisitUnaryOperator(UnaryOperator *E) override {
    if (FoundUnsafe)
      return false;

    if (E->getOpcode() == UO_Deref) {
      QualType SubTy = E->getSubExpr()->getType();
      // Only flag data pointer dereferences, not function pointer dereferences.
      // Function pointer calls are handled separately.
      if (SubTy->isPointerType() && !SubTy->getPointeeType()->isFunctionType()) {
        markUnsafe(ReasonKind::PtrDeref, E->getOperatorLoc());
        return false;
      }
    }
    return true;
  }

  /// Detect reinterpret_cast<T>(expr).
  bool VisitCXXReinterpretCastExpr(CXXReinterpretCastExpr *E) override {
    if (FoundUnsafe)
      return false;
    markUnsafe(ReasonKind::ReinterpretCast, E->getBeginLoc());
    return false;
  }

  /// Detect GCC-style inline assembly: asm("...").
  bool VisitGCCAsmStmt(GCCAsmStmt *Stmt) override {
    if (FoundUnsafe)
      return false;
    markUnsafe(ReasonKind::InlineAsm, Stmt->getAsmLoc());
    return false;
  }

  /// Detect Microsoft-style inline assembly: __asm { ... }.
  bool VisitMSAsmStmt(MSAsmStmt *Stmt) override {
    if (FoundUnsafe)
      return false;
    markUnsafe(ReasonKind::InlineAsm, Stmt->getAsmLoc());
    return false;
  }

  //===--------------------------------------------------------------------===//
  // Call propagation — calls to unsafe/inferred-unsafe functions
  //===--------------------------------------------------------------------===//

  /// Check a function callee's safety status and propagate unsafety.
  void checkCallee(const FunctionDecl *Callee, SourceLocation CallLoc) {
    if (!Callee)
      return;

    const auto *FPT = Callee->getType()->getAs<FunctionProtoType>();
    if (!FPT)
      return;

    FunctionSafetyKind Explicit = FPT->getSafetySpecifier();

    if (Explicit == FunctionSafetyKind::Safe)
      return; // Explicitly safe — allowed.

    if (Explicit == FunctionSafetyKind::Unsafe) {
      markUnsafe(ReasonKind::CallsUnsafe, CallLoc, Callee);
      return;
    }

    // Unspecified — recursively infer.
    assert(Explicit == FunctionSafetyKind::Unspecified);
    Sema::MizarInferenceResult Result = S.InferFunctionSafety(Callee);

    if (Result.Safety == InferredSafety::Unsafe) {
      markUnsafe(ReasonKind::CallsInferredUnsafe, CallLoc, Callee);
    }
    // Safe, Unknown, or InProgress — continue (optimistic for cycles).
  }

  /// Handle regular function calls (including operator calls).
  bool VisitCallExpr(CallExpr *E) override {
    if (FoundUnsafe)
      return false;
    checkCallee(E->getDirectCallee(), E->getBeginLoc());
    return !FoundUnsafe;
  }

  /// Handle constructor calls.
  bool VisitCXXConstructExpr(CXXConstructExpr *E) override {
    if (FoundUnsafe)
      return false;
    checkCallee(E->getConstructor(), E->getLocation());
    return !FoundUnsafe;
  }
};

} // anonymous namespace

//===----------------------------------------------------------------------===//
// Sema::InferFunctionSafety
//===----------------------------------------------------------------------===//

Sema::MizarInferenceResult
Sema::InferFunctionSafety(const FunctionDecl *FD) {
  if (!FD)
    return {}; // Unknown.

  const FunctionDecl *CanonFD = FD->getCanonicalDecl();

  // 1. Check the cache first.
  auto CacheIt = SafetyInferenceCache.find(CanonFD);
  if (CacheIt != SafetyInferenceCache.end())
    return CacheIt->second;

  // 2. Check explicit annotations.
  const auto *FPT = FD->getType()->getAs<FunctionProtoType>();
  if (FPT) {
    FunctionSafetyKind Explicit = FPT->getSafetySpecifier();
    if (Explicit == FunctionSafetyKind::Safe) {
      MizarInferenceResult R;
      R.Safety = InferredSafety::Safe;
      SafetyInferenceCache[CanonFD] = R;
      return R;
    }
    if (Explicit == FunctionSafetyKind::Unsafe) {
      MizarInferenceResult R;
      R.Safety = InferredSafety::Unsafe;
      SafetyInferenceCache[CanonFD] = R;
      return R;
    }
  }

  // 3. Find the definition with a body.
  const FunctionDecl *DefFD = nullptr;
  if (!FD->hasBody(DefFD)) {
    // No visible body — treat as Unknown (permissive for v1).
    // Future: consider treating as Unsafe with a flag.
    MizarInferenceResult R;
    R.Safety = InferredSafety::Unknown;
    SafetyInferenceCache[CanonFD] = R;
    return R;
  }

  // 4. Mark InProgress for cycle detection.
  {
    MizarInferenceResult InProg;
    InProg.Safety = InferredSafety::InProgress;
    SafetyInferenceCache[CanonFD] = InProg;
  }

  // 5. Walk the body looking for unsafe operations.
  UnsafeOperationVisitor Visitor(*this);
  Visitor.TraverseStmt(DefFD->getBody());

  // 6. Cache and return the result.
  MizarInferenceResult Result = Visitor.getResult();
  SafetyInferenceCache[CanonFD] = Result;
  return Result;
}

//===----------------------------------------------------------------------===//
// Sema::CheckMizarSafetyForCall (updated with inference)
//===----------------------------------------------------------------------===//

void Sema::CheckMizarSafetyForCall(SourceLocation CallLoc,
                                   const FunctionDecl *Callee) {
  if (!Callee || !isInSafeContext())
    return;

  const auto *FPT = Callee->getType()->getAs<FunctionProtoType>();
  if (!FPT)
    return;

  FunctionSafetyKind Explicit = FPT->getSafetySpecifier();

  // Explicitly safe — always allowed.
  if (Explicit == FunctionSafetyKind::Safe)
    return;

  // Explicitly unsafe — always diagnosed.
  if (Explicit == FunctionSafetyKind::Unsafe) {
    bool IsMethod = isa<CXXMethodDecl>(Callee);
    Diag(CallLoc, diag::warn_mizar_unsafe_call_in_safe_context)
        << IsMethod << Callee;
    Diag(Callee->getLocation(), diag::note_mizar_unsafe_function_declared_here)
        << Callee;
    return;
  }

  // Unspecified — run demand-driven inference.
  assert(Explicit == FunctionSafetyKind::Unspecified);
  MizarInferenceResult Result = InferFunctionSafety(Callee);

  if (Result.Safety != InferredSafety::Unsafe)
    return; // Safe, Unknown, or InProgress — allow.

  // Emit the primary diagnostic at the call site.
  Diag(CallLoc, diag::warn_mizar_inferred_unsafe_call) << Callee;

  // Emit a note explaining WHY the function is inferred unsafe.
  switch (Result.Reason) {
  case MizarInferenceResult::PtrDeref:
  case MizarInferenceResult::ReinterpretCast:
  case MizarInferenceResult::InlineAsm: {
    // Map ReasonKind to %select index:
    //   PtrDeref=0, ReinterpretCast=1, InlineAsm=2
    unsigned SelectIdx = static_cast<unsigned>(Result.Reason) -
                         static_cast<unsigned>(MizarInferenceResult::PtrDeref);
    Diag(Result.UnsafeLoc, diag::note_mizar_unsafe_operation)
        << Callee << SelectIdx;
    break;
  }
  case MizarInferenceResult::CallsUnsafe:
    Diag(Result.UnsafeLoc, diag::note_mizar_calls_unsafe)
        << Callee << 0 << Result.UnsafeCallee;
    break;
  case MizarInferenceResult::CallsInferredUnsafe:
    Diag(Result.UnsafeLoc, diag::note_mizar_calls_unsafe)
        << Callee << 1 << Result.UnsafeCallee;
    break;
  case MizarInferenceResult::NoReason:
    break;
  }
}
