// RUN: %clang_cc1 -fsyntax-only -ftracked-references -std=c++20 -verify %s
// XFAIL: *
// expected-no-diagnostics
//
// Tests compositional lifetime propagation across record fields.
// These cases require lifetime template arguments (e.g., U<@a>), which are
// not yet implemented. This test will be enabled once that syntax is supported.

// ============================================================================
// Test 1: Simple composition using the same lifetime parameter.
// ============================================================================

template<lifetime @a>
struct Inner {
  int^@a p;
};

template<lifetime @a>
struct Outer {
  Inner<@a> inner;
};

// ============================================================================
// Test 2: Composition plus an additional field using the same lifetime.
// ============================================================================

template<lifetime @a>
struct Pair {
  Inner<@a> left;
  int^@a right;
};

// ============================================================================
// Test 3: Static lifetime composition.
// ============================================================================

template<lifetime @a>
struct StaticInner {
  int^@a p;
};

template<lifetime @a>
struct StaticOuter {
  StaticInner<@a> inner;
};
