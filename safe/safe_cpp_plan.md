We are going to collaborate on designing language extensions for C++
which will allow developers to write code can be verified to be safe
(meaning: verifiably free of undefined behavior). The extensions will
use many of the same semantics as defined in the Rust programming language.

The extensions will also guarantee interoperability between C++ code and Rust code.
Most but not all language features will be accessible across the boundary.

This set of extensions will be called Mizar. Mizar is the codename that
we can use to quickly refer to these extensions, their implementation,
and their design.

There are several aspects of Mizar:

1. Changes to Clang IR: These changes will allow programs to represent
   semantics of Mizar extensions in code. This will include information
   about scoped borrowing (with the same rules as Rust), exclusive vs
   shared borrowing, unnamed and named lifetime parameters, and whether
   a given function can be called from within safe or unsafe calling contexts.

2. Changes to Rust IR: These changes will allow Rust programs to interoperate
   with C++ code. These changes will represent concepts that are only found
   in C++ programs, and not in Rust programs. For example, subclassing,
   virtual methods, polymorphism, function overloading, method overloading,
   operator overloading, and immovable types.

3. The syntax for extending C++ programs. Most of this will consist of
   annotations that are applied to function declarations, method declarations,
   class declarations, and namespace declarations. This is the syntactic
   aspect which allows a developer to construct the modified Clang IR.

4. Rules for borrow-checking C++ code. This will be based heavily on the
   borrow-checker of Rust programs. In fact, we may simply convert the IR of
   C++ programs to the IR of Rust and run the Rust borrow checker.
