//===- clang/ECSL/ECSLLexer.h - ECSL annotation body lexer ----------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
///
/// \file
/// Declares ECSLTokenKind, ECSLToken, and ECSLLexer — the lexer that
/// tokenises an ECSL annotation body string into a flat token sequence.
///
/// The body text is the raw content produced by ECSLCommentHandler (comment
/// delimiters already stripped).  The lexer does NOT require Clang's
/// preprocessor; it operates directly on a StringRef.
///
/// Context-sensitive clause keywords:
///   "requires", "ensures", "assigns" are produced as keyword tokens only at
///   clause position — the start of the annotation or immediately after a
///   semicolon.  Elsewhere they are CAtom pass-through tokens for Clang
///   delegation.
///
/// C pass-through atoms:
///   Any character sequence that is not an ECSL-specific token is returned as
///   a CAtom token.  The parser groups consecutive CAtom tokens into spans
///   that are delegated to Clang's expression parser.
///
//===----------------------------------------------------------------------===//

#ifndef LLVM_CLANG_ECSL_ECSLLEXER_H
#define LLVM_CLANG_ECSL_ECSLLEXER_H

#include "clang/Basic/SourceLocation.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/StringRef.h"

namespace clang {
namespace ecsl {

/// Token kinds produced by the ECSL annotation lexer.
///
/// Two-character ECSL operators (&&, ||, ==, !=, <=, >=) are always produced
/// as a single token and take priority over their single-character prefixes.
enum class ECSLTokenKind {
  Eof, ///< End of annotation body.

  // Clause keywords — context-sensitive; only at clause position.
  KwRequires, ///< "requires"
  KwEnsures,  ///< "ensures"
  KwAssigns,  ///< "assigns"

  // Backslash keywords — M1 terms.
  BslashResult,  ///< "\result"
  BslashNothing, ///< "\nothing"

  // Logical operators.
  Amp2,  ///< "&&"
  Pipe2, ///< "||"
  Bang,  ///< "!"

  // Relational operators.
  EqEq,   ///< "=="
  BangEq, ///< "!="
  Lt,     ///< "<"
  Le,     ///< "<="
  Gt,     ///< ">"
  Ge,     ///< ">="

  // Punctuation.
  Semi,   ///< ";"
  LParen, ///< "("
  RParen, ///< ")"

  // C pass-through.
  CAtom, ///< A single C token (identifier, number, single operator, etc.)
         ///< that is not an ECSL-specific construct.  The parser collects
         ///< consecutive CAtom tokens into a text span for Clang delegation.
};

/// A single token produced by ECSLLexer.
///
/// m_text is a StringRef into the original annotation body; it remains valid
/// for the lifetime of that StringRef.  m_offset is the byte offset of the
/// first character from the start of the body, usable to compute a
/// SourceLocation via base_loc.getLocWithOffset(m_offset).
struct ECSLToken {
  ECSLTokenKind m_kind = ECSLTokenKind::Eof;
  llvm::StringRef m_text;
  unsigned m_offset = 0;
};

/// Tokenises an ECSL annotation body string.
///
/// Usage:
/// \code
///   llvm::SmallVector<ECSLToken> tokens;
///   ECSLLexer lexer(annotation.Body, annotation.Loc);
///   lexer.Lex(tokens);
/// \endcode
///
/// Lex() always appends an Eof token as the final element.
class ECSLLexer {
public:
  ECSLLexer(llvm::StringRef body, SourceLocation base_loc);

  /// Tokenise the entire body, appending tokens (including a final Eof) to
  /// \p out.
  void Lex(llvm::SmallVectorImpl<ECSLToken> &out);

private:
  void SkipWhitespace();
  ECSLToken LexOne();
  ECSLToken LexBackslashKeyword();
  ECSLToken LexWord();
  ECSLToken LexNumber();
  ECSLToken MakeToken(ECSLTokenKind kind, unsigned start) const;

  llvm::StringRef m_body;
  SourceLocation m_base_loc;
  unsigned m_pos = 0;
  bool m_at_clause_pos = true; ///< Reset to true at start and after each ";".
};

} // namespace ecsl
} // namespace clang

#endif // LLVM_CLANG_ECSL_ECSLLEXER_H
