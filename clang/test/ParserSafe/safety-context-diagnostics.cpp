// RUN: %clang_cc1 -fsyntax-only -ftracked-references -std=c++20 -verify %s

// --- Declarations ---

void safe_fn() safe;
void unsafe_fn() unsafe;    // expected-note 4 {{'unsafe_fn' declared unsafe here}}
void unspecified_fn();

// --- Test 1: Calling unsafe from safe function body → error ---

void caller_safe() safe {
  safe_fn();                 // OK: safe calling safe
  unspecified_fn();          // OK: unspecified is allowed (for now)
  unsafe_fn();               // expected-error {{call to unsafe function 'unsafe_fn' requires an unsafe context}}
}

// --- Test 2: Calling unsafe from unsafe function body → OK ---

void caller_unsafe() unsafe {
  safe_fn();                 // OK
  unspecified_fn();          // OK
  unsafe_fn();               // OK: unsafe context permits unsafe calls
}

// --- Test 3: Calling unsafe from unspecified function body → OK ---

void caller_unspecified() {
  safe_fn();                 // OK
  unspecified_fn();          // OK
  unsafe_fn();               // OK: unspecified is permissive
}

// --- Test 4: safe { } block establishes safe context ---

void test_safe_block() {
  unsafe_fn();               // OK: unspecified context
  safe {
    safe_fn();               // OK
    unsafe_fn();             // expected-error {{call to unsafe function 'unsafe_fn' requires an unsafe context}}
  }
  unsafe_fn();               // OK: back to unspecified
}

// --- Test 5: unsafe { } block establishes unsafe context ---

void test_unsafe_block() safe {
  unsafe_fn();               // expected-error {{call to unsafe function 'unsafe_fn' requires an unsafe context}}
  unsafe {
    unsafe_fn();             // OK: unsafe block permits it
  }
}

// --- Test 6: Nested blocks ---

void test_nested_blocks() safe {
  // safe context from function annotation
  safe {
    // still safe
    unsafe {
      // now unsafe
      unsafe_fn();           // OK
      safe {
        // back to safe
        unsafe_fn();         // expected-error {{call to unsafe function 'unsafe_fn' requires an unsafe context}}
      }
      unsafe_fn();           // OK: back to unsafe
    }
  }
}

// --- Test 7: unsafe(expr) creates unsafe context for its operand ---

int returns_int() unsafe;    // expected-note {{'returns_int' declared unsafe here}}

void test_unsafe_expr() safe {
  int x = unsafe(returns_int()); // OK: unsafe(expr) wraps the call
  int y = returns_int();         // expected-error {{call to unsafe function 'returns_int' requires an unsafe context}}
}
