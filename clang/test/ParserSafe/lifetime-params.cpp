// RUN: %clang_cc1 -fsyntax-only -ftracked-references -std=c++20 -verify %s
//
// Tests for parsing lifetime template parameters (§5 of the design spec).
// Verifies that 'template<lifetime @name>' is parsed correctly under
// Mizar mode and rejected otherwise. Also tests @identifier lexing.

// ==========================================================================
// Test 1: Basic lifetime parameter parsing — single parameter.
// ==========================================================================

template<lifetime @a>                   // OK — no diagnostics expected
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

template<lifetime>      // expected-error {{expected lifetime name (e.g., '@a') after 'lifetime'}} expected-error {{extraneous 'template<>'}}
class BadLifetime1 {};

// ==========================================================================
// Test 7: Error — 'lifetime' with plain identifier (no @ prefix).
// ==========================================================================

template<lifetime a>    // expected-error {{expected lifetime name (e.g., '@a') after 'lifetime'}} expected-error {{extraneous 'template<>'}}
class BadLifetime2 {};

// ==========================================================================
// Test 8: T^@a — lifetime annotation resolves to enclosing LifetimeParmDecl.
// ==========================================================================

template<lifetime @a>
class RefHolder {
  int^@a _ref;           // OK — @a resolves to the lifetime parameter
};

// ==========================================================================
// Test 9: T^@a mut — exclusive tracked reference with lifetime.
// ==========================================================================

template<lifetime @a>
class MutRefHolder {
  int^@a mut _ref;       // OK — @a resolves, exclusive reference
};

// ==========================================================================
// Test 10: Multiple lifetime params used in tracked references.
// ==========================================================================

template<lifetime @a, lifetime @b>
class TwoRefs {
  int^@a _first;         // OK — @a
  int^@b _second;        // OK — @b
};

// ==========================================================================
// Test 11: Lifetime param mixed with type param, used in T^@a.
// ==========================================================================

template<typename T, lifetime @a>
class TypedRef {
  T^@a _ref;             // OK — @a resolves, T is the pointee
};

// ==========================================================================
// Test 12: Error — undeclared lifetime in T^@name.
// ==========================================================================

template<lifetime @a>
class BadRef1 {
  int^@b _ref;           // expected-error {{use of undeclared lifetime '@b'}}
};

// ==========================================================================
// Test 13: Error — name exists but is not a lifetime parameter.
// ==========================================================================

template<typename T, lifetime @a>  // expected-note {{declared here}}
class BadRef2 {
  int^@T _ref;           // expected-error {{'T' does not name a lifetime parameter}}
};

// ==========================================================================
// Test 14: Error — undeclared lifetime with no template at all.
// ==========================================================================

class NoTemplate {
  int^@x _ref;           // expected-error {{use of undeclared lifetime '@x'}}
};

// ==========================================================================
// Test 15: Lifetime used in function parameter type.
// ==========================================================================

template<lifetime @a>
void take_ref(int^@a ref) {}   // OK — @a resolves in function param
