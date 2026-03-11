// RUN: %clang_cc1 -fsyntax-only -ftracked-references -std=c++20 %s -verify
// XFAIL: *
//
// Mizar Lifetime Outlives — Violation Test Suite (Enforcement XFAIL)
// ==================================================================
//
// STATUS: XFAIL until conversion-time outlives enforcement is implemented.
//
// This file specifies the FULL set of diagnostic behaviours that the Mizar
// lifetime checker MUST produce once enforcement lands.  The expected-error
// annotations are intentional forward-declarations of the implementation
// contract: when enforcement is added, remove the XFAIL directive above and ALL
// tests in this file must produce exactly the listed diagnostics.
//
// Design intent of each test group:
//
//   §V1  Widening coercion on return — the foundational violation.
//   §V2  Widening coercion on assignment.
//   §V3  Widening coercion on function call argument.
//   §V4  @static cannot be widened to, only from.
//   §V5  Transitivity does NOT extend in the wrong direction.
//   §V6  Unconstrained pairs — no proof means rejection.
//   §V7  Diamond and multi-path violations.
//   §V8  Struct field assignment violations.
//   §V9  Conditional-expression lifetime unification.
//
// Notation used in expected-error messages (provisional; implementor may
// adjust wording but the semantic meaning must be preserved):
//
//   "cannot convert tracked reference from lifetime '@X' to '@Y':
//    '@X' does not outlive '@Y'"
//
// The note pointing to the conflicting constraint declaration is also required:
//
//   "note: lifetime constraint '@A : @B' declared here"
//
// ============================================================================
// §V1  Widening on RETURN — the canonical violation.
//
//   If only @a : @b is declared, we know @a is at least as long as @b.
//   Returning T^@a from a body that holds T^@b is a widening — the caller
//   sees a reference that claims to be valid for @a, but backing storage is
//   only guaranteed for @b.
// ============================================================================

// V1.1: Basic widen — return T^@a from T^@b body.
template<lifetime @a, lifetime @b>
requires @a : @b
int^@a widen_basic(int^@b x) {
  return x; // expected-error {{cannot convert tracked reference from lifetime '@b' to '@a': '@b' does not outlive '@a'}}
}

// V1.2: Widen in a conditional return.
template<lifetime @a, lifetime @b>
requires @a : @b
int^@a widen_cond(int^@a good, int^@b bad, bool which) {
  if (which)
    return good;          // OK: @a → @a
  return bad;             // expected-error {{cannot convert tracked reference from lifetime '@b' to '@a': '@b' does not outlive '@a'}}
}

// V1.3: Two-step attempted widen via local alias.
template<lifetime @a, lifetime @b>
requires @a : @b
int^@a widen_via_alias(int^@b x) {
  int^@a y = x; // expected-error {{cannot convert tracked reference from lifetime '@b' to '@a': '@b' does not outlive '@a'}}
  return y;
}

// V1.4: Widen through an explicit cast (casts must not bypass outlives).
template<lifetime @a, lifetime @b>
requires @a : @b
int^@a widen_cast(int^@b x) {
  return (int^@a)x; // expected-error {{cannot convert tracked reference from lifetime '@b' to '@a': '@b' does not outlive '@a'}}
}

// ============================================================================
// §V2  Widening on ASSIGNMENT.
//
//   After declaration, assigning a shorter-lived reference into a slot
//   demanding a longer lifetime is forbidden for the same reason as returning.
// ============================================================================

// V2.1: Direct assignment.
template<lifetime @a, lifetime @b>
requires @a : @b
void assign_widen(int^@a dest, int^@b src) {
  dest = src; // expected-error {{cannot convert tracked reference from lifetime '@b' to '@a': '@b' does not outlive '@a'}}
}

// V2.2: Assignment inside a loop (destination lives longer than loop-body
//        source — but the constraint is on the template params, not scopes).
template<lifetime @a, lifetime @b>
requires @a : @b
void assign_loop(int^@a slot, int^@b val, int n) {
  for (int i = 0; i < n; ++i)
    slot = val; // expected-error {{cannot convert tracked reference from lifetime '@b' to '@a': '@b' does not outlive '@a'}}
}

// V2.3: Aggregate / struct initialisation — widening into a field.
template<lifetime @a, lifetime @b>
requires @a : @b
struct WideningField {
  int^@a field;
  void assign_from_short(int^@b src) {
    field = src; // expected-error {{cannot convert tracked reference from lifetime '@b' to '@a': '@b' does not outlive '@a'}}
  }
};

