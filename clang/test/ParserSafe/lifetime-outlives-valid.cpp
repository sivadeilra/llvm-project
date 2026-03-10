// RUN: %clang_cc1 -fsyntax-only -ftracked-references -std=c++20 %s -verify
// expected-no-diagnostics
//
// Mizar Lifetime Outlives — Positive Test Suite
// ==============================================
//
// This file tests **valid** use of outlives constraints on lifetime template
// parameters.  Every case here must remain diagnostics-free both now (syntax
// and declaration phase) and after conversion-time enforcement is implemented.
//
// Semantics recap (see clang/docs/MizarLifetimeOutlives.rst for the full spec):
//
//   @a : @b    — "@a outlives @b".  A reference T^@a may be coerced to T^@b.
//
// Lifetime partial order axioms (always true, no explicit constraint needed):
//
//   Reflexivity:  @x : @x   for every declared lifetime @x.
//   Top element:  @static : @x  for every declared lifetime @x.
//   Transitivity: @a : @b and @b : @c  ⟹  @a : @c.
//
// Permitted coercion direction:
//
//   ALLOWED:   T^@long → T^@short   when @long : @short  (shrinking)
//   REJECTED:  T^@short → T^@long   unless @short : @long (widening)
//
// Test categories in this file:
//   §A  Reflexivity — same lifetime used throughout.
//   §B  Static top-lifetime axiom.
//   §C  Explicit single-edge constraints.
//   §D  Transitivity chains.
//   §E  Contrapositive (the absence of a constraint does NOT cause a false
//       positive when the conversion is semantically sound).
//   §F  Struct fields carrying lifetime annotations.
//   §G  Constrained method return types.
//   §H  Multi-constraint requires clauses.
//   §I  Constraint on function parameters only (not return type).

// ============================================================================
// §A  Reflexivity — a lifetime outlives itself, no constraint needed.
// ============================================================================

// A.1: Return T^@a from a function that receives T^@a — trivially valid.
template<lifetime @a>
int^@a identity_ref(int^@a x) {
  return x; // @a : @a (reflexive), so T^@a → T^@a is fine.
}

// A.2: Assign T^@a to another T^@a local — trivially valid.
template<lifetime @a>
void assign_same(int^@a x) {
  int^@a y = x; // same lifetime, reflexive coercion.
  (void)y;
}

// A.3: Store T^@a into a local T^@a slot — trivially valid.
template<lifetime @a>
void forward(int^@a x) {
  int^@a copy = x; // same lifetime, reflexive coercion.
  (void)copy;
}

// ============================================================================
// §B  @static is the top lifetime — it outlives every declared lifetime.
// ============================================================================

// B.1: A globally-lived int can produce a T^ with no explicit lifetime.
int g_val = 42;
int^ ref_global = &g_val; // @static storage, shared tracked ref.

// B.2: A static variable's address is a T^ with implicit @static lifetime.
void use_static_addr() {
  static int s = 99;
  int^ ref_static = &s; // @static storage → shared tracked ref.
  (void)ref_static;
}

// B.3: Constraint declares @long : @a; @static satisfies @long by convention.
template<lifetime @long, lifetime @a>
requires @long : @a
class OutlivesHolder {
  int^@long big;
  int^@a    small;
};

// ============================================================================
// §C  Explicit single-edge outlives constraints.
// ============================================================================

// C.1: shorten — canonical example from the spec.
//      @a : @b  ⟹  T^@a may be returned as T^@b.
template<lifetime @a, lifetime @b>
requires @a : @b
int^@b shorten(int^@a x) {
  return x; // legal shrinking coercion.
}

// C.2: Struct holding both lifetimes; construction is valid when @a : @b.
template<lifetime @a, lifetime @b>
requires @a : @b
struct Pair {
  int^@a longer_ref;
  int^@b shorter_ref;
};

// C.3: Method that shortens via the class-level constraint.
template<lifetime @a, lifetime @b>
requires @a : @b
struct Shortener {
  int^@a src;
  int^@b shorten() const { return src; } // @a : @b established by class requires.
};

// C.4: Function taking two args and returning the shortening.
template<lifetime @a, lifetime @b>
requires @a : @b
int^@b pick_shorter(int^@a x, int^@b y, bool which) {
  if (which) return x; // T^@a → T^@b: valid because @a : @b.
  return y;            // T^@b → T^@b: reflexive.
}

