//===- MizarBorrowCheck.cpp - Mizar NLL Borrow Checker ----------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file implements the Mizar NLL (Non-Lexical Lifetimes) borrow checker
// for tracked reference types (T^ and T^ mut).
//
// Algorithm overview (see specs/borrow_checker.md for full specification):
//
//   Phase 0: Generate facts from the CFG
//     Walk CFG elements and emit Issue, Expire, AssignOrigin, UseOrigin facts.
//
//   Phase 1: Backward origin liveness
//     Compute which origins are live (have a future use) at each program point.
//     Domain: BitVector<OriginID> per block. Join = union of successors.
//
//   Phase 2: Forward loan propagation with NLL filtering
//     Propagate which loans each origin holds. At CFG merge points, dead
//     origins shed their loans (the NLL key insight).
//     Domain: ImmutableMap<OriginID, ImmutableSet<LoanID>> per block.
//
//   Phase 3: Error detection post-pass
//     Replay the analysis per-block, checking for:
//       - Conflicting borrows at each IssueFact
//       - Dangling references at each ExpireFact
//
//===----------------------------------------------------------------------===//

#include "clang/Analysis/Analyses/MizarBorrowCheck.h"
#include "clang/AST/Decl.h"
#include "clang/AST/Expr.h"
#include "clang/AST/ExprCXX.h"
#include "clang/AST/Type.h"
#include "clang/Analysis/CFG.h"
#include "llvm/ADT/BitVector.h"
#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/DenseSet.h"
#include "llvm/ADT/FoldingSet.h"
#include "llvm/ADT/ImmutableMap.h"
#include "llvm/ADT/ImmutableSet.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/raw_ostream.h"
#include <cstdint>

#define DEBUG_TYPE "mizar-borrow-check"

namespace clang {
namespace {

// ========================================================================= //
//                          Core Data Structures
// ========================================================================= //

enum class BorrowKind : uint8_t { Shared, Exclusive };

/// A storage location being borrowed. For v1, just the root variable.
/// Future: field paths (x.field), derefs, indexing.
struct AccessPath {
  const clang::ValueDecl *D;
  AccessPath(const clang::ValueDecl *D) : D(D) {}

  /// Two paths conflict if either is a prefix of the other.
  /// For v1 (no projections), conflict = equality of root.
  bool conflictsWith(const AccessPath &Other) const { return D == Other.D; }
};

/// A type-safe, tagged ID wrapper (same pattern as LifetimeSafety.cpp).
template <typename Tag> struct ID {
  uint32_t Value = 0;

  bool operator==(const ID<Tag> &O) const { return Value == O.Value; }
  bool operator!=(const ID<Tag> &O) const { return !(*this == O); }
  bool operator<(const ID<Tag> &O) const { return Value < O.Value; }
  ID<Tag> operator++(int) {
    ID<Tag> T = *this;
    ++Value;
    return T;
  }
  void Profile(llvm::FoldingSetNodeID &B) const { B.AddInteger(Value); }
};

template <typename Tag>
inline llvm::raw_ostream &operator<<(llvm::raw_ostream &OS, ID<Tag> V) {
  return OS << V.Value;
}

using LoanID = ID<struct LoanTag>;
using OriginID = ID<struct OriginTag>;

/// A loan is created when a tracked reference is born from an address-of.
struct Loan {
  LoanID ID;
  AccessPath Path;
  BorrowKind Kind;
  SourceLocation IssueLoc;

  Loan(LoanID ID, AccessPath Path, BorrowKind Kind, SourceLocation Loc)
      : ID(ID), Path(Path), Kind(Kind), IssueLoc(Loc) {}
};

// ========================================================================= //
//                              Fact Types
// ========================================================================= //

/// Base class for lifetime-relevant facts observed during CFG traversal.
class Fact {
public:
  enum Kind : uint8_t {
    Issue,
    Expire,
    AssignOrigin,
    UseOrigin,
    MoveOrigin,  // Phase B: exclusive ref passed by value (consumed)
    WritePath,   // Phase B: write to a borrowed-from variable
  };

private:
  Kind K;

protected:
  Fact(Kind K) : K(K) {}

public:
  virtual ~Fact() = default;
  Kind getKind() const { return K; }

  template <typename T> const T *getAs() const {
    if (T::classof(this))
      return static_cast<const T *>(this);
    return nullptr;
  }

  virtual void dump(llvm::raw_ostream &OS) const {
    OS << "Fact (Kind: " << static_cast<int>(K) << ")\n";
  }
};

/// A new loan is issued and assigned to an origin.
class IssueFact : public Fact {
  LoanID LID;
  OriginID OID;
  BorrowKind BK;

public:
  static bool classof(const Fact *F) { return F->getKind() == Issue; }

  IssueFact(LoanID L, OriginID O, BorrowKind K)
      : Fact(Issue), LID(L), OID(O), BK(K) {}
  LoanID getLoanID() const { return LID; }
  OriginID getOriginID() const { return OID; }
  BorrowKind getBorrowKind() const { return BK; }

  void dump(llvm::raw_ostream &OS) const override {
    OS << "Issue (Loan: " << LID << ", Origin: " << OID << ", "
       << (BK == BorrowKind::Exclusive ? "Exclusive" : "Shared") << ")\n";
  }
};

/// A loan expires because its backing storage ends (CFGLifetimeEnds).
class ExpireFact : public Fact {
  LoanID LID;

public:
  static bool classof(const Fact *F) { return F->getKind() == Expire; }

  ExpireFact(LoanID L) : Fact(Expire), LID(L) {}
  LoanID getLoanID() const { return LID; }

  void dump(llvm::raw_ostream &OS) const override {
    OS << "Expire (Loan: " << LID << ")\n";
  }
};

/// An origin is propagated from source to destination (p = q).
class AssignOriginFact : public Fact {
  OriginID Dst, Src;

public:
  static bool classof(const Fact *F) {
    return F->getKind() == AssignOrigin;
  }

  AssignOriginFact(OriginID D, OriginID S)
      : Fact(AssignOrigin), Dst(D), Src(S) {}
  OriginID getDestOriginID() const { return Dst; }
  OriginID getSrcOriginID() const { return Src; }

  void dump(llvm::raw_ostream &OS) const override {
    OS << "AssignOrigin (Dest: " << Dst << ", Src: " << Src << ")\n";
  }
};

/// An origin is used (dereferenced or passed somewhere).
class UseOriginFact : public Fact {
  OriginID OID;
  SourceLocation Loc;

public:
  static bool classof(const Fact *F) { return F->getKind() == UseOrigin; }

  UseOriginFact(OriginID O, SourceLocation L)
      : Fact(UseOrigin), OID(O), Loc(L) {}
  OriginID getOriginID() const { return OID; }
  SourceLocation getUseLoc() const { return Loc; }

