// RUN: %clang_cc1 -ast-dump -ftracked-references -std=c++20 %s | FileCheck %s

// Test that 'unsafe(expr)' expressions are parsed and appear in the AST dump
// as MizarUnsafeExpr nodes. The expression type should match the operand type.

int get_value() safe;

void test_unsafe_expr() {
  // CHECK: FunctionDecl {{.*}} test_unsafe_expr
  // CHECK: MizarUnsafeExpr {{.*}} 'int'
  // CHECK-NEXT: CallExpr
  int x = unsafe(get_value());
}

void test_unsafe_expr_arithmetic() {
  // CHECK: FunctionDecl {{.*}} test_unsafe_expr_arithmetic
  // CHECK: MizarUnsafeExpr {{.*}} 'int'
  int y = unsafe(1 + 2);
}

int* get_ptr() unsafe;

void test_unsafe_expr_ptr() {
  // CHECK: FunctionDecl {{.*}} test_unsafe_expr_ptr
  // CHECK: MizarUnsafeExpr {{.*}} 'int *'
  int *p = unsafe(get_ptr());
}

void test_unsafe_expr_nested() {
  // CHECK: FunctionDecl {{.*}} test_unsafe_expr_nested
  // CHECK: MizarUnsafeExpr
  // CHECK: MizarUnsafeExpr
  int z = unsafe(unsafe(get_value()));
}
