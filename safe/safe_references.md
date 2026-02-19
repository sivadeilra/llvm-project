# Mizar: Safe References for C++

## 1. Introduction

Mizar introduces **safe references** to C++: a new reference type with the same
semantics as Rust's references. Safe references are borrow-checked at compile
time and guarantee the absence of dangling pointers, use-after-free, double-free,
and data races caused by aliased mutation.

Safe references coexist with C++'s existing pointer and reference types. They
do not replace `T*` or `T&`; they provide a new, checkable alternative.

## 2. Safe Reference Types

A safe reference is written with the postfix `^` operator on a type:

```cpp
T^          // shared safe reference   (equivalent to Rust's &T)
T^ mut      // exclusive safe reference (equivalent to Rust's &mut T)
```

### 2.1 Properties

| Property              | `T*`  | `T&`  | `T^`  | `T^ mut` |
|----------------------|-------|-------|-------|----------|
| Nullable             | Yes   | No    | No    | No       |
| Always initialized   | No    | Yes   | Yes   | Yes      |
| Reseatable           | Yes   | No    | Yes   | Yes      |
| Implicit deref       | No    | Yes   | No    | No       |
| Borrow-checked       | No    | No    | Yes   | Yes      |
| Lifetime-tracked     | No    | No    | Yes   | Yes      |
| Permits mutation     | Yes   | Yes*  | No    | Yes      |

\* `const T&` does not permit mutation; `T&` does.

### 2.2 Shared References (`T^`)

A shared safe reference permits read-only access to the referent. Any number of
shared references to the same value may coexist simultaneously.

```cpp
int x = 42;
int^ a = ^x;
int^ b = ^x;    // OK: multiple shared borrows
int^ c = a;     // OK: shared references are Copy

int v = *a;     // OK: read through dereference
// *a = 10;     // ERROR: cannot mutate through shared reference
```

### 2.3 Exclusive References (`T^ mut`)

An exclusive safe reference permits read and write access to the referent. At
most one exclusive reference to a value may exist at a time, and no shared
references may coexist with it.

```cpp
int x = 42;
int^ mut e = ^mut x;
*e = 100;        // OK: mutation through exclusive reference

// int^ s = ^x;  // ERROR: cannot borrow x as shared while exclusive borrow exists
```

### 2.4 Reseatability

Unlike C++ references (`T&`), safe references can be reseated. Assignment
operates on the reference itself, not the referent:

```cpp
int a = 1, b = 2;
int^ p = ^a;
p = ^b;          // p now refers to b (reseating)
int v = *p;      // v == 2
```

To assign through the reference to the referent, dereference explicitly:

```cpp
int x = 1;
int^ mut p = ^mut x;
*p = 99;         // x is now 99
```

### 2.5 `const` on Safe References

`const` on the reference itself controls whether the reference can be reseated:

```cpp
int^ const p = ^x;        // shared, cannot be reseated
int^ mut const q = ^mut x; // exclusive, cannot be reseated
```

`const` on the referent type is redundant for shared references (they already
forbid mutation), but is meaningful on exclusive references:

```cpp
const int^ mut r = ^mut x; // exclusive access (no aliasing), but mutation forbidden
```

## 3. Creating Safe References (Borrowing)

The unary prefix `^` operator creates a safe reference from an lvalue:

```cpp
int x = 42;

int^ s = ^x;          // shared borrow
int^ mut e = ^mut x;  // exclusive borrow
```

The `^` prefix operator is new syntax. It does not conflict with the binary XOR
operator, which requires a left-hand operand.

### 3.1 Borrowing Rules

These rules are enforced at compile time by the borrow checker:

1. **Multiple shared borrows are permitted.** Any number of `T^` references to
   the same value may coexist.

2. **Exclusive borrows are exclusive.** While a `T^ mut` to a value exists, no
   other borrows (shared or exclusive) of that value are permitted.

3. **Borrows must not outlive the referent.** A safe reference cannot be used
   after the value it refers to has been destroyed.

```cpp
int^ dangling() {
    int local = 5;
    return ^local;    // ERROR: local does not live long enough
}
```

```cpp
void aliasing() {
    int x = 10;
    int^ mut w = ^mut x;
    int^ r = ^x;       // ERROR: cannot borrow x as shared while
                        //        exclusive borrow w exists
    *w = 20;
}
```

```cpp
void multiple_shared() {
    int x = 10;
    int^ a = ^x;
    int^ b = ^x;
    int^ c = ^x;
    int sum = *a + *b + *c;  // OK: all shared
}
```

## 4. Dereferencing and Member Access

### 4.1 Explicit Dereference

The unary `*` operator dereferences a safe reference:

```cpp
int x = 42;
int^ p = ^x;
int v = *p;    // v == 42
```

### 4.2 Member Access

Both `.` and `->` auto-dereference through safe references. They are equivalent:

```cpp
struct Point { int x; int y; };

Point pt{1, 2};
Point^ ref = ^pt;

int a = ref.x;     // auto-dereference, reads pt.x
int b = ref->x;    // same as above
```

