// RUN: %clang_cc1 -fsyntax-only -verify -std=c++20 %s

// Without -ftracked-references, 'safe' and 'unsafe' should NOT be keywords.
// They should be treated as normal identifiers.

// expected-no-diagnostics
int safe = 42;
int unsafe = 99;

void foo(int safe, int unsafe) {
  int x = safe + unsafe;
}
