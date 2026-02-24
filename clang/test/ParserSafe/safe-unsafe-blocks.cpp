// RUN: %clang_cc1 -ast-dump -ftracked-references -std=c++20 %s | FileCheck %s

// Test that 'safe { }' and 'unsafe { }' block statements are parsed and
// appear in the AST dump as MizarSafetyStmt nodes.

void test_unsafe_block() {
  // CHECK: FunctionDecl {{.*}} test_unsafe_block
  // CHECK: MizarSafetyStmt {{.*}} unsafe
  // CHECK-NEXT: CompoundStmt
  unsafe {
    int x = 42;
  }
}

void test_safe_block() {
  // CHECK: FunctionDecl {{.*}} test_safe_block
  // CHECK: MizarSafetyStmt {{.*}} safe
  // CHECK-NEXT: CompoundStmt
  safe {
    int y = 10;
  }
}

void test_nested_blocks() {
  // CHECK: FunctionDecl {{.*}} test_nested_blocks
  // CHECK: MizarSafetyStmt {{.*}} safe
  // CHECK: MizarSafetyStmt {{.*}} unsafe
  // CHECK: MizarSafetyStmt {{.*}} safe
  safe {
    int a = 1;
    unsafe {
      int b = 2;
      safe {
        int c = 3;
      }
    }
  }
}

void test_block_with_statements() {
  // CHECK: FunctionDecl {{.*}} test_block_with_statements
  // CHECK: MizarSafetyStmt {{.*}} unsafe
  int before = 0;
  unsafe {
    int inside = 1;
    before = inside;
  }
  int after = 2;
}
