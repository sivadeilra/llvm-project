// RUN: %clang_cc1 -fsyntax-only -verify -ftracked-references -std=c++20 %s

// ===== Rule 2: Address-of static storage → T^ @static (shared only) =====

// --- 2.2: Qualifying expressions ---

// Namespace-scope variable → shared tracked ref
int g_value = 10;
int^ ref_global = &g_value; // OK: static storage, shared

// Static local variable → shared tracked ref
void test_static_local() {
    static int s = 20;
    int^ ref_static = &s; // OK: static storage, shared
}

// Static class data member → shared tracked ref
struct MyClass {
    static int s_count;
};
int MyClass::s_count = 0;
int^ ref_class_static = &MyClass::s_count; // OK: static storage, shared

// constexpr variable → shared tracked ref
constexpr int k_value = 42;
const int^ ref_constexpr = &k_value; // OK: static storage, shared, const

// Subobject field of global struct → shared tracked ref
struct Point { int x; int y; };
Point g_point = { 1, 2 };
int^ ref_field = &g_point.x; // OK: field of static storage object

// Element of global array → shared tracked ref
int g_array[256] = {};
int^ ref_element = &g_array[0]; // OK: element of static storage array

// Nested subobject → shared tracked ref
struct Inner { int val; };
struct Outer { Inner inner; };
Outer g_outer = {};
int^ ref_nested = &g_outer.inner.val; // OK: any depth of subobject access

// --- 2.3: Shared only — exclusive from static is forbidden ---
int g_mut = 42;
int^ mut bad_exclusive_global = &g_mut; // expected-error {{cannot initialize a variable of type 'int ^ mut' with an rvalue of type 'int *'}}

// --- 2.4: Const correctness ---
const int g_const = 100;
const int^ ref_const_ok = &g_const;     // OK: const int^ from const int
int^ ref_drop_const = &g_const;         // expected-error {{cannot initialize a variable of type 'int ^' with an rvalue of type 'const int *'}}

// Non-const global to const tracked ref is OK (adding const)
int g_nonconst = 50;
const int^ ref_add_const = &g_nonconst; // OK: adding const is fine


// ===== Rule 3: Address-of automatic storage → T^ with local lifetime =====

void test_automatic_shared() {
    int x = 42;
    int^ p = &x;         // OK: shared borrow of local
}

void test_automatic_exclusive() {
    int x = 42;
    int^ mut q = &x;     // OK: exclusive borrow of local
}

void test_automatic_const() {
    const int x = 42;
    const int^ p = &x;   // OK: const tracked ref from const local
    int^ bad = &x;        // expected-error {{cannot initialize a variable of type 'int ^' with an rvalue of type 'const int *'}}
}

// ===== Rule 6: Explicitly forbidden conversions =====

// nullptr → T^ is forbidden
int^ ref_null = nullptr; // expected-error {{cannot initialize a variable of type 'int ^' with an rvalue of type 'std::nullptr_t'}}

// Raw pointer variable → T^ is forbidden
void test_raw_pointer() {
    int x = 42;
    int* raw = &x;
    int^ bad = raw; // expected-error {{cannot initialize a variable of type 'int ^' with an lvalue of type 'int *'}}
}

// Passing through a pointer variable (not directly &expr) → forbidden
void test_pointer_variable() {
    int x = 42;
    int* p = &x;
    int^ bad = p; // expected-error {{cannot initialize a variable of type 'int ^' with an lvalue of type 'int *'}}
}

// ===== Multiple function parameters =====
void takes_shared(int^ p);
void takes_exclusive(int^ mut p);

void test_function_args() {
    int x = 42;
    takes_shared(&x);     // OK
    takes_exclusive(&x);  // OK: exclusive borrow of local
    
    static int s = 10;
    takes_shared(&s);     // OK: static → shared
}

// ===== Return type conversions =====
int^ return_global_ref() {
    return &g_value;      // OK: static storage → shared
}

int^ return_static_local() {
    static int s = 99;
    return &s;            // OK: static storage → shared
}