  void dump(llvm::raw_ostream &OS) const override {
    OS << "UseOrigin (Origin: " << OID << ")\n";
  }
};

/// An exclusive origin is moved (consumed by a function call).
class MoveOriginFact : public Fact {
  OriginID OID;
  SourceLocation Loc;

public:
  static bool classof(const Fact *F) { return F->getKind() == MoveOrigin; }

  MoveOriginFact(OriginID O, SourceLocation L)
      : Fact(MoveOrigin), OID(O), Loc(L) {}
  OriginID getOriginID() const { return OID; }
  SourceLocation getMoveLoc() const { return Loc; }

  void dump(llvm::raw_ostream &OS) const override {
    OS << "MoveOrigin (Origin: " << OID << ")\n";
  }
};

/// A write to a storage location that may have active loans.
class WritePathFact : public Fact {
  const clang::ValueDecl *Target;
  SourceLocation Loc;

public:
  static bool classof(const Fact *F) { return F->getKind() == WritePath; }

  WritePathFact(const clang::ValueDecl *T, SourceLocation L)
      : Fact(WritePath), Target(T), Loc(L) {}
  const clang::ValueDecl *getTarget() const { return Target; }
  SourceLocation getWriteLoc() const { return Loc; }

  void dump(llvm::raw_ostream &OS) const override {
    OS << "WritePath (" << Target->getNameAsString() << ")\n";
  }
};

// ========================================================================= //
//                      Loan and Origin Managers
// ========================================================================= //

class LoanManager {
  LoanID NextID{0};
  llvm::SmallVector<Loan> AllLoans;

public:
  Loan &addLoan(AccessPath Path, BorrowKind Kind, SourceLocation Loc) {
    LoanID ID = NextID++;
    AllLoans.emplace_back(ID, Path, Kind, Loc);
    return AllLoans.back();
  }

  const Loan &getLoan(LoanID ID) const {
    assert(ID.Value < AllLoans.size());
    return AllLoans[ID.Value];
  }

  llvm::ArrayRef<Loan> getLoans() const { return AllLoans; }
  unsigned getNumLoans() const { return NextID.Value; }
};

class OriginManager {
  OriginID NextID{0};
  llvm::DenseMap<const clang::ValueDecl *, OriginID> DeclToOrigin;
  llvm::DenseSet<unsigned> ExclusiveOrigins;

public:
  OriginID getOrCreate(const clang::ValueDecl &D) {
    auto It = DeclToOrigin.find(&D);
    if (It != DeclToOrigin.end())
      return It->second;
    OriginID ID = NextID++;
    DeclToOrigin[&D] = ID;
    return ID;
  }

  /// Mark an origin as coming from an exclusive tracked reference.
  void markExclusive(OriginID OID) { ExclusiveOrigins.insert(OID.Value); }

  /// Check if an origin is from an exclusive tracked reference.
  bool isExclusive(OriginID OID) const {
    return ExclusiveOrigins.count(OID.Value);
  }

  unsigned getNumOrigins() const { return NextID.Value; }
};

// ========================================================================= //
//                          Fact Manager
// ========================================================================= //

class FactManager {
  LoanManager LoanMgr;
  OriginManager OriginMgr;
  llvm::DenseMap<const CFGBlock *, llvm::SmallVector<const Fact *>>
      BlockToFacts;
  llvm::BumpPtrAllocator FactAllocator;

public:
  LoanManager &getLoanMgr() { return LoanMgr; }
  const LoanManager &getLoanMgr() const { return LoanMgr; }
  OriginManager &getOriginMgr() { return OriginMgr; }
  const OriginManager &getOriginMgr() const { return OriginMgr; }

  template <typename T, typename... Args> T *createFact(Args &&...args) {
    void *Mem = FactAllocator.Allocate<T>();
    return new (Mem) T(std::forward<Args>(args)...);
  }

  void addBlockFacts(const CFGBlock *B, llvm::ArrayRef<Fact *> Facts) {
    if (!Facts.empty())
      BlockToFacts[B].assign(Facts.begin(), Facts.end());
  }

  llvm::ArrayRef<const Fact *> getFacts(const CFGBlock *B) const {
    auto It = BlockToFacts.find(B);
    if (It != BlockToFacts.end())
      return It->second;
    return {};
  }

  void dump(const CFG &Cfg) const {
    llvm::dbgs() << "===== Mizar Borrow Check Facts =====\n";
    for (const CFGBlock *B : Cfg) {
      if (!B)
        continue;
      auto Facts = getFacts(B);
      if (Facts.empty())
        continue;
      llvm::dbgs() << "  Block B" << B->getBlockID() << ":\n";
      for (const Fact *F : Facts) {
        llvm::dbgs() << "    ";
        F->dump(llvm::dbgs());
      }
    }
    llvm::dbgs() << "====================================\n";
  }
};

// ========================================================================= //
//                          Helper Functions
// ========================================================================= //

/// Check if a type is a tracked reference (T^ or T^ mut).
static bool hasTrackedRefOrigin(QualType QT) {
  return QT->isTrackedReferenceType();
}

/// Get the BorrowKind for a tracked reference type.
static BorrowKind getTrackedRefBorrowKind(QualType QT) {
  const auto *TRT = QT->getAs<TrackedReferenceType>();
  assert(TRT && "Expected TrackedReferenceType");
  return TRT->isExclusive() ? BorrowKind::Exclusive : BorrowKind::Shared;
}

/// Look through implicit casts and parens to find an address-of expression
/// targeting a local variable. Returns (BorrowedVar, AddrOfExpr) or nulls.
static std::pair<const VarDecl *, const UnaryOperator *>
findAddressOf(const Expr *E) {
  if (!E)
    return {nullptr, nullptr};
  E = E->IgnoreParenImpCasts();
  if (const auto *UO = dyn_cast<UnaryOperator>(E)) {
    if (UO->getOpcode() == UO_AddrOf) {
      const Expr *Sub = UO->getSubExpr()->IgnoreParenImpCasts();
      if (const auto *DRE = dyn_cast<DeclRefExpr>(Sub)) {
        if (const auto *VD = dyn_cast<VarDecl>(DRE->getDecl())) {
          if (VD->hasLocalStorage())
            return {VD, UO};
        }
      }
    }
  }
  return {nullptr, nullptr};
}

/// Find a DeclRefExpr to a local tracked-reference variable through casts.
static const VarDecl *findTrackedRefVar(const Expr *E) {
  if (!E)
    return nullptr;
  E = E->IgnoreParenImpCasts();
  if (const auto *DRE = dyn_cast<DeclRefExpr>(E)) {
    const auto *VD = dyn_cast<VarDecl>(DRE->getDecl());
    if (VD && hasTrackedRefOrigin(VD->getType()))
      return VD;
  }
  return nullptr;
}

// ========================================================================= //
//                Phase 0: Fact Generator
// ========================================================================= //

/// Walks the CFG and generates borrow-check facts for tracked references.
///
/// For each CFGStmt, we:
///   1. Handle specific patterns (DeclStmt, BinaryOperator) for Issue/Assign
///   2. Recursively scan the expression tree for UseOrigin facts (derefs, calls)
///
/// For CFGLifetimeEnds, we emit ExpireFact for each loan on the dying variable.
class MizarFactGenerator {
  FactManager &FM;
  llvm::SmallVector<Fact *> CurrentBlockFacts;