// ============================================================================
// §V3  Widening on ARGUMENT PASSING.
//
//   Passing T^@short where T^@long is expected — the callee would hold a
//   reference it believes outlives @long, but backing storage only covers @short.
// ============================================================================

// V3.1: Direct call with widening argument.
template<lifetime @a>
void needs_long(int^@a x);

template<lifetime @a, lifetime @b>
requires @a : @b
void call_with_short(int^@b x) {
  needs_long<@a>(x); // expected-error {{cannot convert tracked reference from lifetime '@b' to '@a': '@b' does not outlive '@a'}}
  //                 // expected-note {{in instantiation of function template}}
}

// V3.2: Forwarding to a function that stores the argument in a longer slot.
template<lifetime @a, lifetime @b>
requires @a : @b
void forward_wide(int^@b short_ref) {
  int^@a wide; // expected-note {{declared with lifetime '@a' here}}
  wide = short_ref; // expected-error {{cannot convert tracked reference from lifetime '@b' to '@a': '@b' does not outlive '@a'}}
}

// V3.3: Widening through an intermediate function that accepts the argument.
template<lifetime @long>
void long_consumer(int^@long x);

template<lifetime @long, lifetime @short>
requires @long : @short
void pass_short_as_long(int^@short x) {
  long_consumer<@long>(x); // expected-error {{cannot convert tracked reference from lifetime '@short' to '@long': '@short' does not outlive '@long'}}
}

// ============================================================================
// §V4  @static cannot be widened to from a non-static lifetime.
//
//   @static is the top of the lifetime order.  No declared lifetime @x is
//   guaranteed to outlive @static unless explicitly axiomatized.  Assigning
//   a T^@x into a T^ (implicit @static slot) is a widening violation.
// ============================================================================

// V4.1: Storing a local borrow in a @static slot.
template<lifetime @local>
void store_local_in_static(int^@local x) {
  int^/* @static */ y = x; // expected-error {{cannot convert tracked reference from lifetime '@local' to '@static': '@local' does not outlive '@static'}}
}

// V4.2: Even with a bogus @local : @static "claim", the checker must reject.
//       (This constraint is vacuously satisfiable only if @local is @static.)
template<lifetime @local>
requires @local : @static // user-declared (would be wrong at instantiation)
void bogus_claim_local_is_static(int^@local x) {
  // With enforcement: the requires clause itself would be verified against the
  // actual lifetime argument at the call site.
  (void)x;
}

// V4.3: Return T^ from a function that holds T^@a (where @a may be local).
template<lifetime @a>
int^ elevate_to_static(int^@a x) {
  return x; // expected-error {{cannot convert tracked reference from lifetime '@a' to '@static': '@a' does not outlive '@static'}}
}

// ============================================================================
// §V5  Transitivity does NOT extend in the WRONG direction.
//
//   Knowing @a : @b and @b : @c gives @a : @c, but it does NOT give @c : @a,
//   @c : @b, or @b : @a.  Attempts to use these reversed edges must fail.
// ============================================================================

// V5.1: Chain @a : @b : @c; attempt to widen @c → @b.
template<lifetime @a, lifetime @b, lifetime @c>
requires @a : @b && @b : @c
int^@b widen_via_chain(int^@c x) {
  return x; // expected-error {{cannot convert tracked reference from lifetime '@c' to '@b': '@c' does not outlive '@b'}}
}

// V5.2: Chain @a : @b : @c; attempt to widen @c → @a (two steps reversed).
template<lifetime @a, lifetime @b, lifetime @c>
requires @a : @b && @b : @c
int^@a widen_two_steps(int^@c x) {
  return x; // expected-error {{cannot convert tracked reference from lifetime '@c' to '@a': '@c' does not outlive '@a'}}
}

// V5.3: Transitivity does NOT imply reverse via intermediate.
template<lifetime @a, lifetime @b, lifetime @c>
requires @a : @b && @b : @c
int^@b invalid_reverse_step(int^@c x) {
  int^@a out = x; // expected-error {{cannot convert tracked reference from lifetime '@c' to '@a': '@c' does not outlive '@a'}}
  return out;     // @a → @b would be OK if we got here; but the line above fails first.
}

