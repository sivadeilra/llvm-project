// RUN: %clang_cc1 -fsyntax-only -ftracked-references -std=c++20 -verify %s
//
// Nested projection and inheritance-aware borrow-check tests.
//
// C++ inheritance has no direct Rust equivalent, so these tests intentionally
// use conservative semantics: whole-object borrows conflict with borrows of
// inherited fields, while clearly disjoint fields remain borrowable together.

struct Pair {
  int x;
  int y;
};

struct Outer {
  Pair inner;
  int z;
};

struct Base {
  int bx;
};

struct Derived : Base {
  int dy;
};

struct LeftBase {
  int lx;
};

struct RightBase {
  int ry;
};

struct MultiDerived : LeftBase, RightBase {
  int dz;
};

// --------------------------------------------------------------------------
// N1: Nested sibling fields are disjoint.
// --------------------------------------------------------------------------
void n1_nested_sibling_fields_ok() safe {
  Outer o{{1, 2}, 3};
  int^ mut rx = &o.inner.x;
  int^ mut ry = &o.inner.y; // OK: disjoint nested sibling paths
  *rx = 4;
  *ry = 5;
}

// --------------------------------------------------------------------------
// N2: Nested parent-child conflict.
// --------------------------------------------------------------------------
void n2_nested_parent_child_conflict() safe {
  Outer o{{1, 2}, 3};
  Pair^ mut inner = &o.inner; // expected-note {{exclusive borrow of 'o' created here}}
  int^ mut x = &o.inner.x; // expected-warning {{cannot borrow 'o' as exclusive because it is already borrowed}}
  (*inner).y = 4;
  *x = 5;
}

// --------------------------------------------------------------------------
// N3: Writes to disjoint nested paths are allowed; same path conflicts.
// --------------------------------------------------------------------------
void n3_nested_write_conflict() safe {
  Outer o{{1, 2}, 3};
  int^ mut rx = &o.inner.x; // expected-note {{borrow of 'o' is still live}}
  o.inner.y = 7; // OK: disjoint nested field
  o.z = 8;       // OK: disjoint top-level field
  o.inner.x = 9; // expected-warning {{cannot assign to 'o' because it is borrowed}}
  *rx = 10;
}

// --------------------------------------------------------------------------
// I1: Inherited field and derived field are disjoint.
// --------------------------------------------------------------------------
void i1_inherited_and_derived_fields_ok() safe {
  Derived d{{1}, 2};
  int^ mut bx = &d.bx;
  int^ mut dy = &d.dy; // OK: inherited field and derived field are disjoint
  *bx = 3;
  *dy = 4;
}

// --------------------------------------------------------------------------
// I2: Whole derived-object borrow conflicts with inherited field borrow.
// --------------------------------------------------------------------------
void i2_whole_object_vs_inherited_field_conflict() safe {
  Derived d{{1}, 2};
  Derived^ mut whole = &d; // expected-note {{exclusive borrow of 'd' created here}}
  int^ mut bx = &d.bx; // expected-warning {{cannot borrow 'd' as exclusive because it is already borrowed}}
  (*whole).dy = 5;
  *bx = 6;
}

// --------------------------------------------------------------------------
// I3: Distinct direct-base fields in multiple inheritance are treated as
//     disjoint projected fields.
// --------------------------------------------------------------------------
void i3_multiple_inheritance_distinct_base_fields_ok() safe {
  MultiDerived d{{1}, {2}, 3};
  int^ mut lx = &d.lx;
  int^ mut ry = &d.ry; // OK: distinct direct base subobject fields
  *lx = 4;
  *ry = 5;
}

// --------------------------------------------------------------------------
// I4: Borrow through explicit base cast aliases the same storage path.
//     Conservative rule: `static_cast<Base&>(d).bx` conflicts with `d.bx`.
// --------------------------------------------------------------------------
void i4_base_cast_alias_conflict() safe {
  Derived d{{1}, 2};
  int^ mut b1 = &d.bx; // expected-note {{exclusive borrow of 'd' created here}}
  int^ mut b2 = &static_cast<Base&>(d).bx; // expected-warning {{cannot borrow 'd' as exclusive because it is already borrowed}}
  *b1 = 3;
  *b2 = 4;
}

// --------------------------------------------------------------------------
// I5: Distinct base subobjects in multiple inheritance remain disjoint even
//     when one side is accessed through an explicit base cast.
// --------------------------------------------------------------------------
void i5_multi_base_cast_disjoint_ok() safe {
  MultiDerived d{{1}, {2}, 3};
  int^ mut lx = &static_cast<LeftBase&>(d).lx;
  int^ mut ry = &static_cast<RightBase&>(d).ry; // OK: distinct direct bases
  *lx = 4;
  *ry = 5;
}