  /// Tracks which origins we've already emitted UseOriginFact for in the
  /// current CFGStmt, to avoid duplicates from recursive scanning.
  llvm::DenseSet<unsigned> UsesEmittedThisStmt;

public:
  explicit MizarFactGenerator(FactManager &FM) : FM(FM) {}

  void run(const CFG &Cfg) {
    for (const CFGBlock *B : Cfg) {
      if (!B)
        continue;
      CurrentBlockFacts.clear();
      for (unsigned I = 0; I < B->size(); ++I) {
        const CFGElement &Elem = (*B)[I];
        if (auto CS = Elem.getAs<CFGStmt>())
          processStmt(CS->getStmt());
        else if (auto LE = Elem.getAs<CFGLifetimeEnds>())
          handleLifetimeEnd(*LE);
        // Future: handle CFGAutomaticObjDtor for destructor write events
      }
      FM.addBlockFacts(B, CurrentBlockFacts);
    }
  }

private:
  /// Process a single CFG statement.
  void processStmt(const Stmt *S) {
    UsesEmittedThisStmt.clear();

    // Handle tracked-ref declaration and assignment patterns.
    if (const auto *DS = dyn_cast<DeclStmt>(S))
      handleDeclStmt(DS);
    else if (const auto *BO = dyn_cast<BinaryOperator>(S)) {
      handleBinaryOp(BO);
      // Detect write-while-borrowed: assignment to a non-tracked-ref variable
      // that might have active loans on it.
      handleWriteToLocal(BO);
    }
    else if (const auto *RS = dyn_cast<ReturnStmt>(S))
      handleReturn(RS);

    // Scan the full expression tree for tracked-ref uses.
    scanForUses(S);
  }

  /// Handle: T^ r = &x;  →  IssueFact
  ///         T^ r = q;   →  AssignOriginFact
  void handleDeclStmt(const DeclStmt *DS) {
    for (const Decl *D : DS->decls()) {
      const auto *VD = dyn_cast<VarDecl>(D);
      if (!VD || !hasTrackedRefOrigin(VD->getType()))
        continue;

      const Expr *Init = VD->getInit();
      if (!Init)
        continue;

      // Case 1: T^ r = &x  →  IssueFact(new_loan, origin(r))
      auto [BorrowedVD, AddrOfExpr] = findAddressOf(Init);
      if (BorrowedVD) {
        BorrowKind Kind = getTrackedRefBorrowKind(VD->getType());
        OriginID OID = FM.getOriginMgr().getOrCreate(*VD);
        if (Kind == BorrowKind::Exclusive)
          FM.getOriginMgr().markExclusive(OID);
        AccessPath Path(BorrowedVD);
        Loan &L = FM.getLoanMgr().addLoan(Path, Kind,
                                          AddrOfExpr->getOperatorLoc());
        CurrentBlockFacts.push_back(
            FM.createFact<IssueFact>(L.ID, OID, Kind));
        continue;
      }

      // Case 2: T^ r = q  →  AssignOriginFact(origin(r), origin(q))
      if (const VarDecl *SrcVD = findTrackedRefVar(Init)) {
        BorrowKind Kind = getTrackedRefBorrowKind(VD->getType());
        OriginID DstOID = FM.getOriginMgr().getOrCreate(*VD);
        OriginID SrcOID = FM.getOriginMgr().getOrCreate(*SrcVD);
        if (Kind == BorrowKind::Exclusive)
          FM.getOriginMgr().markExclusive(DstOID);
        CurrentBlockFacts.push_back(
            FM.createFact<AssignOriginFact>(DstOID, SrcOID));
      }
    }
  }

  /// Handle: r = &x;  →  IssueFact
  ///         r = q;   →  AssignOriginFact
  void handleBinaryOp(const BinaryOperator *BO) {
    if (!BO->isAssignmentOp())
      return;

    // Check if LHS is a tracked-ref variable.
    const VarDecl *LhsVD = findTrackedRefVar(BO->getLHS());
    if (!LhsVD)
      return;

    // Case 1: r = &x  →  IssueFact (new loan replaces old)
    auto [BorrowedVD, AddrOfExpr] = findAddressOf(BO->getRHS());
    if (BorrowedVD) {
      BorrowKind Kind = getTrackedRefBorrowKind(LhsVD->getType());
      OriginID OID = FM.getOriginMgr().getOrCreate(*LhsVD);
      if (Kind == BorrowKind::Exclusive)
        FM.getOriginMgr().markExclusive(OID);
      AccessPath Path(BorrowedVD);
      Loan &L = FM.getLoanMgr().addLoan(Path, Kind,
                                        AddrOfExpr->getOperatorLoc());
      CurrentBlockFacts.push_back(
          FM.createFact<IssueFact>(L.ID, OID, Kind));
      return;
    }

    // Case 2: r = q  →  AssignOriginFact
    if (const VarDecl *SrcVD = findTrackedRefVar(BO->getRHS())) {
      BorrowKind Kind = getTrackedRefBorrowKind(LhsVD->getType());
      OriginID DstOID = FM.getOriginMgr().getOrCreate(*LhsVD);
      OriginID SrcOID = FM.getOriginMgr().getOrCreate(*SrcVD);
      if (Kind == BorrowKind::Exclusive)
        FM.getOriginMgr().markExclusive(DstOID);
      CurrentBlockFacts.push_back(
          FM.createFact<AssignOriginFact>(DstOID, SrcOID));
    }
  }

  /// Handle: return r;  →  UseOriginFact
  void handleReturn(const ReturnStmt *RS) {
    if (const Expr *RV = RS->getRetValue()) {
      if (const VarDecl *VD = findTrackedRefVar(RV)) {
        OriginID OID = FM.getOriginMgr().getOrCreate(*VD);
        emitUse(OID, RS->getReturnLoc());
      }
    }
  }

  /// Detect writes to local variables that may have loans.
  /// e.g. x = 42 when there's an outstanding borrow r = &x.
  void handleWriteToLocal(const BinaryOperator *BO) {
    if (!BO->isAssignmentOp())
      return;
    // Skip if LHS is a tracked-ref variable (that's an origin update, not
    // a write to a borrowed-from path).
    const Expr *LHS = BO->getLHS()->IgnoreParenImpCasts();
    // Check for direct assignment: x = ...
    if (const auto *DRE = dyn_cast<DeclRefExpr>(LHS)) {
      const auto *VD = dyn_cast<VarDecl>(DRE->getDecl());
      if (VD && VD->hasLocalStorage() && !hasTrackedRefOrigin(VD->getType())) {
        CurrentBlockFacts.push_back(
            FM.createFact<WritePathFact>(VD, BO->getOperatorLoc()));
      }
    }
    // Check for dereference assignment: *r = ... where r is a tracked-ref
    // pointing at a borrowed variable. This is handled via UseOriginFact
    // (the dereference is a use of the origin, which is caught by scanForUses).
  }

