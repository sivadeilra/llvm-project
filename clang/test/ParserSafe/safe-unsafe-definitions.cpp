// RUN: %clang_cc1 -ast-dump -ftracked-references -std=c++20 %s | FileCheck %s

// Test that safe/unsafe work with function definitions (not just declarations).

// CHECK: FunctionDecl {{.*}} safe_func 'void () safe'
// CHECK: CompoundStmt
void safe_func() safe {
}

// CHECK: FunctionDecl {{.*}} unsafe_func 'void () unsafe'
// CHECK: CompoundStmt
void unsafe_func() unsafe {
}

// Test safe/unsafe combined with noexcept.
// CHECK: FunctionDecl {{.*}} safe_noexcept 'void () noexcept safe'
void safe_noexcept() noexcept safe;

// CHECK: FunctionDecl {{.*}} unsafe_noexcept 'void () noexcept unsafe'
void unsafe_noexcept() noexcept unsafe;
