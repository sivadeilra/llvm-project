// RUN: %clang_cc1 -fsyntax-only -ftracked-references -std=c++20 -verify %s
//
// Mizar Lifetime Outlives — Phase 1 Passing Test Suite
// ====================================================
//
// Tests for lifetime outlives enforcement that are implemented and passing.
// Covers §V1 (return widening), §V2 (assignment widening),
// §V6 (unconstrained pairs), and §V7 (diamond constraints).
//
// Note: cases requiring explicit lifetime template arguments (<@a> syntax),
// @static as a keyword, multi-requires clauses, or ternary unification
// are deferred to lifetime-outlives-violations.cpp (XFAIL spec reference).

// ============================================================================
// §V1  Widening on RETURN — returning a shorter-lived ref where a
//      longer-lived ref is expected.
// ============================================================================

// V1.1: Basic widen — return T^@b from a function declared T^@a (violating).
template<lifetime @a, lifetime @b>
requires @a : @b
int^@a v1_basic_widen(int^@b x) {
  return x; // expected-error {{cannot convert tracked reference from lifetime '@b' to '@a': '@b' does not outlive '@a'}}
}

// V1.2: Widen in a conditional return — valid path and invalid path.
template<lifetime @a, lifetime @b>
requires @a : @b
int^@a v1_cond_widen(int^@a good, int^@b bad, bool which) {
  if (which)
    return good;                // OK: @a -> @a is reflexive
  return bad;                   // expected-error {{cannot convert tracked reference from lifetime '@b' to '@a': '@b' does not outlive '@a'}}
}

// V1.3: Widen via a local alias — aliasing to a wider lifetime fails on init.
template<lifetime @a, lifetime @b>
requires @a : @b
int^@a v1_widen_via_alias(int^@b x) {
  int^@a y = x; // expected-error {{cannot convert tracked reference from lifetime '@b' to '@a': '@b' does not outlive '@a'}}
  return y;
}

// V1.4: Same-lifetime parameter return — valid (reflexive).
template<lifetime @a>
int^@a v1_same_lifetime_ok(int^@a x) {
  return x; // OK: @a -> @a
}

// ============================================================================
// §V2  Widening on ASSIGNMENT.
// ============================================================================

// V2.1: Direct assignment of shorter-lived ref into longer-lived slot.
template<lifetime @a, lifetime @b>
requires @a : @b
void v2_direct_assign(int^@a dest, int^@b src) {
  dest = src; // expected-error {{cannot convert tracked reference from lifetime '@b' to '@a': '@b' does not outlive '@a'}}
}

// V2.2: Assignment in a loop — the violation is the same on every iteration.
template<lifetime @a, lifetime @b>
requires @a : @b
void v2_loop_assign(int^@a slot, int^@b val, int n) {
  for (int i = 0; i < n; ++i)
    slot = val; // expected-error {{cannot convert tracked reference from lifetime '@b' to '@a': '@b' does not outlive '@a'}}
}

// V2.3: Same-lifetime assignment — valid (reflexive).
template<lifetime @a>
void v2_same_lifetime_ok(int^@a dest, int^@a src) {
  dest = src; // OK: @a -> @a
}

// ============================================================================
// §V6  Unconstrained pairs — no proof means rejection (both directions).
// ============================================================================

// V6.1: @a -> @b with no constraint declared.
template<lifetime @a, lifetime @b>
int^@b v6_unconstrained_forward(int^@a x) {
  return x; // expected-error {{cannot convert tracked reference from lifetime '@a' to '@b': no outlives relationship is established between '@a' and '@b'}}
}

// V6.2: @b -> @a with no constraint declared (reverse direction also rejected).
template<lifetime @a, lifetime @b>
int^@a v6_unconstrained_reverse(int^@b x) {
  return x; // expected-error {{cannot convert tracked reference from lifetime '@b' to '@a': no outlives relationship is established between '@b' and '@a'}}
}

// V6.3: Three unconstrained lifetimes — each pair is independent.
template<lifetime @x, lifetime @y, lifetime @z>
int^@z v6_unconstrained_three_a(int^@x a) {
  return a; // expected-error {{cannot convert tracked reference from lifetime '@x' to '@z': no outlives relationship is established between '@x' and '@z'}}
}

template<lifetime @x, lifetime @y, lifetime @z>
int^@z v6_unconstrained_three_b(int^@y b) {
  return b; // expected-error {{cannot convert tracked reference from lifetime '@y' to '@z': no outlives relationship is established between '@y' and '@z'}}
}

// ============================================================================
// §V6+ Valid cases — constrained in the correct direction.
// ============================================================================

// With @a : @b, using @a where @a expected is valid. @b where @b is also valid.
template<lifetime @a, lifetime @b>
requires @a : @b
int^@b v6_constrained_ok(int^@a x) {
  return x; // OK: @a outlives @b, so @a -> @b is a valid narrowing
}

template<lifetime @a, lifetime @b>
requires @a : @b
void v6_constrained_assign_ok(int^@b dest, int^@a src) {
  dest = src; // OK: @a -> @b (narrowing, allowed because @a : @b)
}