  /// Recursively scan an expression tree for tracked-ref uses.
  ///
  /// Detects:
  ///   - Dereference: *r  →  UseOriginFact(origin(r))
  ///   - Call argument: f(r)  →  UseOriginFact(origin(r))
  void scanForUses(const Stmt *S) {
    if (!S)
      return;

    // Dereference of tracked ref: *r
    if (const auto *UO = dyn_cast<UnaryOperator>(S)) {
      if (UO->getOpcode() == UO_Deref) {
        if (const VarDecl *VD = findTrackedRefVar(UO->getSubExpr())) {
          OriginID OID = FM.getOriginMgr().getOrCreate(*VD);
          emitUse(OID, UO->getOperatorLoc());
        }
      }
    }

    // Function call with tracked-ref argument: f(r)
    // Passing a tracked reference (shared or exclusive) to a function creates
    // an implicit reborrow (§9.7). The callee gets a fresh borrow derived
    // from the caller's reference; the caller's reference remains valid
    // after the call returns. This is a use, not a move.
    if (const auto *CE = dyn_cast<CallExpr>(S)) {
      for (const Expr *Arg : CE->arguments()) {
        if (const VarDecl *VD = findTrackedRefVar(Arg)) {
          OriginID OID = FM.getOriginMgr().getOrCreate(*VD);
          emitUse(OID, CE->getBeginLoc());
        }
      }
    }

    // Constructor call with tracked-ref argument
    if (const auto *CE = dyn_cast<CXXConstructExpr>(S)) {
      for (unsigned I = 0; I < CE->getNumArgs(); ++I) {
        if (const VarDecl *VD = findTrackedRefVar(CE->getArg(I))) {
          OriginID OID = FM.getOriginMgr().getOrCreate(*VD);
          emitUse(OID, CE->getBeginLoc());
        }
      }
    }

    // MemberExpr through tracked-ref: r->member
    if (const auto *ME = dyn_cast<MemberExpr>(S)) {
      if (ME->isArrow()) {
        if (const VarDecl *VD = findTrackedRefVar(ME->getBase())) {
          OriginID OID = FM.getOriginMgr().getOrCreate(*VD);
          emitUse(OID, ME->getOperatorLoc());
        }
      }
    }

    // Recurse into children.
    for (const Stmt *Child : S->children())
      scanForUses(Child);
  }

  /// Emit a UseOriginFact, deduplicating within the current CFGStmt.
  void emitUse(OriginID OID, SourceLocation Loc) {
    if (UsesEmittedThisStmt.insert(OID.Value).second)
      CurrentBlockFacts.push_back(FM.createFact<UseOriginFact>(OID, Loc));
  }

  /// Emit a MoveOriginFact, deduplicating within the current CFGStmt.
  void emitMove(OriginID OID, SourceLocation Loc) {
    if (UsesEmittedThisStmt.insert(OID.Value).second)
      CurrentBlockFacts.push_back(FM.createFact<MoveOriginFact>(OID, Loc));
  }

  /// Handle CFGLifetimeEnds: emit ExpireFact for each loan on the dying var.
  void handleLifetimeEnd(const CFGLifetimeEnds &LE) {
    const VarDecl *VD = LE.getVarDecl();
    if (!VD)
      return;
    for (const Loan &L : FM.getLoanMgr().getLoans()) {
      if (L.Path.D == VD)
        CurrentBlockFacts.push_back(FM.createFact<ExpireFact>(L.ID));
    }
  }
};

// ========================================================================= //
//                Phase 1: Backward Origin Liveness
// ========================================================================= //

/// Computes which origins are live (have a future use without intervening
/// redefinition) at each program point.
///
/// This is the NLL key: a loan is only relevant while some origin containing
/// it is live. Dead origins shed their loans at CFG merge points.
class BackwardLiveness {
  const CFG &Cfg;
  const FactManager &FM;
  unsigned NumOrigins;

  llvm::DenseMap<const CFGBlock *, llvm::BitVector> LiveAtEntry;
  llvm::DenseMap<const CFGBlock *, llvm::BitVector> LiveAtExit;

public:
  BackwardLiveness(const CFG &C, const FactManager &F, unsigned NumOrigins)
      : Cfg(C), FM(F), NumOrigins(NumOrigins) {}

  void run() {
    if (NumOrigins == 0)
      return;

    // Initialize all blocks with empty bit vectors.
    for (const CFGBlock *B : Cfg) {
      if (!B)
        continue;
      LiveAtEntry[B] = llvm::BitVector(NumOrigins, false);
      LiveAtExit[B] = llvm::BitVector(NumOrigins, false);
    }

    // Backward worklist — seed with all blocks so that every block is
    // processed at least once.  (Seeding only the exit block is incorrect:
    // the exit block has no facts and no successors, so its LiveAtEntry
    // never changes from the initial empty set, preventing predecessor
    // propagation.)
    llvm::SmallVector<const CFGBlock *, 16> Worklist;
    llvm::DenseSet<const CFGBlock *> InWorklist;

    for (const CFGBlock *B : Cfg) {
      if (!B)
        continue;
      Worklist.push_back(B);
      InWorklist.insert(B);
    }

    while (!Worklist.empty()) {
      const CFGBlock *B = Worklist.pop_back_val();
      InWorklist.erase(B);

      // LiveAtExit(B) = union of LiveAtEntry(Succ) for each successor.
      llvm::BitVector NewExit(NumOrigins, false);
      for (const CFGBlock *Succ : B->succs()) {
        if (Succ) {
          auto It = LiveAtEntry.find(Succ);
          if (It != LiveAtEntry.end())
            NewExit |= It->second;
        }
      }
      LiveAtExit[B] = NewExit;

      // Transfer backward through the block.
      llvm::BitVector NewEntry = transferBlock(B, NewExit);

      if (NewEntry != LiveAtEntry[B]) {
        LiveAtEntry[B] = NewEntry;
        // Add predecessors to worklist.
        for (const CFGBlock *Pred : B->preds()) {
          if (Pred && !InWorklist.count(Pred)) {
            Worklist.push_back(Pred);
            InWorklist.insert(Pred);
          }
        }
      }
    }
  }

  const llvm::BitVector &getEntry(const CFGBlock *B) const {
    static const llvm::BitVector Empty;
    auto It = LiveAtEntry.find(B);
    return It != LiveAtEntry.end() ? It->second : Empty;
  }

  const llvm::BitVector &getExit(const CFGBlock *B) const {
    static const llvm::BitVector Empty;
    auto It = LiveAtExit.find(B);
    return It != LiveAtExit.end() ? It->second : Empty;
  }