// ============================================================================
// §D  Transitivity chains.
// ============================================================================

// D.1: Two-edge chain: @a : @b and @b : @c  ⟹  @a : @c is derivable.
//      Function returns T^@c using T^@a — two shrinking steps.
template<lifetime @a, lifetime @b, lifetime @c>
requires @a : @b && @b : @c
int^@c chain_shorten(int^@a x) {
  return x; // @a : @b : @c — transitive, legal.
}

// D.2: Named struct using three chained lifetimes.
template<lifetime @a, lifetime @b, lifetime @c>
requires @a : @b && @b : @c
struct ThreeLevel {
  int^@a top;
  int^@b mid;
  int^@c bot;
};

// D.3: Three-edge chain in a single requires clause conjunction.
template<lifetime @x, lifetime @y, lifetime @z>
requires @x : @y && @x : @z  // both @y and @z are outlived by @x.
int^@y use_either(int^@x r, bool which) {
  if (which) return r; // T^@x → T^@y: @x : @y
  int^@y local_y = r;
  return local_y;
}

// ============================================================================
// §E  Absence of false positives — conversions that stay within the
//     declared lifetime are always safe with no extra constraint.
// ============================================================================

// E.1: Function that never changes the lifetime — no constraint needed.
template<lifetime @a>
void use_ref(int^@a x) {
  int val = *x;
  (void)val;
}

// E.2: Struct that stores a single lifetime parameter — no outlives needed.
template<lifetime @a>
struct Box {
  int^@a item;
};

// E.3: Two references of the SAME lifetime — no outlives constraint needed.
template<lifetime @a>
void swap_refs(int^@a& lhs, int^@a& rhs) {
  // Both have the same lifetime; no subtyping required.
  (void)lhs; (void)rhs;
}

// ============================================================================
// §F  Struct fields with lifetime annotations.
// ============================================================================

// F.1: A view struct that borrows from an outer scope — @a : @view.
template<lifetime @a, lifetime @view>
requires @a : @view
struct Slice {
  int^@a   ptr;  // actual backing storage lifetime
  int^@view pub; // lifetime exposed to callers (can be shorter)
  unsigned  len;
};

// F.2: Nested struct composition carrying the constraint outward.
template<lifetime @outer, lifetime @inner>
requires @outer : @inner
struct Outer {
  int^@outer storage;

  struct Inner {
    int^@inner portion;
  };
};

// ============================================================================
// §G  Constrained method return types.
// ============================================================================

// G.1: A container that lends references at a shorter lifetime.
template<lifetime @storage, lifetime @borrow>
requires @storage : @borrow
struct Container {
  int^@storage data;

  // Callers see the reference as T^@borrow (safe shortened view).
  int^@borrow get() const { return data; }

  // Callers can also alias the borrow at the shorter lifetime.
  int^@borrow get_borrow() const { return data; }
};

// ============================================================================
// §H  Multi-constraint requires clauses.
// ============================================================================

// H.1: Three lifetimes, two constraints.
template<lifetime @a, lifetime @b, lifetime @c>
requires @a : @b && @b : @c
class Transition {
  int^@a first;
  int^@b second;
  int^@c third;
};

// H.2: Function with four lifetimes and a diamond constraint.
template<lifetime @top, lifetime @left, lifetime @right, lifetime @bot>
requires @top : @left && @top : @right && @left : @bot && @right : @bot
int^@bot diamond(int^@top t) {
  return t; // @top outlives everything down to @bot.
}

// ============================================================================
// §I  Constraint on parameters only — no impact on unconstrained return.
// ============================================================================

// I.1: Constraint ensures the argument is valid; result is unrelated lifetime.
template<lifetime @caller, lifetime @callee>
requires @caller : @callee
void register_callback(int^@callee func_arg, int^@caller caller_data) {
  // @caller outlives @callee, so the callback can safely refer to caller_data
  // for as long as func_arg is alive.
  (void)func_arg; (void)caller_data;
}

// I.2: Requires clause as documentation / precondition for the body.
template<lifetime @a, lifetime @b>
requires @a : @b
void assert_outlives_unused(int^@a x, int^@b y) {
  // Constraint declared but both parameters used independently.
  (void)*x; (void)*y;
}
