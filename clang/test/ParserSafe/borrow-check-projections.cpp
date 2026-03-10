// RUN: %clang_cc1 -fsyntax-only -ftracked-references -std=c++20 -verify %s
//
// Projection-aware borrow checking spec tests (Ladder 1).
//
// These tests encode intended behavior for AccessPath projection +
// prefix-conflict checking. They are expected to fail until the
// implementation moves beyond root-only conflict checks.

struct Pair {
  int x;
  int y;
};

// --------------------------------------------------------------------------
// P1: Disjoint sibling fields may be borrowed exclusively at the same time.
// --------------------------------------------------------------------------
void p1_disjoint_exclusive_ok() safe {
  Pair p{1, 2};
  int^ mut rx = &p.x;
  int^ mut ry = &p.y; // OK: disjoint paths p.x and p.y
  *rx = 3;
  *ry = 4;
}

// --------------------------------------------------------------------------
// P2: Same-field exclusive/exclusive conflicts.
// --------------------------------------------------------------------------
void p2_same_field_exclusive_conflict() safe {
  Pair p{1, 2};
  int^ mut r1 = &p.x; // expected-note {{exclusive borrow of 'p' created here}}
  int^ mut r2 = &p.x; // expected-warning {{cannot borrow 'p' as exclusive because it is already borrowed}}
  *r1 = 1;
  *r2 = 2;
}

// --------------------------------------------------------------------------
// P3: Parent-child conflict (whole-object exclusive vs field exclusive).
// --------------------------------------------------------------------------
void p3_parent_child_exclusive_conflict() safe {
  Pair p{1, 2};
  Pair^ mut whole = &p; // expected-note {{exclusive borrow of 'p' created here}}
  int^ mut field = &p.x; // expected-warning {{cannot borrow 'p' as exclusive because it is already borrowed}}
  (*whole).y = 3;
  *field = 4;
}

// --------------------------------------------------------------------------
// P4: Parent-child conflict (whole-object shared vs field exclusive).
// --------------------------------------------------------------------------
void p4_parent_child_shared_exclusive_conflict() safe {
  Pair p{1, 2};
  Pair^ s = &p; // expected-note {{shared borrow of 'p' created here}}
  int^ mut m = &p.x; // expected-warning {{cannot borrow 'p' as exclusive because it is already borrowed}}
  (void)(*s).x;
  *m = 3;
}

// --------------------------------------------------------------------------
// P5: Sibling shared/exclusive on disjoint fields is allowed.
// --------------------------------------------------------------------------
void p5_disjoint_shared_exclusive_ok() safe {
  Pair p{1, 2};
  int^ sx = &p.x;
  int^ mut my = &p.y; // OK: disjoint paths p.x and p.y
  (void)*sx;
  *my = 42;
}

// --------------------------------------------------------------------------
// P6: Write to borrowed field conflicts, disjoint field write is allowed.
// --------------------------------------------------------------------------
void p6_write_conflict_and_disjoint_ok() safe {
  Pair p{1, 2};
  int^ mut rx = &p.x; // expected-note {{borrow of 'p' is still live}}
  p.y = 11; // OK: write to disjoint field
  p.x = 12; // expected-warning {{cannot assign to 'p' because it is borrowed}}
  *rx = 13;
}

// --------------------------------------------------------------------------
// P7: Whole-object write conflicts with any live field borrow.
// --------------------------------------------------------------------------
void p7_whole_write_conflicts_with_field_borrow() safe {
  Pair p{1, 2};
  int^ mut rx = &p.x; // expected-note {{borrow of 'p' is still live}}
  p = Pair{5, 6}; // expected-warning {{cannot assign to 'p' because it is borrowed}}
  *rx = 7;
}
