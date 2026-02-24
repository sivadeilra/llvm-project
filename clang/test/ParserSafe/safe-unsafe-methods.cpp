// RUN: %clang_cc1 -ast-dump -ftracked-references -std=c++20 %s | FileCheck %s

// Test safe/unsafe on class methods.

struct Widget {
  // CHECK: CXXMethodDecl {{.*}} get 'int () safe'
  int get() safe;

  // CHECK: CXXMethodDecl {{.*}} set 'void (int) unsafe'
  void set(int v) unsafe;

  // Test safe with const qualifier.
  // CHECK: CXXMethodDecl {{.*}} read 'int () const safe'
  int read() const safe;

  // Test unsafe with const qualifier.
  // CHECK: CXXMethodDecl {{.*}} write 'void (int) const unsafe'
  void write(int v) const unsafe;
};
