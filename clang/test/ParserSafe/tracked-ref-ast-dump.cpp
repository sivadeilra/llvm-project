// RUN: %clang_cc1 -ftracked-references -std=c++20 -ast-dump %s | FileCheck %s

// Check AST dump output for tracked reference types.

// CHECK: FunctionDecl {{.*}} f_shared 'void (int ^)'
// CHECK: ParmVarDecl {{.*}} p 'int ^'
void f_shared(int ^p) {}

// CHECK: FunctionDecl {{.*}} f_exclusive 'void (int ^ mut)'
// CHECK: ParmVarDecl {{.*}} p 'int ^ mut'
void f_exclusive(int ^ mut p) {}

// CHECK: FunctionDecl {{.*}} f_return_shared 'int ^(int ^)'
// CHECK: ParmVarDecl {{.*}} x 'int ^'
int ^ f_return_shared(int ^x) { return x; }

// CHECK: FunctionDecl {{.*}} f_return_exclusive 'int ^ mut(int ^ mut)'
// CHECK: ParmVarDecl {{.*}} x 'int ^ mut'
int ^ mut f_return_exclusive(int ^ mut x) { return x; }

// CHECK: FunctionDecl {{.*}} f_multi 'void (int ^, int ^ mut, double ^)'
// CHECK: ParmVarDecl {{.*}} a 'int ^'
// CHECK: ParmVarDecl {{.*}} b 'int ^ mut'
// CHECK: ParmVarDecl {{.*}} c 'double ^'
void f_multi(int ^a, int ^ mut b, double ^c) {}

// Check that struct types work with tracked references.
struct S { int x; };

// CHECK: FunctionDecl {{.*}} f_struct 'void (S ^)'
// CHECK: ParmVarDecl {{.*}} s 'S ^'
void f_struct(S ^s) {}

// CHECK: FunctionDecl {{.*}} f_struct_mut 'void (S ^ mut)'
// CHECK: ParmVarDecl {{.*}} s 'S ^ mut'
void f_struct_mut(S ^ mut s) {}

// Check const-qualified pointee.
// CHECK: FunctionDecl {{.*}} f_const 'void (const int ^)'
// CHECK: ParmVarDecl {{.*}} p 'const int ^'
void f_const(const int ^p) {}
