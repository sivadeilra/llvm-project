// RUN: %clang_cc1 -fsyntax-only -ftracked-references -std=c++20 -verify %s
//
// Tests for drop flags: the interaction between conditional moves,
// user-defined destructors, and the borrow checker.
//
// In Rust, "drop flags" are runtime booleans inserted by the compiler when
// a variable with a destructor is conditionally moved. In Mizar/C++:
// - Tracked references are trivial (no destructor), so no runtime flag
//   is needed for them.
// - But user-defined destructors on borrowed-from objects interact with
//   the move state of tracked references that borrow their fields.
//
// These tests validate §7.3.5 and §9.8 of the borrow checker specification.

// ==========================================================================
// Helper declarations
// ==========================================================================

void consume_ref(int^ mut p) safe;
void use_ref(int^ p) safe;

extern "C" void log_value(int v);

struct TrivialType {
  int value;
  // No destructor — trivially destructible
};

struct NonTrivialGuard {
  int value;
  ~NonTrivialGuard() { log_value(value); }
};

struct MultiFieldGuard {
  int a;
  int b;
  ~MultiFieldGuard() { log_value(a + b); }
};

// --------------------------------------------------------------------------
// Types for drop-glue tests (implicit destructor, no user-provided dtor)
// --------------------------------------------------------------------------

struct Inner {
  int data;
  ~Inner() { log_value(data); }  // user-provided destructor
};

// DropGlueType has NO user-provided destructor. The compiler generates
// implicit drop glue that calls Inner::~Inner() on .resource but never
// touches .value (which is trivially destructible).
struct DropGlueType {
  int value;
  Inner resource;
};

// AllTrivialFields: has a non-trivial field (Inner) and a trivially-
// destructible int, but no user-provided destructor.
struct TwoFields {
  Inner field_a;
  int field_b;
};

// ==========================================================================
// Test 1: Trivial type — no destructor interaction, borrow ends at last use.
// ==========================================================================

void test_trivial_no_destructor_conflict() safe {
  TrivialType t{42};
  int^ mut r = &t.value;
  *r = 100;
  // r's last use is above. NLL kills the borrow.
  // No CFGAutomaticObjDtor for TrivialType — no write event.
  // t's storage ends at CFGLifetimeEnds(t) — OK because borrow is dead.
}

// ==========================================================================
// Test 2: Non-trivial type — destructor creates implicit write event.
//         Borrow must be dead before destructor runs.
// ==========================================================================

void test_nontrivial_destructor_conflicts() safe {
  NonTrivialGuard g{42};
  int^ mut r = &g.value;   // exclusive borrow of g.value
  *r = 100;
  // Even though r is not used after this point, g's destructor accesses g.
  // CFGAutomaticObjDtor(g) → WriteEvent(g) conflicts with loan on g.value.
  // expected-error {{cannot destroy 'g' because a field is exclusively borrowed}}
  // expected-note {{exclusive borrow of 'g.value' created here}}
}

// ==========================================================================
// Test 3: Conditional move of borrow into non-trivial type's field.
//         The destructor conflicts on the path where the borrow is NOT moved.
// ==========================================================================

void test_conditional_move_destructor(bool cond) safe {
  NonTrivialGuard g{42};
  int^ mut r = &g.value;   // exclusive borrow of g.value
  if (cond) {
    consume_ref(r);         // moves r on this path — loan released
  }
  // merge: r is MaybeMoved
  // On !cond path: r still holds loan on g.value
  // CFGAutomaticObjDtor(g) → WriteEvent(g) — conflicts on !cond path
  // Borrow checker is conservative: reports the conflict.
  // expected-error {{cannot destroy 'g' because a field is exclusively borrowed}}
  // expected-note {{exclusive borrow of 'g.value' created here}}
}

// ==========================================================================
// Test 4: Both branches move the borrow — destructor is safe.
// ==========================================================================

void test_both_branches_move_destructor_ok(bool cond) safe {
  NonTrivialGuard g{42};
  int^ mut r = &g.value;   // exclusive borrow of g.value
  if (cond) {
    consume_ref(r);         // moves r
  } else {
    consume_ref(r);         // also moves r
  }
  // merge: r is Moved (definite) — loan is released on all paths
  // CFGAutomaticObjDtor(g) → WriteEvent(g), but no live loan conflicts.
  // OK — no error
}

// ==========================================================================
// Test 5: Borrow of field dropped before destructor — NLL allows this.
// ==========================================================================

void test_borrow_dead_before_destructor() safe {
  NonTrivialGuard g{42};
  {
    int^ mut r = &g.value;
    *r = 100;
    // r goes out of scope here — loan dies
  }
  // g's destructor runs after the inner scope — no live borrow.
  // OK — no error
}

// ==========================================================================
// Test 6: Shared borrow + non-trivial destructor — still conflicts because
//         the destructor takes mutable access.
// ==========================================================================

void test_shared_borrow_destructor_conflict() safe {
  NonTrivialGuard g{42};
  int^ r = &g.value;      // shared borrow of g.value
  int copy = *r;           // last use of r
  // But g.~NonTrivialGuard() is a mutable access to g, which conflicts
  // with any live loan on g.value. NLL: r is dead after `*r`, so...
  // Actually: r IS dead after the last use. NLL should NOT report an error.
  // The destructor WriteEvent happens after r's last use, and NLL would
  // have killed O_r's liveness by then.
  // OK — no error (NLL correctly allows this)
}

