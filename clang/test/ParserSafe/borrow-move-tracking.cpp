// RUN: %clang_cc1 -fsyntax-only -ftracked-references -std=c++20 -verify %s
//
// Tests for move tracking of exclusive tracked references (T^ mut) across
// divergent control flow. Exercises the three-valued move-state lattice
// (Initialized / Moved / MaybeMoved) and its interaction with CFG merges.
//
// Passing a tracked reference to a function creates an implicit reborrow
// (§9.7 of the Mizar borrow-checker spec). The callee gets a fresh borrow
// derived from the caller's reference; the caller's reference remains valid
// after the call returns. Therefore, function calls do NOT move tracked refs.

// ==========================================================================
// Helper declarations
// ==========================================================================

void consume(int^ mut p) safe;
void observe(int^ r) safe;

// ==========================================================================
// Test 1: Passing an exclusive ref to a function creates an implicit
//         reborrow — the original remains usable afterwards.
// ==========================================================================

void test_reborrow_exclusive() safe {
  int x = 10;
  int^ mut r = &x;
  consume(r);     // implicit reborrow, not a move
  *r = 42;        // OK — r is still valid
}

// ==========================================================================
// Test 2: Conditional function call — r is still valid after merge.
// ==========================================================================

void test_conditional_reborrow(bool cond) safe {
  int x = 10;
  int^ mut r = &x;
  if (cond) {
    consume(r);   // implicit reborrow on this path
  }
  // merge point: r is still Initialized on all paths
  *r = 42;        // OK
}

// ==========================================================================
// Test 3: Function call on both branches — r still valid at merge.
// ==========================================================================

void test_reborrow_both_branches(bool cond) safe {
  int x = 10;
  int^ mut r = &x;
  if (cond) {
    consume(r);   // reborrow
  } else {
    consume(r);   // reborrow
  }
  // merge point: r is Initialized (reborrows don't invalidate)
  *r = 42;        // OK
}

// ==========================================================================
// Test 4: No call on either branch — Initialized at merge, OK.
// ==========================================================================

void test_no_call_either_branch(bool cond) safe {
  int x = 10;
  int^ mut r = &x;
  if (cond) {
    *r = 20;      // uses r but does not move it
  } else {
    *r = 30;      // uses r but does not move it
  }
  // merge point: r is Initialized
  *r = 42;        // OK — no move on any path
}

// ==========================================================================
// Test 5: Re-initialization after conditional call — still valid.
// ==========================================================================

void test_reinit_after_conditional_call(bool cond) safe {
  int x = 10;
  int^ mut r = &x;
  if (cond) {
    consume(r);   // reborrow on this path
  }
  r = &x;         // re-initialize r
  *r = 42;        // OK
}

// ==========================================================================
// Test 6: Re-initialization after call.
// ==========================================================================

void test_reinit_after_call() safe {
  int x = 10;
  int^ mut r = &x;
  consume(r);     // reborrow
  r = &x;         // re-initialize
  *r = 42;        // OK
}

// ==========================================================================
// Test 7: Conditional initialization — never initialized on one path.
// Uninitialized origins are treated as Moved in the lattice.
// ==========================================================================

void test_conditional_init(bool cond) safe {
  int x = 10;
  int^ mut r;             // not initialized — treated as Moved
  if (cond) {
    r = &x;               // Initialized on this path
  }
  // merge: join(Initialized, Moved) = MaybeMoved
  *r = 42;        // expected-warning {{use of exclusive borrow that may have been moved}}
}

// ==========================================================================
// Test 8: Nested conditionals with function calls — still valid
//         because calls are reborrows, not moves.
// ==========================================================================

void test_nested_conditionals(bool a, bool b) safe {
  int x = 10;
  int^ mut r = &x;
  if (a) {
    if (b) {
      consume(r);          // reborrow, not move
    }
  }
  *r = 42;        // OK — reborrows don't invalidate
}

// ==========================================================================
// Test 9: Call in a loop — after the loop, r is still valid.
// ==========================================================================

void test_call_in_loop(bool cond) safe {
  int x = 10;
  int^ mut r = &x;
  while (cond) {
    consume(r);   // reborrow inside loop
    break;
  }
  *r = 42;        // OK
}

// ==========================================================================
// Test 10: Call + re-init in loop body — OK after loop.
// ==========================================================================

void test_call_reinit_in_loop(int count) safe {
  int x = 10;
  int^ mut r = &x;
  for (int i = 0; i < count; ++i) {
    consume(r);   // reborrow
    r = &x;       // re-initialize before end of loop body
  }
  *r = 42;        // OK
}

// ==========================================================================
// Test 11: Call in one arm of a switch — r still valid.
// ==========================================================================

void test_call_in_switch(int sel) safe {
  int x = 10;
  int^ mut r = &x;
  switch (sel) {
    case 0:
      consume(r); // reborrow
      break;
    case 1:
      *r = 20;    // uses r, no move
      break;
    default:
      break;      // r untouched
  }
  *r = 42;        // OK — reborrows don't invalidate
}

// ==========================================================================
// Test 12: Multiple exclusive refs — function calls are reborrows.
// ==========================================================================

void test_independent_refs(bool cond) safe {
  int x = 10;
  int y = 20;
  int^ mut r1 = &x;
  int^ mut r2 = &y;
  if (cond) {
    consume(r1);  // reborrow of r1
  }
  *r2 = 42;       // OK — r2 is unaffected
  *r1 = 42;       // OK — reborrows don't invalidate
}