This matches Rust's behavior where `.` auto-dereferences.

## 5. Copy and Move Semantics

### 5.1 Shared References Are Copy

Shared references (`T^`) are trivially copyable. Assigning one shared reference
to another copies it; both remain valid:

```cpp
int x = 10;
int^ a = ^x;
int^ b = a;      // copy: both a and b are valid
```

### 5.2 Exclusive References Are Move-Only

Exclusive references (`T^ mut`) are move-only. Assigning one to another
transfers the borrow; the source is invalidated:

```cpp
int x = 10;
int^ mut a = ^mut x;
int^ mut b = a;   // move: a is invalidated
// *a = 20;       // ERROR: use of moved reference a
*b = 20;          // OK
```

### 5.3 `std::move` Is Not Permitted

`std::move` must not be used with safe references. The borrow checker manages
reference lifetimes directly. Applying `std::move` to a `T^` or `T^ mut` is
a compile-time error:

```cpp
int^ p = ^x;
auto q = std::move(p);  // ERROR: std::move cannot be applied to safe references
```

## 6. Reborrowing

Reborrowing follows Rust's model exactly. An exclusive reference can be
implicitly reborrowed at a call site:

### 6.1 Exclusive to Shared Reborrow

```cpp
void read(int^ r);

void example() {
    int x = 10;
    int^ mut e = ^mut x;
    read(e);       // implicit reborrow: e is temporarily borrowed as int^
                   // e is frozen during the call, usable again after
    *e = 20;       // OK: reborrow has ended
}
```

### 6.2 Exclusive to Exclusive Reborrow

```cpp
void write(int^ mut w);

void example() {
    int x = 10;
    int^ mut e = ^mut x;
    write(e);      // implicit reborrow: shorter-lived exclusive borrow
    *e = 20;       // OK: reborrow has ended
}
```

### 6.3 Reborrowing Prevents Invalidation

During a reborrow, the original reference is frozen (cannot be used). The
original becomes usable again once the reborrow's lifetime ends:

```cpp
void example() {
    int x = 10;
    int^ mut outer = ^mut x;

    {
        int^ inner = outer;    // reborrow: outer is frozen
        int v = *inner;        // OK
        // *outer = 5;         // ERROR: outer is frozen during reborrow
    }
    // inner's reborrow has ended
    *outer = 5;                // OK: outer is unfrozen
}
```

## 7. Implicit Conversions

### 7.1 Exclusive to Shared (Permitted)

An exclusive reference can be implicitly converted to a shared reference. This
is always safe because it only reduces capability:

```cpp
int x = 10;
int^ mut e = ^mut x;
int^ s = e;        // OK: downgrade from exclusive to shared
```

### 7.2 Shared to Exclusive (Forbidden)

A shared reference cannot be converted to an exclusive reference:

```cpp
int^ s = ^x;
// int^ mut e = s;  // ERROR: cannot upgrade shared to exclusive
```

### 7.3 Safe to Raw (Unsafe Only)

Converting a safe reference to a raw pointer requires an `unsafe` block:

```cpp
int^ p = ^x;
unsafe {
    int* raw = p.as_ptr();
}
```

### 7.4 Raw to Safe (Unsafe Only)

Converting a raw pointer to a safe reference requires an `unsafe` block. The
programmer asserts that the pointer is valid and properly aligned:

```cpp
int* raw = &x;
unsafe {
    int^ safe = int^::from_ptr(raw);
}
```

## 8. Lifetime Annotations

Every safe reference has an associated **lifetime**: a compile-time construct
that tracks how long the referent is valid. Lifetimes are used by the borrow
checker to verify that no reference outlives its referent.

### 8.1 Named Lifetimes

Lifetime parameters are declared in template parameter lists using the
`lifetime` keyword and the `@name` syntax:

```cpp
template<lifetime @a>
int^@a max_ref(int^@a x, int^@a y) {
    if (*x > *y) return x;
    return y;
}
```

This declares a lifetime parameter `@a` and constrains both inputs and the
output to the same lifetime. The borrow checker ensures the returned reference
does not outlive either input.

Rust equivalent:
```rust
fn max_ref<'a>(x: &'a i32, y: &'a i32) -> &'a i32 {
    if *x > *y { x } else { y }
}
```

### 8.2 Multiple Lifetimes

```cpp
template<lifetime @a, lifetime @b>
int^@a pick_first(int^@a x, int^@b y) {
    return x;   // return type is tied to @a, not @b
}
```

### 8.3 Lifetime Bounds (Outlives Relationships)

Use `requires` clauses to express outlives relationships:

```cpp
// @a outlives @b (Rust: 'a: 'b)
template<lifetime @a, lifetime @b>
    requires (@a >= @b)
int^@b shorten(int^@a x) {
    return x;   // OK: @a lives at least as long as @b
}
```

The `>=` operator on lifetimes reads as "outlives or equals."

### 8.4 The `@static` Lifetime

`@static` denotes data that lives for the entire program duration:

