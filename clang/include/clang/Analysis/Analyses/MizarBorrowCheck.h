//===- MizarBorrowCheck.h - Mizar NLL Borrow Checker ------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file defines the entry point for the Mizar NLL (Non-Lexical Lifetimes)
// borrow checker for tracked reference types (T^ and T^ mut).
//
// The analysis detects:
//   - Conflicting borrows (shared + exclusive, exclusive + any)
//   - Dangling references (tracked ref outlives its backing storage)
//
// The analysis is based on three phases:
//   Phase 1: Backward origin liveness (which origins are live at each point)
//   Phase 2: Forward loan propagation with NLL filtering
//   Phase 3: Error detection post-pass
//
//===----------------------------------------------------------------------===//
#ifndef LLVM_CLANG_ANALYSIS_ANALYSES_MIZARBORROWCHECK_H
#define LLVM_CLANG_ANALYSIS_ANALYSES_MIZARBORROWCHECK_H

#include "clang/Basic/SourceLocation.h"

namespace clang {

class ASTContext;
class FunctionDecl;
class NamedDecl;

/// Handler interface for borrow-check diagnostics.
///
/// The Mizar borrow checker calls methods on this interface when violations
/// are detected. The concrete implementation (in Sema) translates these
/// calls into Clang diagnostics.
struct MizarBorrowCheckHandler {
  virtual ~MizarBorrowCheckHandler() = default;

  /// A new exclusive borrow conflicts with an existing borrow on the same path.
  ///
  /// \param NewBorrowLoc   Where the conflicting new borrow is created.
  /// \param Path           The storage location being borrowed (root variable).
  /// \param ExistingLoc    Where the existing conflicting borrow was created.
  /// \param ExistingIsExclusive  Whether the existing borrow is exclusive.
  virtual void handleExclusiveBorrowConflict(SourceLocation NewBorrowLoc,
                                             const NamedDecl *Path,
                                             SourceLocation ExistingLoc,
                                             bool ExistingIsExclusive) = 0;

  /// A new shared borrow conflicts with an existing exclusive borrow.
  ///
  /// \param NewBorrowLoc   Where the conflicting shared borrow is created.
  /// \param Path           The storage location being borrowed.
  /// \param ExclusiveLoc   Where the existing exclusive borrow was created.
  virtual void handleSharedWhileExclusive(SourceLocation NewBorrowLoc,
                                          const NamedDecl *Path,
                                          SourceLocation ExclusiveLoc) = 0;

  /// A variable's storage ends while a borrow of it is still live.
  ///
  /// \param DroppedVar     The variable whose storage ends too soon.
  /// \param BorrowLoc      Where the borrow was created.
  /// \param BorrowIsExclusive  Whether the borrow is exclusive.
  /// \param UseLoc         Where the borrow is used after the drop (if found).
  virtual void handleDoesNotLiveLongEnough(const NamedDecl *DroppedVar,
                                           SourceLocation BorrowLoc,
                                           bool BorrowIsExclusive,
                                           SourceLocation UseLoc) = 0;
};

/// Run the Mizar NLL borrow checker on a function.
///
/// Builds a CFG with lifetime and destructor information, generates facts
/// from the CFG elements, runs backward origin liveness and forward loan
/// propagation with NLL filtering, then scans for violations.
///
/// \param FD      The function to check. Must have a body.
/// \param Ctx     The AST context.
/// \param Handler Receives callbacks for each detected violation.
void runMizarBorrowCheck(const FunctionDecl &FD, ASTContext &Ctx,
                         MizarBorrowCheckHandler &Handler);

} // namespace clang

#endif // LLVM_CLANG_ANALYSIS_ANALYSES_MIZARBORROWCHECK_H
