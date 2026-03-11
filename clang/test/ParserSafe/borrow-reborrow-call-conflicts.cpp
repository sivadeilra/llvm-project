// RUN: %clang_cc1 -fsyntax-only -ftracked-references -std=c++20 -verify %s
//
// Conservative reborrow-conflict checks for a single call expression.

void take_two_mut(int^ mut a, int^ mut b) safe;
void take_two_shared(int^ a, int^ b) safe;

// --------------------------------------------------------------------------
// R1: Passing the same exclusive ref twice as mut should conflict.
// --------------------------------------------------------------------------
void r1_same_exclusive_twice_conflicts() safe {
  int x = 1;
  int^ mut r = &x;
  take_two_mut(r, r); // expected-note {{exclusive borrow of 'x' created here}} expected-warning {{cannot borrow 'x' as exclusive because it is already borrowed}}
}

// --------------------------------------------------------------------------
// R2: Passing the same shared ref twice is allowed.
// --------------------------------------------------------------------------
void r2_same_shared_twice_ok() safe {
  int x = 1;
  int^ r = &x;
  take_two_shared(r, r); // OK
}

