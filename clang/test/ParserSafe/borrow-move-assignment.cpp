// RUN: %clang_cc1 -fsyntax-only -ftracked-references -std=c++20 -verify %s
//
// Move tracking for exclusive tracked references transferred via declaration
// and assignment.

// --------------------------------------------------------------------------
// M1: Decl-init transfer of exclusive tracked ref moves source.
// --------------------------------------------------------------------------
void m1_decl_init_moves_source() safe {
  int x = 1;
  int^ mut r = &x;
  int^ mut s = r; // expected-note {{value moved here}}
  *s = 2;
  *r = 3; // expected-warning {{use of exclusive borrow after move}}
}

// --------------------------------------------------------------------------
// M2: Assignment transfer of exclusive tracked ref moves source.
// --------------------------------------------------------------------------
void m2_assignment_moves_source() safe {
  int x = 1;
  int y = 2;
  int^ mut r = &x;
  int^ mut s = &y;
  s = r; // expected-note {{value moved here}}
  *s = 2;
  *r = 3; // expected-warning {{use of exclusive borrow after move}}
}

// --------------------------------------------------------------------------
// M3: Conditional transfer yields maybe-moved source.
// --------------------------------------------------------------------------
void m3_conditional_transfer_maybe_moved(bool cond) safe {
  int x = 1;
  int y = 2;
  int^ mut r = &x;
  int^ mut s = &y;
  if (cond)
    s = r; // expected-note {{value moved here}}
  *r = 3; // expected-warning {{use of exclusive borrow that may have been moved}}
}

// --------------------------------------------------------------------------
// M4: Re-initialization after move makes source valid again.
// --------------------------------------------------------------------------
void m4_reinit_clears_moved_state() safe {
  int x = 1;
  int y = 2;
  int^ mut r = &x;
  int^ mut s = r;
  r = &y;
  *r = 2; // OK
  *s = 3;
}

// --------------------------------------------------------------------------
// M5: Shared tracked refs are copy-like (no move tracking warnings).
// --------------------------------------------------------------------------
void m5_shared_transfer_no_move() safe {
  int x = 1;
  int^ a = &x;
  int^ b = a;
  (void)*a;
  (void)*b;
}
