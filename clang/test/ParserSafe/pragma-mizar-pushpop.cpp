// RUN: %clang_cc1 -fsyntax-only -verify -ftracked-references -std=c++20 %s
// Test #pragma mizar push/pop for state preservation.

// Keywords on by default (via -ftracked-references).
void f1(int ^ mut p) {} // OK: 'mut' is a keyword

#pragma mizar push
#pragma mizar off

// Inside push/off scope: keywords are off.
int mut = 42; // OK: 'mut' is an identifier now

#pragma mizar pop

// After pop, previous state (on) is restored.
void f2(int ^ mut p) {} // OK: 'mut' is a keyword again

// Test push/pop with nested states.
#pragma mizar push
#pragma mizar off

int mut2 = 10; // OK: identifier

#pragma mizar push
#pragma mizar on

void f3(int ^ mut p) {} // OK: keyword again

#pragma mizar pop

// Restored to off
int mut3 = 20; // OK: identifier

#pragma mizar pop

// Restored to on
void f4(int ^ mut p) {} // OK: keyword

// expected-no-diagnostics
