//===--- OutlivesRelation.h - Lifetime Outlives Ordering --------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file defines OutlivesRelation, which computes and queries the transitive
// closure of lifetime outlives constraints derived from 'requires' clauses.
//
// Example: In `template<lifetime @a, @b> requires @a : @b`, the OutlivesRelation
// will compute that @a outlives @b, and transitive chains like @a outlives @b
// and @b outlives @c means @a outlives @c.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CLANG_SEMA_OUTLIVESRELATION_H
#define LLVM_CLANG_SEMA_OUTLIVESRELATION_H

#include "clang/AST/DeclTemplate.h"
#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/DenseSet.h"
#include "llvm/ADT/SmallVector.h"

namespace clang {

class LifetimeParmDecl;
class Expr;

/// Computes and caches the transitive closure of lifetime outlives constraints.
///
/// Given a TemplateParameterList with a 'requires' clause containing lifetime
/// constraints (e.g., `requires @a : @b && @b : @c`), this class builds a
/// directed graph and can answer queries like "does @a outlive @d?" by checking
/// transitive paths through the constraint graph.
///
/// The class also includes implicit axioms:
/// - @static outlives all other lifetimes (reflexive top element)
/// - All lifetimes outlive themselves (reflexivity)
class OutlivesRelation {
public:
  /// Construct from a TemplateParameterList.
  /// Extracts all lifetime constraints from the requires clause (if any)
  /// and builds the outlives relation graph.
  explicit OutlivesRelation(const TemplateParameterList *TPL);

  /// Destructor
  ~OutlivesRelation() = default;

  /// Query: Does @src outlive @tgt?
  /// 
  /// Returns true if:
  /// - src == tgt (reflexive), OR
  /// - there is a path src → ... → tgt in the constraint graph
  ///
  /// Examples:
  /// - Given `requires @a : @b`: outlives(@a, @b) = true, outlives(@b, @a) = false
  /// - Given `requires @a : @b && @b : @c`: outlives(@a, @c) = true (transitive)
  /// - Given any lifetimes: outlives(x, x) = true for all x
  bool outlives(LifetimeParmDecl *src, LifetimeParmDecl *tgt) const;

  /// Check if the constraint graph is empty (no explicit constraints).
  /// Useful for early returns in validation logic.
  bool isEmpty() const { return graph.empty(); }

  /// Returns true if the constraint graph has any edge where @a or @b
  /// appears as source or target. Used to distinguish "wrong direction"
  /// (constrained but reversed) from "no declared relationship at all".
  bool hasConstraintInvolving(LifetimeParmDecl *a, LifetimeParmDecl *b) const;

private:
  /// Internal graph representation: adjacency list.
  /// graph[src] = {tgt1, tgt2, ...} means "src outlives tgt1 and tgt2"
  llvm::DenseMap<LifetimeParmDecl *, llvm::SmallVector<LifetimeParmDecl *, 4>>
      graph;

  /// Add a directed edge src → tgt to the constraint graph.
  void addEdge(LifetimeParmDecl *src, LifetimeParmDecl *tgt);

  /// Compute transitive closure: Does @src reach @tgt via constraint edges?
  /// Uses depth-first search with a visited set to avoid cycles.
  bool canReach(LifetimeParmDecl *src, LifetimeParmDecl *tgt,
                llvm::DenseSet<LifetimeParmDecl *> &visited) const;

  /// Extract lifetime constraints from a 'requires' clause expression.
  /// Handles both atomic LifetimeConstraintExpr and && chains.
  void extractConstraintsFromExpr(const Expr *E);
};

} // namespace clang

#endif // LLVM_CLANG_SEMA_OUTLIVESRELATION_H
