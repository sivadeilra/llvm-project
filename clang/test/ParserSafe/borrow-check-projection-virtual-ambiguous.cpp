// RUN: %clang_cc1 -fsyntax-only -ftracked-references -std=c++20 -verify %s
//
// Virtual-inheritance and ambiguous-base projection edge cases.

struct VBase {
  int vb;
};

struct LeftV : virtual VBase {
  int lx;
};

struct RightV : virtual VBase {
  int ry;
};

struct Diamond : LeftV, RightV {
  int dz;
};

struct AB1 {
  int bx; // expected-note {{member found by ambiguous name lookup}}
};

struct AB2 {
  int bx; // expected-note {{member found by ambiguous name lookup}}
};

struct AmbigDerived : AB1, AB2 {
  int dz;
};

// --------------------------------------------------------------------------
// V1: Accessing the same virtual-base field through different base casts
//     aliases the same storage and must conflict.
// --------------------------------------------------------------------------
void v1_virtual_base_alias_conflict() safe {
  Diamond d{};
  int^ mut a = &static_cast<LeftV&>(d).vb; // expected-note {{exclusive borrow of 'd.vb' created here}}
  int^ mut b = &static_cast<RightV&>(d).vb; // expected-warning {{cannot borrow 'd.vb' as exclusive because it is already borrowed}}
  *a = 1;
  *b = 2;
}

// --------------------------------------------------------------------------
// V2: Whole-object borrow conflicts with virtual-base field borrow.
// --------------------------------------------------------------------------
void v2_whole_vs_virtual_base_conflict() safe {
  Diamond d{};
  Diamond^ mut whole = &d; // expected-note {{exclusive borrow of 'd.vb' created here}}
  int^ mut vb = &static_cast<LeftV&>(d).vb; // expected-warning {{cannot borrow 'd.vb' as exclusive because it is already borrowed}}
  (*whole).dz = 3;
  *vb = 4;
}

// --------------------------------------------------------------------------
// A1: Distinct direct bases with same field spelling are disjoint when
//     explicitly disambiguated.
// --------------------------------------------------------------------------
void a1_explicit_ambiguous_bases_disjoint_ok() safe {
  AmbigDerived d{};
  int^ mut l = &static_cast<AB1&>(d).bx;
  int^ mut r = &static_cast<AB2&>(d).bx; // OK: distinct base subobjects
  *l = 5;
  *r = 6;
}

// --------------------------------------------------------------------------
// A2: Unqualified ambiguous-base access remains a front-end error.
// --------------------------------------------------------------------------
void a2_unqualified_ambiguous_base_is_error() safe {
  AmbigDerived d{};
  int^ mut bad = &d.bx; // expected-error {{member 'bx' found in multiple base classes of different types}}
  *bad = 7;
}
