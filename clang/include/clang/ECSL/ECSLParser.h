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
/// internal ECSLExprParser recursive-descent parser for C/C++ arithmetic
/// sub-expressions.
///
/// \todo ECSLParserImpl (contract clause grammar: requires/ensures/assigns)
///       is not yet implemented; ECSLParser::ParseFunctionContract always
///       returns std::nullopt until ECSLParserImpl is wired in.
///
//===----------------------------------------------------------------------===//

#ifndef LLVM_CLANG_ECSL_ECSLPARSER_H
#define LLVM_CLANG_ECSL_ECSLPARSER_H

#include "clang/ECSL/ECSLAst.h"
#include "clang/ECSL/ECSLCommentHandler.h"
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
/// \todo ParseFunctionContract is currently a stub returning std::nullopt;
///       it will be implemented once ECSLParserImpl (contract clause grammar)
///       is wired in.
class ECSLParser {
public:
  ECSLParser() = default;

  /// Parse \p Text as a function contract annotation.
  ///
  /// \param Text  Raw annotation body (comment delimiters already stripped).
  /// \param Loc   Source location of the first character of \p Text.
  ///              Must be a valid SourceLocation; use
  ///              \c SourceLocation::getFromRawEncoding(1) when location
  ///              info is not needed.  Passing an invalid SourceLocation
  ///              will assert in debug builds.
  /// \param Diags Optional diagnostics engine; null means errors are silent.
  ///
  /// Returns std::nullopt when no valid clause was parsed (empty body,
  /// whitespace-only, or all-garbage token stream).  On recoverable clause
  /// errors the parser skips to the next ';' and continues; the returned
  /// contract may be partial.
  std::optional<ECSLFunctionContract>
  ParseFunctionContract(llvm::StringRef Text, SourceLocation Loc,
                        DiagnosticsEngine *Diags = nullptr);

  /// Parse \p Text as a standalone C/C++ arithmetic expression.
  ///
  /// Lexes \p Text using ECSLLexer and runs ECSLExprParser on the resulting
  /// token span.  Primarily intended for unit-testing ECSLExprParser without
  /// going through the full contract grammar.
  ///
  /// \param Text     Raw C/C++ expression text (no comment delimiters).
  /// \param Loc      Source location of the first character of \p Text.
  ///                 Must be a valid SourceLocation.
  ///
  /// Returns nullptr on any parse error (malformed expression, unconsumed
  /// tokens after the expression, etc.).
  std::unique_ptr<ECSLExpr> ParseExpr(llvm::StringRef Text, SourceLocation Loc);
};

/// Compatibility entry point used by the clangParse dispatch hooks.
/// \todo Wire in typed contract storage once the annotation store supports it.
/// Calls ParseFunctionContract for validation; always returns the original
/// PendingAnnotation unchanged so the raw store stays populated.
///
/// \param PA  The raw annotation (text body + source location) to parse.
PendingAnnotation parseECSLAnnotation(PendingAnnotation PA);

} // namespace ecsl
} // namespace clang

#endif // LLVM_CLANG_ECSL_ECSLPARSER_H
