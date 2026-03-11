// RUN: %clang_cc1 -fsyntax-only -ftracked-references -std=c++20 -verify %s
//
// Parent/child write-path conflict tests for projected AccessPath checks.

struct Pair {
  int x;
  int y;
};

struct Outer {
  Pair inner;
  int z;
};

// --------------------------------------------------------------------------
// W1: Borrow child path, then write parent subobject.
// --------------------------------------------------------------------------
void w1_child_borrow_parent_write_conflict() safe {
  Outer o{{1, 2}, 3};
  int^ mut rx = &o.inner.x; // expected-note {{borrow of 'o.inner' is still live}}
  o.inner = Pair{4, 5}; // expected-warning {{cannot assign to 'o.inner' because it is borrowed}}
  *rx = 6;
}

// --------------------------------------------------------------------------
// W2: Borrow parent subobject, then write child path.
// --------------------------------------------------------------------------
void w2_parent_borrow_child_write_conflict() safe {
  Outer o{{1, 2}, 3};
  Pair^ mut inner = &o.inner; // expected-note {{borrow of 'o.inner.x' is still live}}
  o.inner.x = 7; // expected-warning {{cannot assign to 'o.inner.x' because it is borrowed}}
  (*inner).y = 8;
}

// --------------------------------------------------------------------------
// W3: Borrow nested child, then whole-object write conflicts.
// --------------------------------------------------------------------------
void w3_child_borrow_whole_write_conflict() safe {
  Outer o{{1, 2}, 3};
  int^ mut rx = &o.inner.x; // expected-note {{borrow of 'o' is still live}}
  o = Outer{{9, 10}, 11}; // expected-warning {{cannot assign to 'o' because it is borrowed}}
  *rx = 12;
}

// --------------------------------------------------------------------------
// W4: Mixed assignment form still triggers conflict checks.
// --------------------------------------------------------------------------
void w4_mixed_assignment_form_conflict() safe {
  Outer o{{1, 2}, 3};
  // expected-note@+1 {{borrow of 'o.inner' is still live}}
  int^ mut rx = &o.inner.x;
  // expected-warning@+1 {{cannot assign to 'o.inner' because it is borrowed}}
  (void)(o.inner = Pair{13, 14});
  *rx = 15;
}
