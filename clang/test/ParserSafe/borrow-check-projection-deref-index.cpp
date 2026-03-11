// RUN: %clang_cc1 -fsyntax-only -ftracked-references -std=c++20 -verify %s
//
// Deref/index projection tests for Ladder 1 conservative path extraction.

struct Pair {
  int x;
  int y;
};

// --------------------------------------------------------------------------
// D1: Deref of address-of local should resolve back to the same base path.
// --------------------------------------------------------------------------
void d1_deref_of_addrof_local_ok() safe {
  Pair p{1, 2};
  int^ mut rx = &(*(&p)).x;
  int^ mut ry = &p.y; // OK: disjoint sibling field paths
  *rx = 3;
  *ry = 4;
}

// --------------------------------------------------------------------------
// D2: Parent-child conflict still applies through deref-normalized base.
// --------------------------------------------------------------------------
void d2_deref_parent_child_conflict() safe {
  Pair p{1, 2};
  Pair^ mut whole = &p; // expected-note {{exclusive borrow of 'p.x' created here}}
  int^ mut rx = &(*(&p)).x; // expected-warning {{cannot borrow 'p.x' as exclusive because it is already borrowed}}
  (*whole).y = 5;
  *rx = 6;
}

// --------------------------------------------------------------------------
// I1: Array indexing is modeled conservatively (indices may alias).
// --------------------------------------------------------------------------
void i1_index_conservative_aliasing() safe {
  int a[2] = {1, 2};
  int^ mut i0 = &a[0]; // expected-note {{exclusive borrow of 'a[_]' created here}}
  int^ mut i1 = &a[1]; // expected-warning {{cannot borrow 'a[_]' as exclusive because it is already borrowed}}
  *i0 = 3;
  *i1 = 4;
}

// --------------------------------------------------------------------------
// I2: Write to indexed element conflicts while a borrow of indexed path lives.
// --------------------------------------------------------------------------
void i2_index_write_conflict() safe {
  int a[2] = {1, 2};
  int^ mut i0 = &a[0]; // expected-note {{borrow of 'a[_]' is still live}}
  a[1] = 10; // expected-warning {{cannot assign to 'a[_]' because it is borrowed}}
  *i0 = 11;
}
