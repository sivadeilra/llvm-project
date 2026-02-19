# Implementation Specification: Tracked References in Clang

This document specifies the changes required to implement tracked references
(`T^` and `T^ mut`) in the Clang frontend. It covers the lexer, parser, AST,
and Sema layers, plus the driver flag to enable the feature.

## 1. Feature Flag

### 1.1 LangOptions

Add a new language option in
[clang/include/clang/Basic/LangOptions.def](clang/include/clang/Basic/LangOptions.def):

```cpp
LANGOPT(TrackedReferences, 1, 0, NotCompatible, "tracked reference extensions")
```

This generates a `bool TrackedReferences` field in `LangOptions`.

### 1.2 Driver Flag

Add `-ftracked-references` / `-fno-tracked-references` in
[clang/include/clang/Driver/Options.td](clang/include/clang/Driver/Options.td).

Wire it to set `LangOpts.TrackedReferences = 1` in
[clang/lib/Frontend/CompilerInvocation.cpp](clang/lib/Frontend/CompilerInvocation.cpp).

The flag requires C++ mode (`LangOpts.CPlusPlus`). It is an error to use it
with C, Objective-C, or Objective-C++.

---

## 2. Lexer Changes

All lexer changes are in
[clang/lib/Lex/Lexer.cpp](clang/lib/Lex/Lexer.cpp) and
[clang/include/clang/Basic/TokenKinds.def](clang/include/clang/Basic/TokenKinds.def).

### 2.1 New Keyword: `mut`

Add in [TokenKinds.def](clang/include/clang/Basic/TokenKinds.def), in the
keywords section:

```cpp
KEYWORD(mut, KEYCXX)
```

This creates `tok::kw_mut`. Because `mut` is a full keyword, it will conflict
with any existing uses of `mut` as an identifier. This is accepted.

The keyword is only active when `LangOpts.CPlusPlus` is true (the `KEYCXX`
flag). In C mode, `mut` remains an ordinary identifier.

### 2.2 New Keyword: `lifetime`

Add in [TokenKinds.def](clang/include/clang/Basic/TokenKinds.def):

```cpp
KEYWORD(lifetime, KEYCXX)
```

This creates `tok::kw_lifetime`, used in template parameter lists to declare
lifetime parameters.

### 2.3 New Token: `tok::at_identifier` (Lifetime Name)

Add a new token kind in [TokenKinds.def](clang/include/clang/Basic/TokenKinds.def):

```cpp
TOK(at_identifier)   // @name — a lifetime name like @a, @static, @_
```

### 2.4 Lexer Modification: `@` Handling

Current behavior: `@` produces `tok::at` (Objective-C) or `tok::unknown`.

New behavior when `LangOpts.TrackedReferences` is enabled:

In the `case '@':` branch of `Lexer::LexTokenInternal`
([Lexer.cpp line 4425](clang/lib/Lex/Lexer.cpp#L4425)):

```
case '@':
    if (LangOpts.TrackedReferences && isIdentifierHead(peekNextChar())) {
        // Lex @identifier as a single tok::at_identifier token.
        // Consume '@', then continue consuming identifier characters.
        // The identifier text (without '@') is stored as the token's
        // IdentifierInfo.
        Kind = tok::at_identifier;
        // ... lex the identifier body ...
    } else if (LangOpts.ObjC) {
        Kind = tok::at;
    } else {
        Kind = tok::unknown;
    }
    break;
```

The `tok::at_identifier` token stores the identifier name (e.g., `a`, `static`,
`_`) in its `IdentifierInfo`. The `@` prefix is consumed but not stored — it
serves only as the lifetime sigil.

**Reserved lifetime names:**
- `@static` — the static lifetime (entire program duration)
- `@_` — anonymous / don't-care lifetime

These are not separate tokens. They are `tok::at_identifier` tokens whose
identifier text happens to be `static` or `_`. The parser recognizes them
by name.

### 2.5 No Changes to `^` Lexing

The `^` character continues to produce `tok::caret` and `^=` continues to
produce `tok::caretequal`. No lexer changes are needed for the caret itself.
Disambiguation between XOR and tracked-reference-borrow is handled entirely
in the parser (see §3).

---

## 3. Parser Changes

All parser changes are in
[clang/lib/Parse/ParseDecl.cpp](clang/lib/Parse/ParseDecl.cpp),
[clang/lib/Parse/ParseExpr.cpp](clang/lib/Parse/ParseExpr.cpp), and
[clang/lib/Parse/ParseTemplate.cpp](clang/lib/Parse/ParseTemplate.cpp).

### 3.1 Parsing `T^` in Declarators (Type Position)

Tracked references appear in the same syntactic positions as `T*`, `T&`, and
`T&&`. The parser must recognize `tok::caret` as a **ptr-operator** when
`LangOpts.TrackedReferences` is enabled.

#### 3.1.1 `isPtrOperatorToken`

In [ParseDecl.cpp line 6294](clang/lib/Parse/ParseDecl.cpp#L6294), modify
`isPtrOperatorToken`:

```cpp
static bool isPtrOperatorToken(tok::TokenKind Kind, const LangOptions &Lang,
                               DeclaratorContext TheContext) {
  if (Kind == tok::star || Kind == tok::caret)
    return true;
  // ... existing code ...
}
```

Currently `tok::caret` is already accepted here (for Blocks). When
`TrackedReferences` is enabled, the parser will still enter the caret branch.
The disambiguation between Blocks and tracked references is done in
`ParseDeclaratorInternal` (see §3.1.2).

#### 3.1.2 `ParseDeclaratorInternal`

In [ParseDecl.cpp line 6433](clang/lib/Parse/ParseDecl.cpp#L6433), in the
`Kind == tok::caret` branch:

```
if (Kind == tok::caret) {
    if (LangOpts.TrackedReferences) {
        // Parse tracked reference: T^ or T^ mut
        ConsumeToken();  // consume '^'

        // Check for optional lifetime annotation: ^@name
        LifetimeAnnotation lifetime;
        if (Tok.is(tok::at_identifier)) {
            lifetime = ParseLifetimeAnnotation();
        }

        // Check for 'mut' keyword
        bool IsMutable = false;
        if (Tok.is(tok::kw_mut)) {
            IsMutable = true;
            ConsumeToken();
        }

        D.AddTypeInfo(
            DeclaratorChunk::getTrackedReference(IsMutable, lifetime, Loc),
            std::move(DS.getAttributes()), EndLoc);
    } else {
        // Existing Blocks behavior
        D.AddTypeInfo(
            DeclaratorChunk::getBlockPointer(...), ...);
    }
}
```

#### 3.1.3 Parsing the Full Type Syntax

The parser recognizes these forms in type position:

| Source | Parsed as |
|--------|-----------|
| `int^` | Shared tracked reference, elided lifetime |
| `int^ mut` | Exclusive tracked reference, elided lifetime |
| `int^@a` | Shared tracked reference, lifetime `@a` |
| `int^@a mut` | Exclusive tracked reference, lifetime `@a` |
| `int^@static` | Shared tracked reference, static lifetime |
| `int^@_ mut` | Exclusive tracked reference, anonymous lifetime |
| `int^ const` | Non-reseatable shared tracked reference |
| `int^ mut const` | Non-reseatable exclusive tracked reference |

After `^`, the parser checks:
1. Is the next token `tok::at_identifier`? → consume as lifetime annotation.
2. Is the next token `tok::kw_mut`? → mark as exclusive.
3. Is the next token `tok::kw_const` / `tok::kw_volatile`? → parse cv-qualifiers
   on the reference itself (reseatability).

### 3.2 Parsing `^x` and `^mut x` in Expression Position

The unary prefix `^` creates a borrow. This new unary operator must be parsed
in expression context.

In [ParseExpr.cpp](clang/lib/Parse/ParseExpr.cpp), in the unary-expression
parsing (the function handling prefix operators), add handling for `tok::caret`
when `LangOpts.TrackedReferences` is enabled:

```
case tok::caret:
    if (LangOpts.TrackedReferences && isUnaryContext()) {
        ConsumeToken();  // consume '^'
        bool IsMutable = false;
        if (Tok.is(tok::kw_mut)) {
            IsMutable = true;
            ConsumeToken();
        }
        ExprResult Operand = ParseCastExpression(/*isUnaryExpression=*/true);
        return Actions.ActOnBorrowExpr(Loc, IsMutable, Operand.get());
    }
    break;
```

**Disambiguation from binary XOR:**

The unary `^` is distinguishable from binary XOR by the same rules C++ uses for
unary `*` vs. binary `*`, and unary `&` vs. binary `&`:

- If `^` appears at the start of an expression, after `(`, after `,`, after
  `=`, after `return`, or after any binary operator — it is **unary** (borrow).
- If `^` appears after an expression (identifier, literal, `)`, `]`) — it is
  **binary** (XOR).

Clang already handles this exact ambiguity for `*` and `&`. The `^` case
follows the same precedence dispatch in `ParseCastExpression`.

### 3.3 Parsing `template<lifetime @a>`

In [ParseTemplate.cpp line 467](clang/lib/Parse/ParseTemplate.cpp#L467),
modify `ParseTemplateParameter` to recognize lifetime parameters:

```
NamedDecl *Parser::ParseTemplateParameter(unsigned Depth, unsigned Position) {
    // NEW: Check for 'lifetime' keyword
    if (LangOpts.TrackedReferences && Tok.is(tok::kw_lifetime)) {
        return ParseLifetimeParameter(Depth, Position);
    }

    // ... existing dispatch (type, template-template, non-type) ...
}
```

#### 3.3.1 `ParseLifetimeParameter`

New function:

```
NamedDecl *Parser::ParseLifetimeParameter(unsigned Depth, unsigned Position) {
    ConsumeToken();  // consume 'lifetime'

    // Expect @name
    if (!Tok.is(tok::at_identifier)) {
        Diag(Tok, diag::err_expected_lifetime_name);
        return nullptr;
    }

    IdentifierInfo *Name = Tok.getIdentifierInfo();
    SourceLocation NameLoc = Tok.getLocation();
    ConsumeToken();  // consume @name

    return Actions.ActOnLifetimeParameter(Name, NameLoc, Depth, Position);
}
```

### 3.4 Parsing Lifetime Bounds in `requires` Clauses

In a `requires` clause, the expressions `(@a >= @b)` are parsed. When the
parser encounters `tok::at_identifier` inside a requires-clause expression, it
produces a `LifetimeRefExpr` AST node (see §4.5). The `>=` is the existing
`tok::greaterequal` operator, overloaded for lifetime operands.

---

## 4. AST Changes

### 4.1 New Type Node: `TrackedReferenceType`

Add in [clang/include/clang/Basic/TypeNodes.td](clang/include/clang/Basic/TypeNodes.td):

```tablegen
def TrackedReferenceType : TypeNode<Type>;
```

This generates a `TypeClass::TrackedReference` enum value.

Implement in [clang/include/clang/AST/Type.h](clang/include/clang/AST/Type.h):

```cpp
class TrackedReferenceType : public Type, public llvm::FoldingSetNode {
  friend class ASTContext;

  QualType PointeeType;
  bool Exclusive;                    // true = T^ mut, false = T^
  LifetimeParameterIndex Lifetime;   // index into the enclosing template's
                                     // lifetime parameters, or sentinel
                                     // values for @static, @_, elided

  TrackedReferenceType(QualType Pointee, bool Exclusive,
                       LifetimeParameterIndex Lifetime,
                       QualType CanonicalRef)
      : Type(TrackedReference, CanonicalRef, Pointee->getDependence()),
        PointeeType(Pointee), Exclusive(Exclusive), Lifetime(Lifetime) {}

public:
  QualType getPointeeType() const { return PointeeType; }
  bool isExclusive() const { return Exclusive; }
  bool isShared() const { return !Exclusive; }
  LifetimeParameterIndex getLifetime() const { return Lifetime; }

  bool isSugared() const { return false; }
  QualType desugar() const { return QualType(this, 0); }

  void Profile(llvm::FoldingSetNodeID &ID) {
    Profile(ID, getPointeeType(), isExclusive(), getLifetime());
  }
  static void Profile(llvm::FoldingSetNodeID &ID, QualType Pointee,
                       bool Exclusive, LifetimeParameterIndex LT) {
    ID.AddPointer(Pointee.getAsOpaquePtr());
    ID.AddBoolean(Exclusive);
    ID.AddInteger(LT.getValue());
  }

  static bool classof(const Type *T) {
    return T->getTypeClass() == TrackedReference;
  }
};
```

**Design decision: Why a new type node, not a qualifier?**

Tracked references are fundamentally a different kind of indirection, not a
qualifier on an existing type. They carry additional information (exclusive vs.
shared, lifetime index) that does not fit the qualifier model. This parallels
how `BlockPointerType` is a separate type node from `PointerType`, even though
both represent pointer-like indirection.

### 4.2 `DeclaratorChunk` Extension

Add a new `Kind` to the `DeclaratorChunk` enum in
[clang/include/clang/Sema/DeclSpec.h](clang/include/clang/Sema/DeclSpec.h):

```cpp
enum {
    Pointer, Reference, Array, Function, BlockPointer,
    MemberPointer, Paren, Pipe,
    TrackedReference    // NEW
} Kind;
```

Add a new inner struct:

```cpp
struct TrackedReferenceTypeInfo {
    unsigned IsMutable : 1;    // T^ mut
    LifetimeAnnotation Lifetime;
};
```

Add to the union:

```cpp
union {
    // ... existing members ...
    TrackedReferenceTypeInfo TRef;
};
```

Add a factory method:

```cpp
static DeclaratorChunk getTrackedReference(bool IsMutable,
                                           LifetimeAnnotation Lifetime,
                                           SourceLocation Loc) {
    DeclaratorChunk I;
    I.Kind = TrackedReference;
    I.Loc = Loc;
    I.TRef.IsMutable = IsMutable;
    I.TRef.Lifetime = Lifetime;
    return I;
}
```

### 4.3 `ASTContext` Creation Method

Add in [clang/include/clang/AST/ASTContext.h](clang/include/clang/AST/ASTContext.h):

```cpp
QualType getTrackedReferenceType(QualType Pointee, bool Exclusive,
                                  LifetimeParameterIndex Lifetime) const;
```

Implementation in [clang/lib/AST/ASTContext.cpp](clang/lib/AST/ASTContext.cpp)
follows the same pattern as `getPointerType`: check the FoldingSet for an
existing canonical type, or create a new one and insert it.

### 4.4 New Decl Node: `LifetimeParmDecl`

Add a new `Decl` subclass for lifetime template parameters:

```cpp
class LifetimeParmDecl : public NamedDecl {
  unsigned Depth;
  unsigned Position;

public:
  LifetimeParmDecl(DeclContext *DC, SourceLocation Loc,
                   IdentifierInfo *Name, unsigned Depth, unsigned Position)
      : NamedDecl(LifetimeParm, DC, Loc, DeclarationName(Name)),
        Depth(Depth), Position(Position) {}

  unsigned getDepth() const { return Depth; }
  unsigned getIndex() const { return Position; }

  static bool classof(const Decl *D) {
    return D->getKind() == LifetimeParm;
  }
};
```

Register in [clang/include/clang/Basic/DeclNodes.td](clang/include/clang/Basic/DeclNodes.td):

```tablegen
def LifetimeParm : DeclNode<Named>;
```

This allows lifetime parameters to participate in template parameter lists
alongside `TemplateTypeParmDecl`, `NonTypeTemplateParmDecl`, and
`TemplateTemplateParmDecl`.

### 4.5 New Expression Nodes

#### 4.5.1 `BorrowExpr`

Represents `^x` (shared borrow) or `^mut x` (exclusive borrow):

```cpp
class BorrowExpr : public Expr {
  Stmt *Operand;
  bool Exclusive;           // ^mut vs ^
  SourceLocation CaretLoc;

public:
  BorrowExpr(Expr *operand, bool exclusive, QualType resultTy,
             SourceLocation caretLoc)
      : Expr(BorrowExprClass, resultTy, VK_PRValue, OK_Ordinary),
        Operand(operand), Exclusive(exclusive), CaretLoc(caretLoc) {}

  Expr *getOperand() { return cast<Expr>(Operand); }
  bool isExclusive() const { return Exclusive; }
  SourceLocation getCaretLoc() const { return CaretLoc; }
};
```

Register in [clang/include/clang/Basic/StmtNodes.td](clang/include/clang/Basic/StmtNodes.td):

```tablegen
def BorrowExpr : StmtNode<Expr>;
```

#### 4.5.2 `LifetimeRefExpr`

Represents a reference to a lifetime parameter in an expression (e.g., inside
`requires (@a >= @b)`):

```cpp
class LifetimeRefExpr : public Expr {
  LifetimeParmDecl *Param;
  SourceLocation Loc;

public:
  LifetimeRefExpr(LifetimeParmDecl *param, SourceLocation loc);
  LifetimeParmDecl *getParam() const { return Param; }
};
```

### 4.6 LifetimeParameterIndex

A small value type used to identify which lifetime a tracked reference is
tied to:

```cpp
class LifetimeParameterIndex {
  enum : unsigned {
    Elided = ~0u,          // lifetime was not written
    Static = ~1u,          // @static
    Anonymous = ~2u,       // @_
  };
  unsigned Value;

public:
  static LifetimeParameterIndex elided() { return {Elided}; }
  static LifetimeParameterIndex staticLifetime() { return {Static}; }
  static LifetimeParameterIndex anonymous() { return {Anonymous}; }
  static LifetimeParameterIndex fromIndex(unsigned i) { return {i}; }

  bool isElided() const { return Value == Elided; }
  bool isStatic() const { return Value == Static; }
  bool isAnonymous() const { return Value == Anonymous; }
  bool isNamed() const { return Value < Elided - 2; }
  unsigned getValue() const { return Value; }
};
```

---

## 5. Sema Changes

### 5.1 `ActOnBorrowExpr`

New Sema action in [clang/include/clang/Sema/Sema.h](clang/include/clang/Sema/Sema.h)
and [clang/lib/Sema/SemaExpr.cpp](clang/lib/Sema/SemaExpr.cpp):

```cpp
ExprResult Sema::ActOnBorrowExpr(SourceLocation CaretLoc, bool IsMutable,
                                  Expr *Operand);
```

Semantics:
1. The operand must be an lvalue.
2. The result type is `TrackedReferenceType(OperandType, IsMutable, <elided>)`.
3. If the operand is not an lvalue, emit `diag::err_borrow_of_rvalue`.
4. If `IsMutable` is true and the operand is `const`-qualified, emit
   `diag::err_mut_borrow_of_const`.

### 5.2 `ActOnLifetimeParameter`

```cpp
NamedDecl *Sema::ActOnLifetimeParameter(IdentifierInfo *Name,
                                         SourceLocation Loc,
                                         unsigned Depth, unsigned Position);
```

Creates a `LifetimeParmDecl` and adds it to the current scope.

### 5.3 Building `TrackedReferenceType` from Declarator Chunks

In `Sema::GetTypeForDeclarator` (in
[clang/lib/Sema/SemaType.cpp](clang/lib/Sema/SemaType.cpp)), add a case for
`DeclaratorChunk::TrackedReference`:

```cpp
case DeclaratorChunk::TrackedReference: {
    if (!LangOpts.TrackedReferences) {
        Diag(..., diag::err_tracked_ref_not_enabled);
        break;
    }
    LifetimeParameterIndex LT = resolveLifetime(DeclType.TRef.Lifetime);
    T = Context.getTrackedReferenceType(T, DeclType.TRef.IsMutable, LT);
    break;
}
```

---

## 6. Diagnostics

Add the following diagnostics in
[clang/include/clang/Basic/DiagnosticSemaKinds.td](clang/include/clang/Basic/DiagnosticSemaKinds.td):

| ID | Text |
|----|------|
| `err_tracked_ref_not_enabled` | `"tracked references require '-ftracked-references'"` |
| `err_borrow_of_rvalue` | `"cannot borrow an rvalue; borrow target must be an lvalue"` |
| `err_mut_borrow_of_const` | `"cannot create exclusive borrow of const-qualified value"` |
| `err_expected_lifetime_name` | `"expected lifetime name after 'lifetime' keyword"` |
| `err_mutable_tracked_ref_field` | `"'mutable' is not permitted on fields of tracked reference type"` |
| `err_std_move_tracked_ref` | `"'std::move' cannot be applied to tracked references"` |
| `err_tracked_ref_null` | `"tracked references cannot be null"` |
| `warn_lifetime_elision_ambiguous` | `"cannot determine output lifetime; annotate explicitly"` |

---

## 7. Type Printing and Diagnostics Rendering

In [clang/lib/AST/TypePrinter.cpp](clang/lib/AST/TypePrinter.cpp), add
`printTrackedReferenceBefore` / `printTrackedReferenceAfter`:

```cpp
void TypePrinter::printTrackedReferenceBefore(
    const TrackedReferenceType *T, raw_ostream &OS) {
  printBefore(T->getPointeeType(), OS);
}

void TypePrinter::printTrackedReferenceAfter(
    const TrackedReferenceType *T, raw_ostream &OS) {
  printAfter(T->getPointeeType(), OS);
  OS << '^';
  LifetimeParameterIndex LT = T->getLifetime();
  if (LT.isStatic()) OS << "@static";
  else if (LT.isAnonymous()) OS << "@_";
  else if (LT.isNamed()) OS << "@" << getLifetimeName(LT);
  if (T->isExclusive()) OS << " mut";
}
```

---

## 8. Serialization (PCH / Modules)

In [clang/lib/Serialization/ASTWriter.cpp](clang/lib/Serialization/ASTWriter.cpp)
and [clang/lib/Serialization/ASTReader.cpp](clang/lib/Serialization/ASTReader.cpp),
add serialization for:

- `TrackedReferenceType` (type node)
- `LifetimeParmDecl` (decl node)
- `BorrowExpr` (stmt/expr node)
- `LifetimeRefExpr` (stmt/expr node)

Each follows the existing patterns for their respective node categories.

---

## 9. CodeGen (Deferred)

Code generation for tracked references is deferred to a later phase. At the
LLVM IR level, tracked references will be represented as plain pointers (they
have identical runtime representation). The borrow-checking is purely a
compile-time analysis and does not affect generated code.

For this phase, `TrackedReferenceType` emits the same LLVM IR as `PointerType`:
an `llvm::PointerType` (opaque pointer).

---

## 10. Summary: Files Modified

| File | Change |
|------|--------|
| `clang/include/clang/Basic/LangOptions.def` | Add `TrackedReferences` flag |
| `clang/include/clang/Driver/Options.td` | Add `-ftracked-references` |
| `clang/lib/Frontend/CompilerInvocation.cpp` | Wire driver flag |
| `clang/include/clang/Basic/TokenKinds.def` | Add `tok::kw_mut`, `tok::kw_lifetime`, `tok::at_identifier` |
| `clang/lib/Lex/Lexer.cpp` | Handle `@identifier` lexing |
| `clang/include/clang/Basic/TypeNodes.td` | Add `TrackedReferenceType` |
| `clang/include/clang/AST/Type.h` | Define `TrackedReferenceType` class, `LifetimeParameterIndex` |
| `clang/lib/AST/ASTContext.cpp` | Add `getTrackedReferenceType()` |
| `clang/include/clang/AST/ASTContext.h` | Declare `getTrackedReferenceType()` |
| `clang/include/clang/Basic/DeclNodes.td` | Add `LifetimeParm` |
| `clang/include/clang/AST/DeclTemplate.h` | Define `LifetimeParmDecl` class |
| `clang/include/clang/Basic/StmtNodes.td` | Add `BorrowExpr`, `LifetimeRefExpr` |
| `clang/include/clang/AST/Expr.h` | Define `BorrowExpr`, `LifetimeRefExpr` classes |
| `clang/include/clang/Sema/DeclSpec.h` | Extend `DeclaratorChunk` with `TrackedReference` kind |
| `clang/include/clang/Sema/Sema.h` | Declare `ActOnBorrowExpr`, `ActOnLifetimeParameter` |
| `clang/lib/Sema/SemaExpr.cpp` | Implement `ActOnBorrowExpr` |
| `clang/lib/Sema/SemaType.cpp` | Handle `TrackedReference` declarator chunk |
| `clang/lib/Parse/ParseDecl.cpp` | Parse `T^`, `T^ mut`, `T^@a`, `T^@a mut` in declarators |
| `clang/lib/Parse/ParseExpr.cpp` | Parse `^x`, `^mut x` as unary borrow expressions |
| `clang/lib/Parse/ParseTemplate.cpp` | Parse `lifetime @a` in template parameter lists |
| `clang/lib/AST/TypePrinter.cpp` | Print `TrackedReferenceType` |
| `clang/include/clang/Basic/DiagnosticSemaKinds.td` | Add diagnostic messages |
| `clang/lib/Serialization/ASTWriter.cpp` | Serialize new nodes |
| `clang/lib/Serialization/ASTReader.cpp` | Deserialize new nodes |
| `clang/lib/CodeGen/CodeGenTypes.cpp` | Emit tracked refs as opaque pointers |

---

## 11. Phasing

| Phase | Scope |
|-------|-------|
| **Phase 1** | Feature flag, keywords (`mut`, `lifetime`), `@identifier` lexing |
| **Phase 2** | Parser: `T^` / `T^ mut` in declarators, `DeclaratorChunk` extension |
| **Phase 3** | AST: `TrackedReferenceType`, `ASTContext` creation, type printing |
| **Phase 4** | Parser: `^x` / `^mut x` borrow expressions, `BorrowExpr` AST node |
| **Phase 5** | Parser + AST: `template<lifetime @a>`, `LifetimeParmDecl` |
| **Phase 6** | Sema: type checking, diagnostics, `std::move` rejection |
| **Phase 7** | Serialization, CodeGen (trivial — opaque pointers) |
| **Phase 8** | Borrow checker (separate specification) |