```cpp
int^@static get_global() {
    static int g = 42;
    return ^g;   // OK: static storage duration satisfies @static
}
```

### 8.5 Lifetime Elision

When lifetimes are not explicitly annotated, the compiler applies **elision
rules** identical to Rust's:

**Rule 1:** Each safe reference parameter with an elided lifetime gets a
distinct lifetime parameter.

**Rule 2:** If there is exactly one input lifetime position, that lifetime is
assigned to all output lifetime positions.

**Rule 3:** If the function is a method with a safe reference to `self`, the
lifetime of `self` is assigned to all output lifetime positions.

```cpp
// Elided: one input reference → Rule 2 applies
int^ first(std::span<int>^ v);
// Desugars to:
template<lifetime @a>
int^@a first(std::span<int>^@a v);
```

```cpp
// Elided: two input references, no output → Rule 1 applies
void process(int^ x, float^ y);
// Desugars to:
template<lifetime @a, lifetime @b>
void process(int^@a x, float^@b y);
```

```cpp
// Elided: two input references with output → ambiguous, ERROR
int^ bad(int^ x, int^ y);
// ERROR: cannot determine output lifetime; annotate explicitly
```

### 8.6 Anonymous Lifetimes (`@_`)

Use `@_` when a lifetime must be present syntactically but you do not need to
name or constrain it:

```cpp
template<lifetime @a>
int^@a focused(int^@a important, float^@_ irrelevant);
```

## 9. Structs with Lifetime Parameters

Structs that contain safe references must declare lifetime parameters:

```cpp
template<lifetime @a>
struct StringView {
    const char^@a data;
    size_t len;
};
```

Rust equivalent:
```rust
struct StringView<'a> {
    data: &'a [u8],
    len: usize,
}
```

Usage:

```cpp
void example() {
    char buf[] = "hello";
    StringView view{ ^buf[0], 5 };
    // view borrows buf; it cannot outlive buf
}
```

## 10. Methods with Explicit `this` Parameter

Methods declare their receiver borrow using C++23 explicit object parameters:

```cpp
struct Counter {
    int count = 0;

    // Shared borrow of self (Rust: &self)
    int get(this Counter^ self) {
        return self.count;
    }

    // Exclusive borrow of self (Rust: &mut self)
    void increment(this Counter^ mut self) {
        self.count += 1;
    }

    // Returning a reference tied to self's lifetime
    template<lifetime @a>
    int^@a count_ref(this Counter^@a self) {
        return ^self.count;
    }
};
```

Usage:

```cpp
void use_counter() {
    Counter c;
    c.increment();          // exclusive borrow of c
    int v = c.get();        // shared borrow of c
    int^ r = c.count_ref(); // shared borrow, lifetime tied to c
}
```

## 11. Interaction with Templates

Safe references compose with generic type parameters:

```cpp
template<typename T, lifetime @a>
T^@a min_ref(T^@a x, T^@a y) {
    if (*x < *y) return x;
    return y;
}
```

```cpp
template<typename T, lifetime @a>
struct Ref {
    T^@a inner;
};
```

## 12. The `mutable` Keyword

The `mutable` storage class specifier is **not permitted** on fields whose type
is or contains a safe reference. The borrow checker assumes that shared
references provide immutable access; `mutable` would violate this invariant.

```cpp
struct Bad {
    mutable int^ ref;   // ERROR: mutable not permitted on safe reference fields
};
```

Interior mutability patterns (e.g., `Cell<T>`, `RefCell<T>`, `Mutex<T>`) will
be provided as library types with explicit `unsafe` implementations, matching
Rust's model.

## 13. Iterator Invalidation (Prevented)

Safe references prevent iterator invalidation at compile time:

```cpp
void prevented() {
    std::vector<int> v = {1, 2, 3};
    int^ first = ^v[0];    // shared borrow of v's contents
    // v.push_back(4);     // ERROR: cannot mutate v while shared borrow exists
    int val = *first;      // OK
}
```

## 14. Summary of New Syntax

| Syntax              | Meaning                                     |
|---------------------|---------------------------------------------|
| `T^`                | Shared safe reference to `T`                |
| `T^ mut`            | Exclusive safe reference to `T`             |
| `^x`                | Create shared borrow of `x`                 |
| `^mut x`            | Create exclusive borrow of `x`              |
| `*p`                | Dereference safe reference `p`              |
| `template<lifetime @a>` | Declare lifetime parameter `@a`        |
| `T^@a`              | Shared reference with lifetime `@a`         |
| `T^@a mut`          | Exclusive reference with lifetime `@a`      |
| `@static`           | Lifetime of the entire program              |
| `@_`                | Anonymous (don't-care) lifetime             |
| `requires (@a >= @b)` | Lifetime `@a` outlives lifetime `@b`     |

## 15. Keywords

| Keyword    | Status     | Meaning                          |
|-----------|------------|----------------------------------|
| `mut`     | New        | Marks a safe reference as exclusive |
| `lifetime`| New        | Declares a lifetime parameter    |
| `unsafe`  | New        | Enters an unsafe context         |
