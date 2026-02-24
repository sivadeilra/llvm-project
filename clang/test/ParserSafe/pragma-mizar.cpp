// RUN: %clang_cc1 -fsyntax-only -verify -ftracked-references -std=c++20 %s
// Test #pragma mizar on/off keyword toggling.
// With -ftracked-references, Mizar keywords start enabled. We can turn them
// off and back on using the pragma.

// Keywords are on by default with -ftracked-references.
void takes_shared(int ^ p) {}
void takes_exclusive(int ^ mut p) {}

#pragma mizar off

// After #pragma mizar off, 'mut' is an identifier again.
// You can use it as a variable name.
int mut = 42;

#pragma mizar on

// After #pragma mizar on, 'mut' is a keyword again.
int ^ mut return_exclusive(int ^ mut p) { return p; }

// expected-no-diagnostics
