// RUN: %clang_cc1 -fsyntax-only -ftracked-references -std=c++20 -verify %s
//
// Tests for parsing lifetime template parameters (§5 of the design spec).
// Verifies that 'template<lifetime @name>' is parsed correctly under
// Mizar mode and rejected otherwise. Also tests @identifier lexing.

// ==========================================================================
// Test 1: Basic lifetime parameter parsing — single parameter.
// ==========================================================================

template<lifetime @a>                   // expected-no-diagnostics for this line
class StringView {
  const char *_ptr;
  unsigned _len;
};

// ==========================================================================
// Test 2: Multiple lifetime parameters.
// ==========================================================================

template<lifetime @a, lifetime @b>
class JoinView {
  int x;
};

// ==========================================================================
// Test 3: Lifetime parameter mixed with type parameter.
// ==========================================================================

template<typename T, lifetime @a>
class Span {
  T *_ptr;
  unsigned _len;
};

// ==========================================================================
// Test 4: Multiple type params + multiple lifetimes.
// ==========================================================================

template<typename T, typename U, lifetime @a, lifetime @b>
class MultiParam {
  T *_t;
  U *_u;
};

// ==========================================================================
// Test 5: Lifetime parameter before type parameter.
// ==========================================================================

template<lifetime @a, typename T>
class LifetimeFirst {
  T *_ptr;
};

// ==========================================================================
// Test 6: Error — missing @name after 'lifetime'.
// ==========================================================================

template<lifetime>      // expected-error {{expected lifetime name (e.g., '@a') after 'lifetime'}}
class BadLifetime1 {};

// ==========================================================================
// Test 7: Error — 'lifetime' with plain identifier (no @ prefix).
// ==========================================================================

template<lifetime a>    // expected-error {{expected lifetime name (e.g., '@a') after 'lifetime'}}
class BadLifetime2 {};
