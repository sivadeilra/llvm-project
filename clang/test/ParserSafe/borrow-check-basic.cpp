// RUN: %clang_cc1 -fsyntax-only -ftracked-references -std=c++20 -verify %s
//
// Basic tests for the Mizar NLL borrow checker (Phase A):
//   - Conflicting exclusive borrows
//   - Shared borrow while exclusively borrowed
//   - Dangling reference (variable goes out of scope while borrowed)
//
// These test the three-phase NLL algorithm:
//   Phase 0: Fact generation from CFG
//   Phase 1: Backward origin liveness
//   Phase 2: Forward loan propagation with NLL filtering
//   Phase 3: Error detection

// ==========================================================================
// Test 1: Two exclusive borrows of the same variable — conflict.
// ==========================================================================

void test_exclusive_conflict() safe {
  int x = 10;
  int^ mut r1 = &x;           // exclusive borrow of x
  int^ mut r2 = &x;           // expected-warning {{cannot borrow 'x' as exclusive because it is already borrowed}}
                               // expected-note@-2 {{exclusive borrow of 'x' created here}}
  *r1 = 1;
  *r2 = 2;
}

// ==========================================================================
// Test 2: Shared borrow while exclusively borrowed — conflict.
// ==========================================================================

void test_shared_while_exclusive() safe {
  int x = 10;
  int^ mut r1 = &x;           // exclusive borrow of x
  int^ r2 = &x;               // expected-warning {{cannot borrow 'x' as shared because it is exclusively borrowed}}
                               // expected-note@-2 {{exclusive borrow of 'x' created here}}
  *r1 = 1;
  (void)*r2;
}

// ==========================================================================
// Test 3: Two shared borrows — allowed.
// ==========================================================================

void test_shared_shared_ok() safe {
  int x = 10;
  int^ r1 = &x;               // shared borrow of x
  int^ r2 = &x;               // OK: shared + shared is fine
  (void)*r1;
  (void)*r2;
}

// ==========================================================================
// Test 4: Dangling reference — borrow outlives the borrowed variable.
// ==========================================================================

void test_dangling_ref() safe {
  int^ r;
  {
    int x = 10;
    r = &x;                    // expected-warning {{'x' does not live long enough}}
  }
  (void)*r;                    // expected-note {{shared borrow used here}}
}

// ==========================================================================
// Test 5: NLL — exclusive borrow is dead before second borrow (no conflict).
// ==========================================================================

void test_nll_dead_borrow() safe {
  int x = 10;
  int^ mut r1 = &x;           // exclusive borrow of x
  *r1 = 1;
  // r1 is not used after this point → NLL makes it dead.
  int^ mut r2 = &x;           // OK: r1's origin is dead by NLL.
  *r2 = 2;
}

// ==========================================================================
// Test 6: Exclusive borrow, use, then reassign — second borrow OK.
// ==========================================================================

void test_reborrow_after_reassign() safe {
  int x = 10;
  int y = 20;
  int^ mut r = &x;            // exclusive borrow of x
  *r = 1;
  r = &y;                     // reassign origin: now borrows y
  int^ mut r2 = &x;           // OK: the old borrow of x was replaced
  *r = 2;
  *r2 = 3;
}

// ==========================================================================
// Test 7: No tracked references — analysis should be a no-op.
// ==========================================================================

void test_no_tracked_refs() safe {
  int x = 10;
  int y = x + 1;
  (void)y;
}

// ==========================================================================
// Test 8: Exclusive borrow used on one branch AFTER second borrow — NLL
//         keeps r1 alive across the branch, producing a conflict.
// ==========================================================================

void test_nll_branch_keeps_alive(bool cond) safe {
  int x = 10;
  int^ mut r1 = &x;           // exclusive borrow of x
  int^ mut r2 = &x;           // expected-warning {{cannot borrow 'x' as exclusive because it is already borrowed}}
                               // expected-note@-2 {{exclusive borrow of 'x' created here}}
  if (cond) {
    *r1 = 1;                  // use of r1 on this branch — keeps r1 live at r2
  }
  *r2 = 2;
}
