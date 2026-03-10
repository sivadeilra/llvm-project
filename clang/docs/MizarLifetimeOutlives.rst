Mizar Lifetime Outlives Semantics
=================================

.. contents::
   :local:

Overview
--------

This document defines the semantic contract for lifetime outlives constraints
in Mizar tracked references.

Syntax (current parser form):

.. code-block:: c++

  template<lifetime @a, lifetime @b>
  requires @a : @b
  int^@b shorten(int^@a x) {
    return x;
  }

Read ``@a : @b`` as: ``@a`` outlives ``@b``.

Conceptual Grounding
--------------------

Lifetimes form a partial order:

- Reflexive: every lifetime outlives itself.
- Transitive: if ``@a : @b`` and ``@b : @c``, then ``@a : @c``.
- Antisymmetry is not required in diagnostics; equivalent regions can be
  represented by mutual outlives constraints.

``@static`` is the top lifetime in the order and outlives every other
lifetime.

Traced-reference lifetime coercion should follow the outlives order:

- Shrinking is allowed: ``T^@long -> T^@short`` when ``@long : @short``.
- Widening is rejected: ``T^@short -> T^@long`` unless provable.

This rule is the core mechanism that prevents returning, storing, or passing
references that may outlive their backing storage.

Specification
-------------

Inputs
~~~~~~

For each constrained entity (function, method, class template), semantic
analysis consumes:

- Declared lifetime parameters.
- Explicit outlives constraints from ``requires`` clauses.
- Implicit axioms:
  - ``@x : @x`` for all declared lifetimes.
  - ``@static : @x`` for all declared lifetimes ``@x``.

Derived relation
~~~~~~~~~~~~~~~~

Build a closure graph over lifetime parameters:

- Node: lifetime parameter or ``@static``.
- Edge ``A -> B`` means ``A : B``.
- Compute transitive closure for fast entailment checks.

Entailment query:

- ``outlives(A, B)`` is true iff ``A == B`` or ``A`` reaches ``B``.

Enforcement points
~~~~~~~~~~~~~~~~~~

The checker must apply outlives entailment at all tracked-reference conversion
sites:

- Variable initialization.
- Assignment.
- Return statements.
- Argument passing.
- Conditional and aggregate expressions that unify tracked-reference types.

For conversion ``T^@src -> T^@dst``:

- Accept if ``outlives(@src, @dst)`` is true.
- Reject otherwise.

Diagnostics
~~~~~~~~~~~

Desired diagnostic shape for failed conversion:

- Primary error at conversion site:
  - cannot convert tracked reference from lifetime ``@src`` to ``@dst``:
    ``@src`` does not outlive ``@dst``
- Notes:
  - where ``@src`` and ``@dst`` were introduced.
  - relevant declared constraints (if any).

Current Project Status
----------------------

Implemented:

- Parsing of lifetime constraints in requires clauses (``@a : @b``).
- AST node for ``LifetimeConstraintExpr``.
- Name/kind validation that referenced identifiers are lifetime parameters.

Not yet implemented:

- Conversion-time outlives entailment across assignments/returns/calls.
- Rejection of widening conversions based on lifetime ordering.
- Diagnostics that explain unsatisfied outlives obligations.

Test Plan
---------

Tests live in ``clang/test/ParserSafe/``.

Positive tests — ``lifetime-outlives-valid.cpp``
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

All cases use ``// expected-no-diagnostics`` and must stay clean forever.