  /// Compute per-element liveness within a block.
  /// Returns a vector where [i] = live origins BEFORE fact i.
  llvm::SmallVector<llvm::BitVector>
  getPerElementLiveness(const CFGBlock *B) const {
    llvm::ArrayRef<const Fact *> Facts = FM.getFacts(B);
    llvm::SmallVector<llvm::BitVector> Result(
        Facts.size(), llvm::BitVector(NumOrigins, false));

    // Start from LiveAtExit and process backward.
    llvm::BitVector Live = getExit(B);
    for (int I = static_cast<int>(Facts.size()) - 1; I >= 0; --I) {
      applyFactBackward(Live, Facts[I]);
      Result[I] = Live;
    }
    return Result;
  }

private:
  llvm::BitVector transferBlock(const CFGBlock *B,
                                llvm::BitVector Live) const {
    llvm::ArrayRef<const Fact *> Facts = FM.getFacts(B);
    for (int I = static_cast<int>(Facts.size()) - 1; I >= 0; --I)
      applyFactBackward(Live, Facts[I]);
    return Live;
  }

  void applyFactBackward(llvm::BitVector &Live, const Fact *F) const {
    switch (F->getKind()) {
    case Fact::UseOrigin: {
      auto *U = F->getAs<UseOriginFact>();
      unsigned Idx = U->getOriginID().Value;
      if (Idx < NumOrigins)
        Live.set(Idx); // gen: origin is used here.
      break;
    }
    case Fact::MoveOrigin: {
      // A move is a use (gen) then a kill — but for liveness purposes,
      // a move makes the origin live before it (we need to know the origin
      // was consumed). After the move, the origin is dead (killed).
      auto *M = F->getAs<MoveOriginFact>();
      unsigned Idx = M->getOriginID().Value;
      if (Idx < NumOrigins) {
        Live.reset(Idx); // kill: origin is consumed by the move.
        Live.set(Idx);   // gen: origin must be live to be moved.
        // Net effect: Live.set(Idx) — move requires origin to be live.
      }
      break;
    }
    case Fact::AssignOrigin: {
      auto *A = F->getAs<AssignOriginFact>();
      unsigned DstIdx = A->getDestOriginID().Value;
      unsigned SrcIdx = A->getSrcOriginID().Value;
      if (DstIdx < NumOrigins)
        Live.reset(DstIdx); // kill: destination is overwritten.
      if (SrcIdx < NumOrigins)
        Live.set(SrcIdx); // gen: source is read.
      break;
    }
    case Fact::Issue: {
      auto *I = F->getAs<IssueFact>();
      unsigned Idx = I->getOriginID().Value;
      if (Idx < NumOrigins)
        Live.reset(Idx); // kill: origin is redefined with a new loan.
      break;
    }
    case Fact::Expire:
    case Fact::WritePath:
      // No effect on origin liveness.
      break;
    }
  }
};

// ========================================================================= //
//                Phase 2: Forward Loan Propagation (with NLL)
// ========================================================================= //

using LoanSet = llvm::ImmutableSet<LoanID>;
using OriginLoanMap = llvm::ImmutableMap<OriginID, LoanSet>;

/// Factories for immutable collections, shared across all lattice instances.
struct LoanFactory {
  OriginLoanMap::Factory OriginMapFact;
  LoanSet::Factory LoanSetFact;

  LoanSet createSingleton(LoanID L) {
    return LoanSetFact.add(LoanSetFact.getEmptySet(), L);
  }

  LoanSet unionSets(LoanSet A, LoanSet B) {
    if (A.getHeight() < B.getHeight())
      std::swap(A, B);
    LoanSet Result = A;
    for (LoanID L : B)
      Result = LoanSetFact.add(Result, L);
    return Result;
  }
};

/// The dataflow lattice: maps each origin to its set of loans.
struct LoanLattice {
  OriginLoanMap Origins = OriginLoanMap(nullptr);

  LoanLattice() = default;
  explicit LoanLattice(OriginLoanMap M) : Origins(M) {}

  bool operator==(const LoanLattice &O) const { return Origins == O.Origins; }
  bool operator!=(const LoanLattice &O) const { return !(*this == O); }

  LoanSet getLoans(OriginID OID) const {
    if (auto *S = Origins.lookup(OID))
      return *S;
    return LoanSet(nullptr);
  }

  /// NLL-filtered join: merge predecessor states, dropping dead origins.
  ///
  /// For each origin that appears in any predecessor state:
  ///   if the origin is live at the successor's entry → union loan sets
  ///   else → drop from result (the NLL key insight)
  LoanLattice join(const LoanLattice &Other, LoanFactory &F,
                   const llvm::BitVector &LiveAtEntry) const {
    // First, compute the raw union of both maps.
    OriginLoanMap Merged = Origins;
    for (const auto &Entry : Other.Origins) {
      OriginID OID = Entry.first;
      LoanSet OtherLoans = Entry.second;
      LoanSet MyLoans = getLoans(OID);
      Merged = F.OriginMapFact.add(Merged, OID,
                                   F.unionSets(MyLoans, OtherLoans));
    }

    // Then filter: keep only live origins.
    OriginLoanMap Filtered = F.OriginMapFact.getEmptyMap();
    for (const auto &Entry : Merged) {
      unsigned Idx = Entry.first.Value;
      if (Idx < LiveAtEntry.size() && LiveAtEntry[Idx])
        Filtered = F.OriginMapFact.add(Filtered, Entry.first, Entry.second);
    }
    return LoanLattice(Filtered);
  }

  void dump(llvm::raw_ostream &OS) const {
    for (const auto &Entry : Origins) {
      OS << "  Origin " << Entry.first << " → {";
      bool First = true;
      for (LoanID L : Entry.second) {
        if (!First)
          OS << ", ";
        OS << L;
        First = false;
      }
      OS << "}\n";
    }
  }
};

/// Forward loan propagation with NLL filtering at join points.
class ForwardLoanPropagation {
  const CFG &Cfg;
  const FactManager &FM;
  LoanFactory Factory;
  const BackwardLiveness &Liveness;

  llvm::DenseMap<const CFGBlock *, LoanLattice> BlockEntryStates;
  llvm::DenseMap<const CFGBlock *, LoanLattice> BlockExitStates;

public:
  ForwardLoanPropagation(const CFG &C, const FactManager &F,
                         const BackwardLiveness &L)
      : Cfg(C), FM(F), Liveness(L) {}

