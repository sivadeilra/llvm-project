// RUN: %clang_cc1 -fsyntax-only -verify -std=c++20 %s
// Test #pragma mizar error diagnostics.

#pragma mizar pop // expected-error {{'#pragma mizar pop' without matching '#pragma mizar push'}}

#pragma mizar badtoken // expected-error {{expected 'on', 'off', 'push', or 'pop' after '#pragma mizar'}}
