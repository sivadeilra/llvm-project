# Mizar Borrow Checker Roadmap (Rust-Guided)

Last updated: 2026-03-10

## Scope
This roadmap tracks borrow-checker completeness for tracked references (`T^`, `T^ mut`) using Rust as the semantic guide and C++ object-model constraints as conservative boundaries.

## Current State (Implemented)
- NLL core is in place (origin liveness + loan propagation).
- Path-sensitive conflicts are implemented for:
  - field projections
  - conservative deref/index projections
  - inheritance/base-cast projection normalization for safe cast kinds
- Projected-path diagnostics are emitted (for example `'p.x'`, `'a[_]'`).
- Branch-sensitive projected-path liveness is covered by tests.
- Parent/child write-path conflicts are covered, including mixed assignment forms.
- Virtual inheritance and ambiguous-base edge behavior are tested with conservative policy.

## Rust-Model Audit
- Parity area: aliasing discipline for shared vs exclusive borrows on local, intra-procedural paths.
- Parity area: NLL-style liveness shortening for many borrow patterns.
- Conservative divergence: index aliasing (`a[i]`) currently treated as potentially aliasing all siblings under same base.
- Missing vs Rust model:
  - comprehensive move fact emission (all semantic move sites)
  - explicit/implicit reborrow + freeze/unfreeze semantics
  - destructor-precision invalidation (drop interactions)
  - inter-procedural borrow/lifetime contract reasoning

## Priority Roadmap

### Milestone 1: Ladder 2 - Move Semantics Completeness
Goal: Ensure move state reflects all real move points and feeds diagnostics accurately.

Planned work:
- Emit `MoveOriginFact` at all consuming expression/statement sites.
- Validate merge behavior (`Moved` vs `MaybeMoved`) with branching tests.
- Validate reinitialization transitions after move.

Exit criteria:
- Definite-move/use and maybe-move/use tests are complete and stable.
- No regressions in projection, lifetime, and pragma suites.

### Milestone 2: Ladder 3 - Reborrow and Freeze
Goal: Match Rust ergonomics for `mut` references in calls/assignments without unsound moves.

Planned work:
- Add facts and transfer rules for reborrow/freeze/unfreeze.
- Implement conservative call-boundary lifetime for implicit reborrows.
- Add tests for sequential calls and use-during-freeze failures.

Exit criteria:
- `T^ mut` call patterns behave as Rust-style short-lived reborrows.

### Milestone 3: Ladder 4 - Destructor Precision
Goal: Model destructor-triggered writes/uses with projected-path precision.

Planned work:
- Emit destructor write/invalidation facts using path-aware rules.
- Diagnose conflicts at destructor sites with actionable notes.

Exit criteria:
- Non-trivial destructor conflict tests pass and point to correct sites.

### Milestone 4: Ladder 5 - Inter-Procedural Precision
Goal: Use signature/lifetime contracts to reduce over-conservative call behavior.

Planned work:
- Seed origin/loan state for parameters from signature contracts.
- Constrain return-origin behavior from outlives contracts.

Exit criteria:
- Contract-driven call/return tests pass with improved acceptance precision.

## Recommended Next Iteration (Immediate)
1. Land Ladder 2 test-first matrix for move sites (calls, assignments, returns, conditional moves).
2. Implement missing `MoveOriginFact` generation paths and verify with full ParserSafe Mizar suite.
3. Add diagnostics notes clarifying move origin and reinitialization state transitions.

## Test Gate For Every Milestone
Run and keep green (allowing known XFAILs):
- `clang/test/ParserSafe/borrow-check-*.cpp`
- `clang/test/ParserSafe/lifetime-*.cpp`
- `clang/test/ParserSafe/pragma-mizar*.cpp`