  void run() {
    llvm::SmallVector<const CFGBlock *, 16> Worklist;
    llvm::DenseSet<const CFGBlock *> InWorklist;

    const CFGBlock &Entry = Cfg.getEntry();
    Worklist.push_back(&Entry);
    InWorklist.insert(&Entry);

    while (!Worklist.empty()) {
      const CFGBlock *B = Worklist.pop_back_val();
      InWorklist.erase(B);

      LoanLattice EntryState = getEntryState(B);
      LoanLattice ExitState = transferBlock(B, EntryState);
      BlockExitStates[B] = ExitState;

      for (const CFGBlock *Succ : B->succs()) {
        if (!Succ)
          continue;

        LoanLattice OldEntry = getEntryState(Succ);
        const llvm::BitVector &SuccLive = Liveness.getEntry(Succ);
        LoanLattice NewEntry = OldEntry.join(ExitState, Factory, SuccLive);

        if (NewEntry != OldEntry) {
          BlockEntryStates[Succ] = NewEntry;
          if (!InWorklist.count(Succ)) {
            Worklist.push_back(Succ);
            InWorklist.insert(Succ);
          }
        }
      }
    }
  }

  LoanLattice getEntryState(const CFGBlock *B) const {
    auto It = BlockEntryStates.find(B);
    return It != BlockEntryStates.end() ? It->second : LoanLattice{};
  }

  LoanFactory &getFactory() { return Factory; }

private:
  LoanLattice transferBlock(const CFGBlock *B, LoanLattice State) {
    for (const Fact *F : FM.getFacts(B))
      State = transferFact(State, F);
    return State;
  }

  LoanLattice transferFact(LoanLattice In, const Fact *F) {
    switch (F->getKind()) {
    case Fact::Issue: {
      auto *I = F->getAs<IssueFact>();
      // Strong update: new loan replaces any old loans in this origin.
      return LoanLattice(Factory.OriginMapFact.add(
          In.Origins, I->getOriginID(),
          Factory.createSingleton(I->getLoanID())));
    }
    case Fact::AssignOrigin: {
      auto *A = F->getAs<AssignOriginFact>();
      // Propagate source's loans to destination.
      LoanSet SrcLoans = In.getLoans(A->getSrcOriginID());
      return LoanLattice(Factory.OriginMapFact.add(
          In.Origins, A->getDestOriginID(), SrcLoans));
    }
    case Fact::MoveOrigin: {
      // A move kills the origin's loans (the ref is consumed).
      auto *M = F->getAs<MoveOriginFact>();
      return LoanLattice(Factory.OriginMapFact.remove(
          In.Origins, M->getOriginID()));
    }
    case Fact::Expire:
    case Fact::UseOrigin:
    case Fact::WritePath:
      // No effect on the loan lattice.
      return In;
    }
    llvm_unreachable("Unknown fact kind");
  }
};

// ========================================================================= //
//                Phase 2b: Forward Move State Tracking
// ========================================================================= //

/// Three-valued move state for each exclusive origin.
enum class MoveState : uint8_t {
  Valid,      // Initialized and not moved.
  Moved,      // Definitively moved on all reaching paths.
  MaybeMoved, // Moved on some paths but not all.
};

/// Join two move states at a CFG merge point.
static MoveState joinMoveState(MoveState A, MoveState B) {
  if (A == B)
    return A;
  return MoveState::MaybeMoved;
}

/// Per-origin move state and the location of the (first) move.
struct OriginMoveInfo {
  MoveState State = MoveState::Moved; // Default to Moved (uninitialized).
  SourceLocation MoveLoc;             // Where the move happened.

  bool operator==(const OriginMoveInfo &O) const {
    return State == O.State && MoveLoc == O.MoveLoc;
  }
  bool operator!=(const OriginMoveInfo &O) const { return !(*this == O); }
};

/// Forward move-state analysis per CFGBlock.
///
/// Tracks which exclusive origins have been moved, handling CFG merge
/// points with the three-valued lattice join.
class ForwardMoveTracking {
  const CFG &Cfg;
  const FactManager &FM;

  using MoveMap = llvm::DenseMap<unsigned, OriginMoveInfo>;

  llvm::DenseMap<const CFGBlock *, MoveMap> BlockEntryStates;
  llvm::DenseMap<const CFGBlock *, MoveMap> BlockExitStates;

  /// Tracks which blocks have had their entry state set by at least one
  /// predecessor. Distinguishes "not yet reached" from "reached with empty
  /// state".
  llvm::DenseSet<const CFGBlock *> Reached;

public:
  ForwardMoveTracking(const CFG &C, const FactManager &F)
      : Cfg(C), FM(F) {}

  void run() {
    llvm::SmallVector<const CFGBlock *, 16> Worklist;
    llvm::DenseSet<const CFGBlock *> InWorklist;

    // Seed from the entry block.
    const CFGBlock &Entry = Cfg.getEntry();
    Worklist.push_back(&Entry);
    InWorklist.insert(&Entry);
    Reached.insert(&Entry);

    while (!Worklist.empty()) {
      const CFGBlock *B = Worklist.pop_back_val();
      InWorklist.erase(B);

      MoveMap EntryState = getEntryState(B);
      MoveMap ExitState = transferBlock(B, EntryState);
      BlockExitStates[B] = ExitState;

      for (const CFGBlock *Succ : B->succs()) {
        if (!Succ)
          continue;

        bool FirstReach = Reached.insert(Succ).second;
        if (FirstReach) {
          // First predecessor to reach this block — set state directly.
          BlockEntryStates[Succ] = ExitState;
          if (!InWorklist.count(Succ)) {
            Worklist.push_back(Succ);
            InWorklist.insert(Succ);
          }
        } else {
          // Already reached — join with existing entry state.
          MoveMap OldEntry = getEntryState(Succ);
          MoveMap NewEntry = joinStates(OldEntry, ExitState);
          if (NewEntry != OldEntry) {
            BlockEntryStates[Succ] = NewEntry;
            if (!InWorklist.count(Succ)) {
              Worklist.push_back(Succ);
              InWorklist.insert(Succ);
            }
          }
        }
      }
    }
  }

  MoveMap getEntryState(const CFGBlock *B) const {
    auto It = BlockEntryStates.find(B);
    return It != BlockEntryStates.end() ? It->second : MoveMap{};
  }

  /// Get the move info for an origin at a block's entry.
  OriginMoveInfo getOriginState(const CFGBlock *B, OriginID OID) const {
    auto It = BlockEntryStates.find(B);
    if (It != BlockEntryStates.end()) {
      auto OIt = It->second.find(OID.Value);
      if (OIt != It->second.end())
        return OIt->second;
    }
    // Not in the map → uninitialized → Moved.
    return OriginMoveInfo{MoveState::Moved, SourceLocation()};
  }

private:
  MoveMap transferBlock(const CFGBlock *B, MoveMap State) {
    for (const Fact *F : FM.getFacts(B))
      applyFact(State, F);
    return State;
  }

