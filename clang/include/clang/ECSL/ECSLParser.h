//===- clang/ECSL/ECSLParser.h - ECSL annotation parser (M1) --------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
///
/// \file
/// Declares the M1 ECSL annotation recursive-descent parser.
///
/// ECSLParser handles the M1 clause set:
///   requires <pred>;  ensures <pred>;  assigns \nothing;
///
/// Predicates support:  ==, !=, <, <=, >, >=, &&, ||, !
/// Terms support:       \result, \nothing, C/C++ expressions (delegated)
/// Arithmetic (+, -, *, /, %) is inside C terms; delegation passes the raw
/// ECSLToken span to an ExprDelegate callback supplied by the caller.
///
/// Layering: clangECSL must NOT depend on clangParse.  ECSLParser only uses
/// clangBasic and clangLex headers.  Clang expression delegation is handled
/// via the ExprDelegate callback which is provided by the clangParse layer in
/// PR 4/4; callers that do not need delegation (tests, validation-only passes)
/// may pass nullptr, in which case CExpr terms carry a null clang::Expr*.
///
//===----------------------------------------------------------------------===//

#ifndef LLVM_CLANG_ECSL_ECSLPARSER_H
#define LLVM_CLANG_ECSL_ECSLPARSER_H

#include "clang/ECSL/ECSLAst.h"
#include "clang/ECSL/ECSLCommentHandler.h"
#include "clang/ECSL/ECSLLexer.h"
#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/StringRef.h"
#include <functional>
#include <optional>

namespace clang {
class DiagnosticsEngine;
class Expr;

namespace ecsl {

/// Parses ECSL annotation bodies for M1 function contracts.
///
/// Instantiate once; call ParseFunctionContract for each annotation.
/// Thread-hostile: state is per-call, but the object must not be used
/// concurrently from multiple threads.
class ECSLParser {
public:
  /// Callback type for delegating a C/C++ expression fragment to Clang.
  ///
  /// Receives the raw ECSLToken span that makes up the C expression and
  /// returns the parsed clang::Expr* (may be null on error).
  /// Provided by the clangParse layer (PR 4/4); pass nullptr for tests or
  /// when expression delegation is not needed.
  using ExprDelegate = std::function<clang::Expr *(llvm::ArrayRef<ECSLToken>)>;

  ECSLParser() = default;

  /// Parse \p Text as an M1 function contract annotation.
  ///
  /// \param Text     Raw annotation body (comment delimiters already stripped).
  /// \param Loc      Source location of the annotation's opening delimiter.
  /// \param Diags    Optional diagnostics engine; null means errors are silent.
  /// \param Delegate Optional Clang expression delegate; null means CExpr terms
  ///                 carry null clang::Expr* pointers.
  ///
  /// Returns std::nullopt only on a fatal structural error (e.g. no valid
  /// clauses and an unrecoverable token stream).  On recoverable errors the
  /// parser skips to the next ';' and continues; the returned contract may be
  /// partial.
  std::optional<ECSLFunctionContract>
  ParseFunctionContract(llvm::StringRef Text, SourceLocation Loc,
                        DiagnosticsEngine *Diags = nullptr,
                        ExprDelegate Delegate = nullptr);
};

/// Compatibility entry point used by the clangParse dispatch hooks (PR 4/5,
/// PR 5/5).  Calls ParseFunctionContract for validation; always returns the
/// original PendingAnnotation unchanged so the raw store stays populated until
/// PR 4/4 wires in typed contract storage.
PendingAnnotation parseECSLAnnotation(PendingAnnotation PA);

} // namespace ecsl
} // namespace clang

#endif // LLVM_CLANG_ECSL_ECSLPARSER_H
