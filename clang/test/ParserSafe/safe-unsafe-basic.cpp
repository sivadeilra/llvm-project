// RUN: %clang_cc1 -ast-dump -ftracked-references -std=c++20 %s | FileCheck %s

// Test that 'safe' and 'unsafe' are recognized as postfix function annotations
// and appear in the AST dump on the FunctionProtoType.

// CHECK: FunctionDecl {{.*}} foo 'void () safe'
void foo() safe;

// CHECK: FunctionDecl {{.*}} bar 'void () unsafe'
void bar() unsafe;

// CHECK: FunctionDecl {{.*}} baz 'int (int) safe'
int baz(int x) safe;

// CHECK: FunctionDecl {{.*}} qux 'int (int, int) unsafe'
int qux(int x, int y) unsafe;