  void applyFact(MoveMap &State, const Fact *F) {
    switch (F->getKind()) {
    case Fact::Issue: {
      // IssueFact: origin is (re)initialized with a new loan → Valid.
      auto *I = F->getAs<IssueFact>();
      unsigned Idx = I->getOriginID().Value;
      if (FM.getOriginMgr().isExclusive(I->getOriginID()))
        State[Idx] = OriginMoveInfo{MoveState::Valid, SourceLocation()};
      break;
    }
    case Fact::AssignOrigin: {
      // AssignOriginFact: destination gets source's state.
      // But for the destination, it's a re-initialization → Valid.
      auto *A = F->getAs<AssignOriginFact>();
      unsigned DstIdx = A->getDestOriginID().Value;
      if (FM.getOriginMgr().isExclusive(A->getDestOriginID()))
        State[DstIdx] = OriginMoveInfo{MoveState::Valid, SourceLocation()};
      break;
    }
    case Fact::MoveOrigin: {
      auto *M = F->getAs<MoveOriginFact>();
      unsigned Idx = M->getOriginID().Value;
      State[Idx] = OriginMoveInfo{MoveState::Moved, M->getMoveLoc()};
      break;
    }
    default:
      break;
    }
  }

  MoveMap joinStates(const MoveMap &A, const MoveMap &B) {
    MoveMap Result = A;
    for (const auto &Entry : B) {
      auto It = Result.find(Entry.first);
      if (It == Result.end()) {
        // Origin only in B → A path didn't track it (= Moved/uninitialized).
        MoveState Joined = joinMoveState(MoveState::Moved, Entry.second.State);
        Result[Entry.first] = OriginMoveInfo{Joined, Entry.second.MoveLoc};
      } else {
        // Origin in both → join states.
        MoveState Joined = joinMoveState(It->second.State, Entry.second.State);
        SourceLocation Loc = It->second.MoveLoc.isValid()
                                 ? It->second.MoveLoc
                                 : Entry.second.MoveLoc;
        It->second = OriginMoveInfo{Joined, Loc};
      }
    }
    // Origins only in A are already in Result. But we need to consider
    // that any origin in A but not in B is Moved on B's path (uninitialized).
    for (auto &Entry : Result) {
      if (B.count(Entry.first) == 0) {
        // Origin only in A → join with Moved (B path didn't initialize).
        MoveState Joined = joinMoveState(Entry.second.State, MoveState::Moved);
        Entry.second.State = Joined;
      }
    }
    return Result;
  }
};

// ========================================================================= //
//                Phase 3: Error Detection
// ========================================================================= //

/// Scans the CFG for borrow-check violations using the results of
/// backward liveness and forward loan propagation.
class ErrorDetector {
  const CFG &Cfg;
  const FactManager &FM;
  const BackwardLiveness &Liveness;
  ForwardLoanPropagation &Loans;
  const ForwardMoveTracking &MoveTracking;
  MizarBorrowCheckHandler &Handler;

  using MoveMap = llvm::DenseMap<unsigned, OriginMoveInfo>;

public:
  ErrorDetector(const CFG &C, const FactManager &F,
                const BackwardLiveness &L, ForwardLoanPropagation &P,
                const ForwardMoveTracking &MT,
                MizarBorrowCheckHandler &H)
      : Cfg(C), FM(F), Liveness(L), Loans(P), MoveTracking(MT), Handler(H) {}

  void run() {
    for (const CFGBlock *B : Cfg) {
      if (!B)
        continue;
      checkBlock(B);
    }
  }

private:
  void checkBlock(const CFGBlock *B) {
    llvm::ArrayRef<const Fact *> Facts = FM.getFacts(B);
    if (Facts.empty())
      return;

    // Compute per-element liveness: LiveBefore[i] = live origins BEFORE fact i.
    llvm::SmallVector<llvm::BitVector> LiveBefore =
        Liveness.getPerElementLiveness(B);

    // Replay forward loan state through the block.
    LoanLattice State = Loans.getEntryState(B);

    // Replay forward move state through the block.
    MoveMap MState = MoveTracking.getEntryState(B);

    for (unsigned I = 0; I < Facts.size(); ++I) {
      const Fact *F = Facts[I];

      switch (F->getKind()) {
      case Fact::Issue:
        checkIssueFact(F->getAs<IssueFact>(), State, LiveBefore[I]);
        break;
      case Fact::Expire:
        checkExpireFact(F->getAs<ExpireFact>(), State, LiveBefore[I], B, I);
        break;
      case Fact::UseOrigin:
        checkUseOriginFact(F->getAs<UseOriginFact>(), MState);
        break;
      case Fact::WritePath:
        checkWritePathFact(F->getAs<WritePathFact>(), State, LiveBefore[I]);
        break;
      default:
        break;
      }

      // Advance the loan state through this fact.
      State = applyTransfer(State, F);
      // Advance the move state through this fact.
      applyMoveTransfer(MState, F);
    }
  }

  /// At an IssueFact: check for conflicting live loans on the same path.
  void checkIssueFact(const IssueFact *Issue, const LoanLattice &State,
                      const llvm::BitVector &Live) {
    LoanID NewLID = Issue->getLoanID();
    const Loan &NewLoan = FM.getLoanMgr().getLoan(NewLID);

    for (const auto &Entry : State.Origins) {
      OriginID OID = Entry.first;
      // Only check live origins.
      if (OID.Value >= Live.size() || !Live[OID.Value])
        continue;

      for (LoanID ExistingLID : Entry.second) {
        const Loan &Existing = FM.getLoanMgr().getLoan(ExistingLID);
        if (!Existing.Path.conflictsWith(NewLoan.Path))
          continue;

        // Conflict detected!
        if (NewLoan.Kind == BorrowKind::Exclusive) {
          Handler.handleExclusiveBorrowConflict(
              NewLoan.IssueLoc, NewLoan.Path.D, Existing.IssueLoc,
              Existing.Kind == BorrowKind::Exclusive);
          return; // One error per issue fact.
        }

        if (NewLoan.Kind == BorrowKind::Shared &&
            Existing.Kind == BorrowKind::Exclusive) {
          Handler.handleSharedWhileExclusive(NewLoan.IssueLoc, NewLoan.Path.D,
                                             Existing.IssueLoc);
          return;
        }
        // Shared + Shared: no conflict.
      }
    }
  }

  /// At an ExpireFact: check if any live origin holds the expiring loan.
  void checkExpireFact(const ExpireFact *Expire, const LoanLattice &State,
                       const llvm::BitVector &Live, const CFGBlock *B,
                       unsigned FactIdx) {
    LoanID ExpiredLID = Expire->getLoanID();
    const Loan &ExpiredLoan = FM.getLoanMgr().getLoan(ExpiredLID);

    for (const auto &Entry : State.Origins) {
      OriginID OID = Entry.first;
      if (OID.Value >= Live.size() || !Live[OID.Value])
        continue;

      for (LoanID LID : Entry.second) {
        if (LID != ExpiredLID)
          continue;

        // A live origin holds the expiring loan → dangling reference!
        SourceLocation UseLoc = findNextUse(OID, B, FactIdx + 1);

        Handler.handleDoesNotLiveLongEnough(
            ExpiredLoan.Path.D, ExpiredLoan.IssueLoc,
            ExpiredLoan.Kind == BorrowKind::Exclusive, UseLoc);
        return;
      }
    }
  }

