// RUN: %clang_cc1 -fsyntax-only -ftracked-references -std=c++20 -verify %s
//
// Tests for demand-driven safety inference: when a safe context calls an
// unspecified (unannotated) function, the compiler walks its body to
// determine if it is effectively safe.

// ==========================================================================
// Helper declarations
// ==========================================================================

void explicitly_unsafe() unsafe;

// A harmless function — no unsafe ops, no unsafe calls.
void clean_helper() {
  int x = 42;
  x += 1;
}

// ==========================================================================
// Test 1: Pointer dereference makes a function inferred-unsafe.
// ==========================================================================

void does_ptr_deref(int *p) {
  *p = 42; // expected-note {{'does_ptr_deref' dereferences a raw pointer here}}
}

void test_inferred_ptr_deref() safe {
  int x = 0;
  does_ptr_deref(&x);      // expected-error {{call to function 'does_ptr_deref' is not known to be safe}}
}

// ==========================================================================
// Test 2: reinterpret_cast makes a function inferred-unsafe.
// ==========================================================================

void does_reinterpret(int *p) {
  auto n = reinterpret_cast<unsigned long long>(p); // expected-note {{'does_reinterpret' uses reinterpret_cast here}}
  (void)n;
}

void test_inferred_reinterpret() safe {
  int x = 0;
  does_reinterpret(&x);    // expected-error {{call to function 'does_reinterpret' is not known to be safe}}
}

// ==========================================================================
// Test 3: Inline assembly makes a function inferred-unsafe.
// ==========================================================================

void does_asm() {
  __asm__ __volatile__("nop"); // expected-note {{'does_asm' uses inline assembly here}}
}

void test_inferred_asm() safe {
  does_asm();               // expected-error {{call to function 'does_asm' is not known to be safe}}
}

// ==========================================================================
// Test 4: Calling an explicitly-unsafe function makes a function
//         inferred-unsafe (propagation through calls).
// ==========================================================================

void calls_unsafe() {
  explicitly_unsafe(); // expected-note {{'calls_unsafe' calls unsafe function 'explicitly_unsafe'}}
}

void test_inferred_calls_unsafe() safe {
  calls_unsafe();           // expected-error {{call to function 'calls_unsafe' is not known to be safe}}
}

// ==========================================================================
// Test 5: A clean function is inferred safe — no diagnostic.
// ==========================================================================

void test_clean_call() safe {
  clean_helper();           // OK: clean_helper has no unsafe operations.
}

// ==========================================================================
// Test 6: unsafe { } block suppresses inference diagnostic.
// ==========================================================================

void test_unsafe_block_suppresses() safe {
  int x = 0;
  unsafe {
    does_ptr_deref(&x);    // OK: inside unsafe block.
  }
}

// ==========================================================================
// Test 7: unsafe(expr) suppresses inference diagnostic.
// ==========================================================================

int deref_and_return(int *p) {
  return *p; // expected-note {{'deref_and_return' dereferences a raw pointer here}}
}

void test_unsafe_expr_suppresses() safe {
  int x = 42;
  int y = unsafe(deref_and_return(&x));  // OK: unsafe(expr) wraps the call.
  int z = deref_and_return(&x);          // expected-error {{call to function 'deref_and_return' is not known to be safe}}
}

// ==========================================================================
// Test 8: Transitive inference — unspecified calls unspecified with deref.
// ==========================================================================

void inner_deref(int *p) {
  *p = 10;
}

void middle(int *p) {
  inner_deref(p); // expected-note {{'middle' calls potentially unsafe function 'inner_deref'}}
}

void test_transitive() safe {
  int x = 0;
  middle(&x);               // expected-error {{call to function 'middle' is not known to be safe}}
}

// ==========================================================================
// Test 9: Functions without bodies (extern) — treated as Unknown,
//         which is permissive in v1. No diagnostic.
// ==========================================================================

void extern_fn(int *p);     // No body visible.

void test_extern_call() safe {
  int x = 0;
  extern_fn(&x);            // OK for v1: no body = Unknown = permissive.
}

// ==========================================================================
// Test 10: An unspecified function whose unsafe code is inside an
//          unsafe { } block is inferred-safe.
// ==========================================================================

void deref_in_unsafe_block(int *p) {
  unsafe {
    *p = 42;                // This is inside unsafe { }, so it doesn't count.
  }
}

void test_unsafe_block_doesnt_taint() safe {
  int x = 0;
  deref_in_unsafe_block(&x);  // OK: the deref is inside unsafe { }.
}

// ==========================================================================
// Test 11: From an unspecified context, no inference is triggered.
// ==========================================================================

void test_from_unspecified() {
  int x = 0;
  does_ptr_deref(&x);      // OK: not in a safe context, no inference.
  does_reinterpret(&x);    // OK
  does_asm();               // OK
  calls_unsafe();           // OK
}

// ==========================================================================
// Test 12: From an unsafe context, no inference is triggered.
// ==========================================================================

void test_from_unsafe() unsafe {
  int x = 0;
  does_ptr_deref(&x);      // OK: unsafe context.
  does_reinterpret(&x);    // OK
  does_asm();               // OK
  calls_unsafe();           // OK
}
