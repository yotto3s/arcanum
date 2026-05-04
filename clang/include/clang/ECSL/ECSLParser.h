//===- clang/ECSL/ECSLParser.h - ECSL annotation parser --------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
///
/// \file
/// Declares the ECSL annotation parser front-end (ECSLParser) and the
/// internal ECSLExprParser recursive-descent parser for C arithmetic
/// sub-expressions.
///
/// This PR introduces ECSLExprParser only.  ECSLParser::ParseFunctionContract
/// is a stub that always returns std::nullopt; ECSLParserImpl (contract clause
/// grammar: requires/ensures/assigns) is added in the next PR.
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

/// Parses ECSL annotation bodies.
///
/// Stateless parser front-end.  Instantiate once and call
/// ParseFunctionContract for each annotation; parsing state is created
/// per call.
///
/// \note ParseFunctionContract is currently a stub returning std::nullopt.
///       ECSLParserImpl (contract clause grammar) is added in the next PR.
class ECSLParser {
public:
  ECSLParser() = default;

  /// Parse \p Text as a function contract annotation.
  ///
  /// \param Text  Raw annotation body (comment delimiters already stripped).
  /// \param Loc   Source location of the first character of \p Text.
  ///              Pass an invalid SourceLocation to suppress location info.
  /// \param Diags Optional diagnostics engine; null means errors are silent.
  ///
  /// Returns std::nullopt when no valid clause was parsed (empty body,
  /// whitespace-only, or all-garbage token stream).  On recoverable clause
  /// errors the parser skips to the next ';' and continues; the returned
  /// contract may be partial.
  std::optional<ECSLFunctionContract>
  ParseFunctionContract(llvm::StringRef Text, SourceLocation Loc,
                        DiagnosticsEngine *Diags = nullptr);
};

/// Compatibility entry point used by the clangParse dispatch hooks.
/// \todo Wire in typed contract storage once the annotation store supports it.
/// Calls ParseFunctionContract for validation; always returns the original
/// PendingAnnotation unchanged so the raw store stays populated.
PendingAnnotation parseECSLAnnotation(PendingAnnotation PA);

} // namespace ecsl
} // namespace clang

#endif // LLVM_CLANG_ECSL_ECSLPARSER_H