// ============================================================================
// §V6  Unconstrained pairs — no proof, no conversion.
//
//   Two independent lifetime parameters with no declared relationship are
//   incomparable.  ANY directional coercion between them must be rejected.
// ============================================================================

// V6.1: No constraint declared — T^@a → T^@b rejected.
template<lifetime @a, lifetime @b>
int^@b unconstrained_forward(int^@a x) {
  return x; // expected-error {{cannot convert tracked reference from lifetime '@a' to '@b': no outlives relationship is established between '@a' and '@b'}}
}

// V6.2: No constraint declared — T^@b → T^@a also rejected.
template<lifetime @a, lifetime @b>
int^@a unconstrained_reverse(int^@b x) {
  return x; // expected-error {{cannot convert tracked reference from lifetime '@b' to '@a': no outlives relationship is established between '@b' and '@a'}}
}

// V6.3: Three unconstrained lifetimes — each pair is independent.
template<lifetime @x, lifetime @y, lifetime @z>
int^@z unconstrained_three(int^@x a, int^@y b) {
  if (1) return a; // expected-error {{cannot convert tracked reference from lifetime '@x' to '@z': no outlives relationship is established between '@x' and '@z'}}
  return b;        // expected-error {{cannot convert tracked reference from lifetime '@y' to '@z': no outlives relationship is established between '@y' and '@z'}}
}

// ============================================================================
// §V7  Diamond constraint violations.
//
//   @top : @left, @top : @right, @left : @bot, @right : @bot.
//   Valid: @top → any, @left → @bot, @right → @bot.
//   Invalid: @bot → anything above it.
// ============================================================================

template<lifetime @top, lifetime @left, lifetime @right, lifetime @bot>
requires @top : @left && @top : @right && @left : @bot && @right : @bot
int^@left diamond_bot_to_left(int^@bot x) {
  return x; // expected-error {{cannot convert tracked reference from lifetime '@bot' to '@left': '@bot' does not outlive '@left'}}
}

template<lifetime @top, lifetime @left, lifetime @right, lifetime @bot>
requires @top : @left && @top : @right && @left : @bot && @right : @bot
int^@top diamond_right_to_top(int^@right x) {
  return x; // expected-error {{cannot convert tracked reference from lifetime '@right' to '@top': '@right' does not outlive '@top'}}
}

// ============================================================================
// §V8  Struct field assignment violations.
//
//   When a struct carries lifetime-annotated fields, attempts to assign
//   shorter-lived references into longer-lifetime field slots must fail.
// ============================================================================

// V8.1: Field with @long lifetime; attempt to store @short reference.
template<lifetime @long, lifetime @short>
requires @long : @short
struct FieldHolder {
  int^@long field;

  void store_short(int^@short s) {
    field = s; // expected-error {{cannot convert tracked reference from lifetime '@short' to '@long': '@short' does not outlive '@long'}}
  }
};

// V8.2: Brace-initialise the field with the wrong lifetime.
template<lifetime @a, lifetime @b>
requires @a : @b
struct Initialised {
  int^@a slot;
};

template<lifetime @a, lifetime @b>
requires @a : @b
void brace_widen(int^@b x) {
  Initialised<@a, @b> obj{x}; // expected-error {{cannot convert tracked reference from lifetime '@b' to '@a': '@b' does not outlive '@a'}}
}

// ============================================================================
// §V9  Conditional-expression lifetime unification.
//
//   A ternary whose branches return references of incompatible lifetimes must
//   be rejected because the result type cannot be safely widened.
// ============================================================================

// V9.1: Branches return different tracked-ref lifetimes; result must be the
//        shorter — attempting to use the ternary at the wider lifetime fails.
template<lifetime @long, lifetime @short>
requires @long : @short
int^@long ternary_widen(int^@long a, int^@short b, bool w) {
  return w ? a : b; // expected-error {{cannot convert tracked reference from lifetime '@short' to '@long': '@short' does not outlive '@long'}}
}

// V9.2: Nested ternary still propagates the violation.
template<lifetime @a, lifetime @b>
requires @a : @b
int^@a nested_ternary_widen(int^@a x, int^@b y, bool c1, bool c2) {
  return c1 ? x : (c2 ? x : y); // expected-error {{cannot convert tracked reference from lifetime '@b' to '@a': '@b' does not outlive '@a'}}
}
