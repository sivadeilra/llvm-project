// RUN: %clang_cc1 -fsyntax-only -ftracked-references -std=c++20 -verify %s
//
// Branch-sensitive NLL tests for projection-aware borrow checking.

struct Pair {
  int x;
  int y;
};

// --------------------------------------------------------------------------
// L1: Same-field exclusive borrow dies after last use; second borrow is OK.
// --------------------------------------------------------------------------
void l1_same_field_dead_borrow_ok() safe {
  Pair p{1, 2};
  int^ mut r1 = &p.x;
  *r1 = 1;
  int^ mut r2 = &p.x; // OK: r1 is dead by NLL
  *r2 = 2;
}

// --------------------------------------------------------------------------
// L2: Same-field exclusive borrow kept alive by future branch use.
// --------------------------------------------------------------------------
void l2_same_field_branch_keeps_alive(bool cond) safe {
  Pair p{1, 2};
  int^ mut r1 = &p.x; // expected-note {{exclusive borrow of 'p' created here}}
  int^ mut r2 = &p.x; // expected-warning {{cannot borrow 'p' as exclusive because it is already borrowed}}
  if (cond)
    *r1 = 1; // future use keeps r1 live at r2
  *r2 = 2;
}

// --------------------------------------------------------------------------
// L3: Disjoint sibling field borrow remains allowed even if first field is
//     kept live on one branch.
// --------------------------------------------------------------------------
void l3_disjoint_sibling_branch_ok(bool cond) safe {
  Pair p{1, 2};
  int^ mut rx = &p.x;
  int^ mut ry = &p.y; // OK: sibling path does not conflict
  if (cond)
    *rx = 1;
  *ry = 2;
}

// --------------------------------------------------------------------------
// L4: Whole-object exclusive borrow conflicts with field borrow that is kept
//     alive by future branch use.
// --------------------------------------------------------------------------
void l4_whole_object_branch_conflict(bool cond) safe {
  Pair p{1, 2};
  int^ mut rx = &p.x; // expected-note {{exclusive borrow of 'p' created here}}
  Pair^ mut whole = &p; // expected-warning {{cannot borrow 'p' as exclusive because it is already borrowed}}
  if (cond)
    *rx = 1; // future use keeps field borrow live at whole-object borrow
  (*whole).y = 2;
}