// ==========================================================================
// Test 7: Shared borrow used AFTER the point where destructor implications
//         would matter — error because loan extends past destructor.
// ==========================================================================

// This test requires that the borrow outlives the borrowed-from object.

void test_shared_borrow_outlives_destructor() safe {
  int^ r;
  {
    NonTrivialGuard g{42};
    r = &g.value;           // shared borrow of g.value
    // g.~NonTrivialGuard() runs at end of this scope
    // CFGAutomaticObjDtor(g) → WriteEvent(g), and r is live (used below)
    // expected-error {{cannot destroy 'g' because a field is borrowed}}
    // expected-note {{borrow of 'g.value' created here}}
  }
  int val = *r;             // use of r after g is destroyed
  // Also a dangling reference error:
  // expected-error {{'g' does not live long enough}}
}

// ==========================================================================
// Test 8: Multi-field type — borrows of distinct fields with destructor.
// ==========================================================================

void test_multifield_destructor_distinct_borrows() safe {
  MultiFieldGuard m{1, 2};
  int^ mut ra = &m.a;      // exclusive borrow of m.a
  int^ mut rb = &m.b;      // exclusive borrow of m.b
  *ra = 10;
  *rb = 20;
  // Both borrows are alive when m.~MultiFieldGuard() runs.
  // CFGAutomaticObjDtor(m) → WriteEvent(m) conflicts with both loans.
  // expected-error {{cannot destroy 'm' because a field is exclusively borrowed}}
}

// ==========================================================================
// Test 9: Drop one borrow explicitly, keep the other — still conflicts.
// ==========================================================================

void test_multifield_one_dropped(bool cond) safe {
  MultiFieldGuard m{1, 2};
  int^ mut ra = &m.a;
  *ra = 10;                 // last use of ra — NLL kills it
  // ra is dead. But m.b was never borrowed.
  // CFGAutomaticObjDtor(m) runs — no live loans conflict with m.
  // OK — NLL correctly allows this.
}

// ==========================================================================
// Test 10: Conditional init + destructor — the full "drop flag" scenario.
// ==========================================================================

void test_conditional_init_with_destructor(bool cond) safe {
  int x = 10;
  NonTrivialGuard g{42};
  int^ mut r;               // uninitialized — treated as Moved
  if (cond) {
    r = &g.value;           // Initialized on this path
  }
  // merge: r is MaybeMoved
  // On cond path: r holds loan on g.value, live if used below
  // On !cond path: r is uninitialized, no loan

  // Using r here would be: expected-error {{use of exclusive borrow that may have been moved}}
  // But even without an explicit use, on the cond path the loan is live
  // at g's destructor point → conservative error.
  // expected-error {{cannot destroy 'g' because a field may be exclusively borrowed}}
}

// ==========================================================================
// Test 11: Drop glue — borrow of trivially-destructible field.
//          Implicit destructor only calls Inner::~Inner() on .resource;
//          it never touches .value. Loan on .value should NOT conflict.
// ==========================================================================

void test_drop_glue_trivial_field_ok() safe {
  DropGlueType dg;
  dg.value = 1;
  int^ mut r = &dg.value;  // borrow of .value (int — trivially destructible)
  *r = 42;
  // Implicit dtor runs: Inner::~Inner() on .resource only.
  // .value is trivially destructible → drop glue doesn't touch it.
  // Loan on .value does NOT expire at the implicit dtor → no conflict.
  // OK — no error
}

// ==========================================================================
// Test 12: Drop glue — borrow of non-trivially-destructible field.
//          Implicit destructor calls Inner::~Inner() on .resource, which
//          IS a write to .resource → conflict with loan on .resource.data.
// ==========================================================================

void test_drop_glue_nontrivial_field_conflict() safe {
  DropGlueType dg;
  int^ mut r = &dg.resource.data;  // borrow of .resource.data
  *r = 42;
  // Implicit dtor calls Inner::~Inner() → Inner has a user-provided dtor
  // which accesses .resource (equiv to `impl Drop`).
  // TODO: This test requires deeper access path modeling (nested fields).
  // For now, the loan is on `dg` (base variable), and any destruction of
  // dg expires it. This test documents the intended future behavior.
}

// ==========================================================================
// Test 13: User-provided dtor — borrow of ANY field conflicts.
//          NonTrivialGuard has ~NonTrivialGuard() which accesses `this`.
//          Even a borrow of a trivially-destructible field (.value) must
//          expire because the user dtor can access it.
// ==========================================================================

void test_user_dtor_trivial_field_still_conflicts() safe {
  NonTrivialGuard g{42};
  int^ mut r = &g.value;   // borrow of .value
  *r = 100;
  // g.~NonTrivialGuard() runs: user-provided dtor accesses g.value.
  // Even though .value is `int` (trivially destructible), the user-provided
  // dtor has whole-object `this` access → loan expires → conflict if live.
  // expected-error {{cannot destroy 'g' because a field is exclusively borrowed}}
  // expected-note {{exclusive borrow of 'g.value' created here}}
}

// ==========================================================================
// Test 14: Drop glue with two fields — borrow trivial field while
//          non-trivial field destructs. Should be safe.
// ==========================================================================

void test_drop_glue_two_fields_independent() safe {
  TwoFields tf;
  int^ mut r = &tf.field_b;  // borrow of .field_b (int — trivially destructible)
  *r = 42;
  // Implicit dtor calls Inner::~Inner() on .field_a only.
  // .field_b is trivially destructible → drop glue doesn't touch it.
  // OK — no error
}
