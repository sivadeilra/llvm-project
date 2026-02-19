// RUN: %clang_cc1 -ftracked-references -std=c++20 -fsyntax-only -verify %s
// Test diagnostics for tracked reference type mismatches.

// Shared and exclusive tracked references are distinct types.
int ^ shared_from_exclusive(int ^ mut p) {
  return p; // expected-error {{cannot initialize return object of type 'int ^' with an lvalue of type 'int ^ mut'}}
}
