// RUN: %clang_cc1 -fsyntax-only -std=c++20 -verify %s
//
// Tests that @identifier tokens are NOT recognized when Mizar mode is off
// (no -ftracked-references and no #pragma mizar on). The @ should be
// treated as an unknown token.

template<typename T>
class Plain {
  T *_ptr;
};

// This should fail — 'lifetime' is just an identifier without Mizar,
// and @ is an unknown token.
template<lifetime @a>  // expected-error {{unknown type name 'lifetime'}}
class Bad {};
