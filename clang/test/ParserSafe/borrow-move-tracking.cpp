// RUN: %clang_cc1 -fsyntax-only -ftracked-references -std=c++20 -verify %s
//
// Tests for move tracking of exclusive tracked references (T^ mut) across
// divergent control flow. Exercises the three-valued move-state lattice
// (Initialized / Moved / MaybeMoved) and its interaction with CFG merges.
//
// These tests validate the borrow checker's move analysis as specified in
// §7.3 of the Mizar borrow checker specification.

// ==========================================================================
// Helper declarations
// ==========================================================================

void consume(int^ mut p) safe;
void observe(int^ r) safe;

// ==========================================================================
// Test 1: Simple move — use after definite move is an error.
// ==========================================================================

void test_use_after_move() safe {
  int x = 10;
  int^ mut r = &x;
  consume(r);     // moves r
  *r = 42;        // expected-error {{use of exclusive borrow after move}}
                  // expected-note@-2 {{value moved here}}
}

// ==========================================================================
// Test 2: Conditional move on one branch — MaybeMoved at merge.
// ==========================================================================

void test_maybe_moved(bool cond) safe {
  int x = 10;
  int^ mut r = &x;
  if (cond) {
    consume(r);   // moves r on this path
  }
  // merge point: r is MaybeMoved
  *r = 42;        // expected-error {{use of exclusive borrow that may have been moved}}
                  // expected-note@-4 {{value moved here}}
}

// ==========================================================================
// Test 3: Move on both branches — definite Moved at merge.
// ==========================================================================

void test_moved_both_branches(bool cond) safe {
  int x = 10;
  int^ mut r = &x;
  if (cond) {
    consume(r);   // moves r
  } else {
    consume(r);   // also moves r
  }
  // merge point: r is Moved (both paths moved)
  *r = 42;        // expected-error {{use of exclusive borrow after move}}
}

// ==========================================================================
// Test 4: No move on either branch — Initialized at merge, OK.
// ==========================================================================

void test_no_move_either_branch(bool cond) safe {
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
// Test 5: Re-initialization after conditional move heals MaybeMoved.
// ==========================================================================

void test_reinit_after_conditional_move(bool cond) safe {
  int x = 10;
  int^ mut r = &x;
  if (cond) {
    consume(r);   // moves r on this path
  }
  // merge: MaybeMoved
  r = &x;         // re-initialize r → Initialized
  *r = 42;        // OK — r was re-initialized
}

// ==========================================================================
// Test 6: Re-initialization after definite move heals Moved.
// ==========================================================================

void test_reinit_after_definite_move() safe {
  int x = 10;
  int^ mut r = &x;
  consume(r);     // moves r
  r = &x;         // re-initialize → Initialized
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
  *r = 42;        // expected-error {{use of exclusive borrow that may have been moved}}
}

// ==========================================================================
// Test 8: Nested conditionals — multiple levels of branching.
// ==========================================================================

void test_nested_conditionals(bool a, bool b) safe {
  int x = 10;
  int^ mut r = &x;
  if (a) {
    if (b) {
      consume(r);          // moved only on (a && b) path
    }
  }
  // outer merge: join(MaybeMoved, Initialized) = MaybeMoved
  *r = 42;        // expected-error {{use of exclusive borrow that may have been moved}}
}

// ==========================================================================
// Test 9: Move in a loop — after the loop, state depends on whether
//         the loop body executed.
// ==========================================================================

void test_move_in_loop(bool cond) safe {
  int x = 10;
  int^ mut r = &x;
  while (cond) {
    consume(r);   // moves r inside loop
    // If the loop runs even once, r is Moved.
    // After loop: join(Initialized (never entered), Moved) = MaybeMoved
    break;
  }
  *r = 42;        // expected-error {{use of exclusive borrow that may have been moved}}
}

// ==========================================================================
// Test 10: Move + re-init in loop body — OK after loop.
// ==========================================================================

void test_move_reinit_in_loop(int count) safe {
  int x = 10;
  int^ mut r = &x;
  for (int i = 0; i < count; ++i) {
    consume(r);   // moves r
    r = &x;       // re-initialize before end of loop body
  }
  // After loop: r is Initialized (re-init at bottom of loop ensures this)
  *r = 42;        // OK
}

// ==========================================================================
// Test 11: Move in one arm of a switch.
// ==========================================================================

void test_move_in_switch(int sel) safe {
  int x = 10;
  int^ mut r = &x;
  switch (sel) {
    case 0:
      consume(r); // moves r
      break;
    case 1:
      *r = 20;    // uses r, no move
      break;
    default:
      break;      // r untouched
  }
  // merge: join(Moved, Initialized, Initialized) = MaybeMoved
  *r = 42;        // expected-error {{use of exclusive borrow that may have been moved}}
}

// ==========================================================================
// Test 12: Multiple exclusive refs — only the moved one is affected.
// ==========================================================================

void test_independent_refs(bool cond) safe {
  int x = 10;
  int y = 20;
  int^ mut r1 = &x;
  int^ mut r2 = &y;
  if (cond) {
    consume(r1);  // moves r1
  }
  // r1 is MaybeMoved, r2 is still Initialized
  *r2 = 42;       // OK — r2 was never moved
  *r1 = 42;       // expected-error {{use of exclusive borrow that may have been moved}}
}