  /// Scan forward from a given position to find the next use of an origin.
  /// This provides the "borrow used here" third diagnostic point.
  SourceLocation findNextUse(OriginID OID, const CFGBlock *B,
                             unsigned StartIdx) {
    // Search forward in the current block.
    llvm::ArrayRef<const Fact *> Facts = FM.getFacts(B);
    for (unsigned I = StartIdx; I < Facts.size(); ++I) {
      if (auto *U = Facts[I]->getAs<UseOriginFact>()) {
        if (U->getOriginID() == OID)
          return U->getUseLoc();
      }
    }

    // Search successor blocks (simple BFS, bounded depth).
    llvm::SmallVector<const CFGBlock *, 8> BFS;
    llvm::DenseSet<const CFGBlock *> Visited;
    for (const CFGBlock *Succ : B->succs()) {
      if (Succ) {
        BFS.push_back(Succ);
        Visited.insert(Succ);
      }
    }
    for (unsigned Depth = 0; Depth < BFS.size() && Depth < 32; ++Depth) {
      const CFGBlock *Cur = BFS[Depth];
      for (const Fact *F : FM.getFacts(Cur)) {
        if (auto *U = F->getAs<UseOriginFact>()) {
          if (U->getOriginID() == OID)
            return U->getUseLoc();
        }
      }
      for (const CFGBlock *Succ : Cur->succs()) {
        if (Succ && Visited.insert(Succ).second)
          BFS.push_back(Succ);
      }
    }

    return SourceLocation(); // Not found.
  }

  /// Apply the forward transfer function for a single fact.
  LoanLattice applyTransfer(LoanLattice In, const Fact *F) {
    switch (F->getKind()) {
    case Fact::Issue: {
      auto *I = F->getAs<IssueFact>();
      return LoanLattice(Loans.getFactory().OriginMapFact.add(
          In.Origins, I->getOriginID(),
          Loans.getFactory().createSingleton(I->getLoanID())));
    }
    case Fact::AssignOrigin: {
      auto *A = F->getAs<AssignOriginFact>();
      LoanSet SrcLoans = In.getLoans(A->getSrcOriginID());
      return LoanLattice(Loans.getFactory().OriginMapFact.add(
          In.Origins, A->getDestOriginID(), SrcLoans));
    }
    case Fact::Expire:
    case Fact::UseOrigin:
    case Fact::MoveOrigin:
    case Fact::WritePath:
      return In;
    }
    llvm_unreachable("Unknown fact kind");
  }

  /// Check a UseOriginFact against the current move state.
  void checkUseOriginFact(const UseOriginFact *Use, const MoveMap &MState) {
    OriginID OID = Use->getOriginID();
    if (!FM.getOriginMgr().isExclusive(OID))
      return;

    auto It = MState.find(OID.Value);
    if (It == MState.end())
      return; // Not tracked yet → Valid (freshly declared inline).

    const OriginMoveInfo &Info = It->second;
    switch (Info.State) {
    case MoveState::Valid:
      break;
    case MoveState::Moved:
      Handler.handleUseAfterMove(Use->getUseLoc(), Info.MoveLoc);
      break;
    case MoveState::MaybeMoved:
      Handler.handleUseAfterMaybeMove(Use->getUseLoc(), Info.MoveLoc);
      break;
    }
  }

  /// Check a WritePathFact: writing to a local while an origin borrows it.
  void checkWritePathFact(const WritePathFact *WF, const LoanLattice &State,
                          const llvm::BitVector &Live) {
    const ValueDecl *WrittenVar = WF->getTarget();

    for (const auto &Entry : State.Origins) {
      OriginID OID = Entry.first;
      if (OID.Value >= Live.size() || !Live[OID.Value])
        continue;

      for (LoanID LID : Entry.second) {
        const Loan &L = FM.getLoanMgr().getLoan(LID);
        if (L.Path.D == WrittenVar) {
          Handler.handleWriteWhileBorrowed(WF->getWriteLoc(), WrittenVar,
                                           L.IssueLoc);
          return;
        }
      }
    }
  }

  /// Apply the forward move-state transfer function for a single fact.
  void applyMoveTransfer(MoveMap &MState, const Fact *F) {
    switch (F->getKind()) {
    case Fact::Issue: {
      auto *I = F->getAs<IssueFact>();
      if (FM.getOriginMgr().isExclusive(I->getOriginID()))
        MState[I->getOriginID().Value] =
            OriginMoveInfo{MoveState::Valid, SourceLocation()};
      break;
    }
    case Fact::AssignOrigin: {
      auto *A = F->getAs<AssignOriginFact>();
      if (FM.getOriginMgr().isExclusive(A->getDestOriginID()))
        MState[A->getDestOriginID().Value] =
            OriginMoveInfo{MoveState::Valid, SourceLocation()};
      break;
    }
    case Fact::MoveOrigin: {
      auto *M = F->getAs<MoveOriginFact>();
      MState[M->getOriginID().Value] =
          OriginMoveInfo{MoveState::Moved, M->getMoveLoc()};
      break;
    }
    default:
      break;
    }
  }
};

} // anonymous namespace

// ========================================================================= //
//                          Entry Point
// ========================================================================= //

void runMizarBorrowCheck(const FunctionDecl &FD, ASTContext &Ctx,
                         MizarBorrowCheckHandler &Handler) {
  if (!FD.hasBody())
    return;

  // Build the CFG with lifetime and destructor information.
  CFG::BuildOptions BO;
  BO.AddImplicitDtors = true;
  BO.AddLifetime = true;
  BO.AddScopes = true;
  BO.AddTemporaryDtors = true;
  BO.PruneTriviallyFalseEdges = true;

  std::unique_ptr<CFG> Cfg =
      CFG::buildCFG(&FD, FD.getBody(), &Ctx, BO);
  if (!Cfg)
    return;

  LLVM_DEBUG(Cfg->dump(Ctx.getLangOpts(), /*ShowColors=*/true));

  // Phase 0: Generate facts.
  FactManager FM;
  MizarFactGenerator FactGen(FM);
  FactGen.run(*Cfg);

  unsigned NumOrigins = FM.getOriginMgr().getNumOrigins();
  if (NumOrigins == 0)
    return; // No tracked references in this function.

  LLVM_DEBUG(FM.dump(*Cfg));

  // Phase 1: Backward origin liveness.
  BackwardLiveness Liveness(*Cfg, FM, NumOrigins);
  Liveness.run();

  // Phase 2a: Forward loan propagation with NLL filtering.
  ForwardLoanPropagation LoanProp(*Cfg, FM, Liveness);
  LoanProp.run();

  // Phase 2b: Forward move-state tracking.
  ForwardMoveTracking MoveTrack(*Cfg, FM);
  MoveTrack.run();

  // Phase 3: Error detection.
  ErrorDetector Detector(*Cfg, FM, Liveness, LoanProp, MoveTrack, Handler);
  Detector.run();
}

} // namespace clang
