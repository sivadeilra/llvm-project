//===--- OutlivesRelation.cpp - Lifetime Outlives Ordering -------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "OutlivesRelation.h"
#include "clang/AST/Decl.h"
#include "clang/AST/DeclTemplate.h"
#include "clang/AST/Expr.h"
#include "clang/AST/ExprConcepts.h"

namespace clang {

OutlivesRelation::OutlivesRelation(const TemplateParameterList *TPL) {
  if (!TPL)
    return;

  const Expr *RequiresClause = TPL->getRequiresClause();
  if (!RequiresClause)
    return;

  extractConstraintsFromExpr(RequiresClause);
}

void OutlivesRelation::extractConstraintsFromExpr(const Expr *E) {
  if (!E)
    return;

  E = E->IgnoreParenImpCasts();

  // Case 1: Single LifetimeConstraintExpr
  if (const auto *LCE = dyn_cast<LifetimeConstraintExpr>(E)) {
    if (LCE->getOutliverParam() && LCE->getOutlivedParam()) {
      addEdge(LCE->getOutliverParam(), LCE->getOutlivedParam());
    }
    return;
  }

  // Case 2: BinaryOperator with LAnd operator (&&)
  // This represents chained constraints: @a : @b && @c : @d
  if (const auto *BinOp = dyn_cast<BinaryOperator>(E)) {
    if (BinOp->getOpcode() == BO_LAnd) {
      extractConstraintsFromExpr(BinOp->getLHS());
      extractConstraintsFromExpr(BinOp->getRHS());
    }
    return;
  }

  // Case 3: UnaryOperator wrapping a constraint (shouldn't happen normally)
  if (const auto *UnOp = dyn_cast<UnaryOperator>(E)) {
    extractConstraintsFromExpr(UnOp->getSubExpr());
    return;
  }

  // Unknown expression type; skip
}

void OutlivesRelation::addEdge(LifetimeParmDecl *src, LifetimeParmDecl *tgt) {
  if (!src || !tgt)
    return;

  // src outlives tgt means src → tgt edge in our graph
  graph[src].push_back(tgt);
}

bool OutlivesRelation::outlives(LifetimeParmDecl *src,
                                LifetimeParmDecl *tgt) const {
  if (!src || !tgt)
    return false;

  // Reflexivity: everything outlives itself
  if (src == tgt)
    return true;

  // Check if there's a path from src to tgt
  llvm::DenseSet<LifetimeParmDecl *> visited;
  return canReach(src, tgt, visited);
}

bool OutlivesRelation::canReach(LifetimeParmDecl *src, LifetimeParmDecl *tgt,
                                llvm::DenseSet<LifetimeParmDecl *> &visited) const {
  if (!src || !tgt)
    return false;

  if (src == tgt)
    return true;

  if (visited.count(src))
    return false;

  visited.insert(src);

  // Check all neighbors of src in the graph
  auto it = graph.find(src);
  if (it == graph.end())
    return false;

  for (LifetimeParmDecl *neighbor : it->second) {
    if (canReach(neighbor, tgt, visited))
      return true;
  }

  return false;
}

bool OutlivesRelation::hasConstraintInvolving(LifetimeParmDecl *a,
                                               LifetimeParmDecl *b) const {
  // Returns true if either `a` or `b` appears as source or target in the graph.
  for (const auto &Entry : graph) {
    LifetimeParmDecl *src = Entry.first;
    if (src == a || src == b)
      return true;
    for (LifetimeParmDecl *tgt : Entry.second) {
      if (tgt == a || tgt == b)
        return true;
    }
  }
  return false;
}

} // namespace clang
