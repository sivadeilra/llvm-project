// RUN: %clang_cc1 -std=c++20 -fsyntax-only -verify %s
// Without -ftracked-references, ^ is treated as block pointer syntax
// which requires -fblocks and a function type pointee.

void f(int ^p) {} // expected-error {{blocks support disabled}} expected-error {{block pointer to non-function type is invalid}}
