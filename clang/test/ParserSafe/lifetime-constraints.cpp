// RUN: %clang_cc1 -fsyntax-only -ftracked-references -std=c++20 %s -verify
// expected-no-diagnostics

// Test lifetime constraint expressions in requires clauses
// Syntax: requires @a : @b  (meaning @a outlives @b)

template<lifetime @a, lifetime @b>
requires @a : @b
class OutlivesBidirectional {};

template<lifetime @long, lifetime @short>
class Container {
  // This should work when composed
};

template<lifetime @a, lifetime @b>
requires @a : @b
void outlives_func() {}

// Multiple constraints combined with &&
// Combined lifetime constraints with logical operators are not supported yet.
