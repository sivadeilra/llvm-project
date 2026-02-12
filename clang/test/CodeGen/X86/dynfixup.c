// REQUIRES: x86-registered-target
//
// Verifies codegen for "dynamic value" relocations, also known as DVRT symbols.
// These are global variables marked with __declspec(dynfixup), which maps to
// an IR string attribute "msvc_dynfixup".
//
// RUN: %clang_cl -c --target=x86_64-windows-msvc -O2 /clang:-S /clang:-o- -- %s | FileCheck %s

#define DECLSPEC_DYNFIXUP __declspec(dynfixup)

DECLSPEC_DYNFIXUP extern void* __pte_base;

// Tests the simplest case
void* check_dynfixup_simple() {
    return &__pte_base;
}
// CHECK: .def check_dynfixup_simple
// CHECK: movabs rax, offset __pte_base@DYNFIXUP
// CHECK-NEXT: ret

// Test that the MOV64 is not merged with addressing modes used by GEP.
int check_dynfixup_indexed(int x) {
    void* pte_base = &__pte_base;
    return ((int*)pte_base)[x];
}

// CHECK: .def check_dynfixup_indexed
// CHECK: movabs rcx, offset __pte_base@DYNFIXUP
// CHECK: ret

extern void* g_foo;

// Test 'select' folding. 'select' and 'cmp' operations should not fold with
// dynfixup variables.
int check_select_folding() {
    if (&__pte_base == g_foo) {
        return 100;
    } else {
        return 200;
    }
}

// CHECK: .def check_select_folding
// CHECK: movabs rax, offset __pte_base@DYNFIXUP
// CHECK: ret
