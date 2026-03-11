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
#include "llvm/ADT/StringRef.h"

namespace clang {

class ASTContext;
class FunctionDecl;

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
  /// \param PathText       Display text for the borrowed path.
  /// \param ExistingLoc    Where the existing conflicting borrow was created.
  /// \param ExistingIsExclusive  Whether the existing borrow is exclusive.
  virtual void handleExclusiveBorrowConflict(SourceLocation NewBorrowLoc,
                                             llvm::StringRef PathText,
                                             SourceLocation ExistingLoc,
                                             bool ExistingIsExclusive) = 0;

  /// A new shared borrow conflicts with an existing exclusive borrow.
  ///
  /// \param NewBorrowLoc   Where the conflicting shared borrow is created.
  /// \param PathText       Display text for the borrowed path.
  /// \param ExclusiveLoc   Where the existing exclusive borrow was created.
  virtual void handleSharedWhileExclusive(SourceLocation NewBorrowLoc,
                                          llvm::StringRef PathText,
                                          SourceLocation ExclusiveLoc) = 0;

  /// A variable's storage ends while a borrow of it is still live.
  ///
  /// \param DroppedPathText Display text for the borrowed path whose storage ends too soon.
  /// \param BorrowLoc      Where the borrow was created.
  /// \param BorrowIsExclusive  Whether the borrow is exclusive.
  /// \param UseLoc         Where the borrow is used after the drop (if found).
  virtual void handleDoesNotLiveLongEnough(llvm::StringRef DroppedPathText,
                                           SourceLocation BorrowLoc,
                                           bool BorrowIsExclusive,
                                           SourceLocation UseLoc) = 0;

  /// An exclusive tracked reference is used after being definitively moved.
  ///
  /// \param UseLoc   Where the moved-from reference is used.
  /// \param MoveLoc  Where the reference was moved.
  virtual void handleUseAfterMove(SourceLocation UseLoc,
                                  SourceLocation MoveLoc) = 0;

  /// An exclusive tracked reference is used after being possibly moved
  /// (moved on some but not all control flow paths).
  ///
  /// \param UseLoc   Where the maybe-moved reference is used.
  /// \param MoveLoc  Where a move occurred (one of possibly several).
  virtual void handleUseAfterMaybeMove(SourceLocation UseLoc,
                                       SourceLocation MoveLoc) = 0;

  /// A variable is assigned while a borrow (loan) of it is still live.
  ///
  /// \param WriteLoc   Where the write to the borrowed variable occurs.
  /// \param PathText   Display text for the written path.
  /// \param BorrowLoc  Where the conflicting borrow was created.
  virtual void handleWriteWhileBorrowed(SourceLocation WriteLoc,
                                        llvm::StringRef PathText,
                                        SourceLocation BorrowLoc) = 0;
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
