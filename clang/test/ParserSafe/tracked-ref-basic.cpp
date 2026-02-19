// RUN: %clang_cc1 -fsyntax-only -verify -ftracked-references -std=c++20 %s
// expected-no-diagnostics
// Test basic parsing of tracked reference syntax (T^ and T^ mut).

// ===== Function parameters =====
void shared_param(int ^p) {}
void exclusive_param(int ^ mut p) {}
void multi_params(int ^a, int ^ mut b, double ^c) {}

// ===== Return types =====
int ^ return_shared(int ^p) { return p; }
int ^ mut return_exclusive(int ^ mut p) { return p; }

// ===== Struct types =====
struct S { int x; };
void struct_shared(S ^p) {}
void struct_exclusive(S ^ mut p) {}
S ^ return_struct_shared(S ^p) { return p; }
S ^ mut return_struct_exclusive(S ^ mut p) { return p; }

// ===== Nested pointer/reference types =====
void ptr_to_tracked(int ^*pp) {}
void ref_to_tracked(int ^&r) {}

// ===== Const-qualified pointee =====
void const_shared(const int ^p) {}

// ===== Void tracked reference =====
void void_shared(void ^p) {}

// ===== Lifetime annotations (parsed but not yet semantically used) =====
void lifetime_shared(int ^@a p) {}
void lifetime_exclusive(int ^@life mut p) {}

// ===== Multiple function parameters with lifetimes =====
void multi_lifetime(int ^@a p, int ^@b q) {}
