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
/// Terms support:       \result, \nothing, C expressions
/// Arithmetic (+, -, *, /, %) inside C terms is parsed by ECSL's own
/// recursive-descent C expression parser, producing ECSLExpr trees.
///
/// Layering: clangECSL must NOT depend on clangParse.  ECSLParser only uses
/// clangBasic and clangLex headers.  No Clang expression delegation.
///
//===----------------------------------------------------------------------===//

#ifndef LLVM_CLANG_ECSL_ECSLPARSER_H
#define LLVM_CLANG_ECSL_ECSLPARSER_H

#include "clang/ECSL/ECSLAst.h"
#include "clang/ECSL/ECSLCommentHandler.h"
#include "clang/ECSL/ECSLLexer.h"
#include "llvm/ADT/StringRef.h"
#include <optional>

namespace clang {
class DiagnosticsEngine;

namespace ecsl {

/// Parses ECSL annotation bodies for M1 function contracts.
///
/// Instantiate once; call ParseFunctionContract for each annotation.
/// Thread-hostile: state is per-call, but the object must not be used
/// concurrently from multiple threads.
class ECSLParser {
public:
  ECSLParser() = default;

  /// Parse \p Text as an M1 function contract annotation.
  ///
  /// \param Text  Raw annotation body (comment delimiters already stripped).
  /// \param Loc   Source location of the annotation's opening delimiter.
  /// \param Diags Optional diagnostics engine; null means errors are silent.
  ///
  /// Returns std::nullopt only on a fatal structural error (e.g. no valid
  /// clauses and an unrecoverable token stream).  On recoverable errors the
  /// parser skips to the next ';' and continues; the returned contract may be
  /// partial.
  std::optional<ECSLFunctionContract>
  ParseFunctionContract(llvm::StringRef Text, SourceLocation Loc,
                        DiagnosticsEngine *Diags = nullptr);
};

/// Compatibility entry point used by the clangParse dispatch hooks (PR 4/5,
/// PR 5/5).  Calls ParseFunctionContract for validation; always returns the
/// original PendingAnnotation unchanged so the raw store stays populated until
/// PR 4/4 wires in typed contract storage.
PendingAnnotation parseECSLAnnotation(PendingAnnotation PA);

} // namespace ecsl
} // namespace clang

#endif // LLVM_CLANG_ECSL_ECSLPARSER_H