+------+-------------------------------------------+----------------------------------------------+
| §    | Category                                  | What it verifies                             |
+======+===========================================+==============================================+
| §A   | Reflexivity                               | ``@a → @a`` is always valid; no constraint   |
|      |                                           | needed.                                      |
+------+-------------------------------------------+----------------------------------------------+
| §B   | ``@static`` top-lifetime axiom            | ``@static : @x`` holds without declaration.  |
+------+-------------------------------------------+----------------------------------------------+
| §C   | Single-edge explicit constraint           | ``requires @a : @b`` enables ``T^@a → T^@b`` |
|      |                                           | (``shorten`` canonical example).             |
+------+-------------------------------------------+----------------------------------------------+
| §D   | Transitivity chains                       | ``@a : @b`` and ``@b : @c`` imply            |
|      |                                           | ``T^@a → T^@c``.                             |
+------+-------------------------------------------+----------------------------------------------+
| §E   | Absence of false positives                | Purely same-lifetime use compiles without    |
|      |                                           | any constraint.                              |
+------+-------------------------------------------+----------------------------------------------+
| §F   | Struct fields with lifetime annotations   | Fields at different lifetimes in one struct. |
+------+-------------------------------------------+----------------------------------------------+
| §G   | Constrained method return types           | ``get()`` returning shortened borrow.        |
+------+-------------------------------------------+----------------------------------------------+
| §H   | Multi-constraint ``requires`` clauses     | Diamond and transitivity structures.         |
+------+-------------------------------------------+----------------------------------------------+
| §I   | Constraint on parameters only             | No impact on the return lifetime.            |
+------+-------------------------------------------+----------------------------------------------+

Negative (enforcement) tests — ``lifetime-outlives-violations.cpp``
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

Status: ``XFAIL: *`` until conversion-time enforcement is implemented.
Each test carries ``// expected-error`` annotations that become the
implementation contract; remove the XFAIL line to activate them.

+------+-------------------------------------------+----------------------------------------------+
| §V   | Category                                  | Violation being specified                    |
+======+===========================================+==============================================+
| §V1  | Widening on return                        | ``T^@b → T^@a`` when only ``@a : @b``.      |
|      |                                           | Includes conditional return and cast forms.  |
+------+-------------------------------------------+----------------------------------------------+
| §V2  | Widening on assignment                    | Assigning ``T^@short`` into ``T^@long``.     |
+------+-------------------------------------------+----------------------------------------------+
| §V3  | Widening on argument passing              | Passing ``T^@short`` where ``T^@long``       |
|      |                                           | expected at call site.                       |
+------+-------------------------------------------+----------------------------------------------+
| §V4  | ``@static`` cannot be widened to          | ``T^@local → T^`` (implicit @static) fails   |
|      |                                           | unless ``@local : @static`` is proven.       |
+------+-------------------------------------------+----------------------------------------------+
| §V5  | Transitivity is one-directional           | ``@a : @b : @c`` does NOT give               |
|      |                                           | ``@c : @b`` or ``@c : @a``.                  |
+------+-------------------------------------------+----------------------------------------------+
| §V6  | Unconstrained pairs are incomparable      | Without any declared relationship, ALL       |
|      |                                           | cross-lifetime conversions must be rejected. |
+------+-------------------------------------------+----------------------------------------------+
| §V7  | Diamond constraint violations             | Only the valid edges of the diamond allow    |
|      |                                           | coercion; the rest must fail.                |
+------+-------------------------------------------+----------------------------------------------+
| §V8  | Struct field assignment violations        | Field with ``@long`` cannot accept           |
|      |                                           | ``@short`` value.                            |
+------+-------------------------------------------+----------------------------------------------+
| §V9  | Conditional-expression unification        | Ternary branches with different lifetimes    |
|      |                                           | cannot silently widen.                       |
+------+-------------------------------------------+----------------------------------------------+

Developer Examples
------------------

Legal shortening:

.. code-block:: c++

  template<lifetime @a, lifetime @b>
  requires @a : @b
  int^@b shorten(int^@a x) {
    return x;
  }

Illegal widening:

.. code-block:: c++

  template<lifetime @a, lifetime @b>
  requires @a : @b
  int^@a widen(int^@b x) {
    return x; // should be rejected
  }

Implementation Guidance
-----------------------

A practical first implementation can proceed in three steps:

1. Capture lifetime annotations in tracked-reference type metadata available
   at conversion checks.
2. Build per-declaration outlives closure from requires constraints.
3. Query closure in tracked-reference conversion logic and emit targeted
   diagnostics.

This keeps parsing and borrow-checking concerns decoupled while making
outlives ordering enforceable at type-conversion boundaries.
