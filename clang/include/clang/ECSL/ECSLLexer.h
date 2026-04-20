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
/// Identifiers and clause keywords:
///   "requires", "ensures", "assigns" are produced as plain Identifier tokens.
///   The ECSL parser recognises them from context.
///
/// Numeric literals:
///   Integer literals (decimal, hex, octal, binary) are produced as
///   IntegerLiteral tokens.  Float literals (decimal-point or exponent form,
///   including hex floats) are produced as FloatLiteral tokens.
///
/// Backslash keywords:
///   \result → BslashResult, \nothing → BslashNothing; unknown backslash
///   sequences produce Unknown.
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

  // Backslash keywords — M1 terms.
  BslashResult,  ///< "\result"
  BslashNothing, ///< "\nothing"

  // Logical operators.
  AmpAmp,   ///< "&&"
  PipePipe, ///< "||"
  Bang,     ///< "!"

  // Relational operators.
  EqEq,   ///< "=="
  BangEq, ///< "!="
  Lt,     ///< "<"
  Le,     ///< "<="
  Gt,     ///< ">"
  Ge,     ///< ">="

  // Punctuation.
  Semicolon, ///< ";"
  LParen,    ///< "("
  RParen,    ///< ")"

  // Arithmetic and assignment operators.
  Plus,    ///< "+"
  Minus,   ///< "-"
  Star,    ///< "*"
  Slash,   ///< "/"
  Percent, ///< "%"
  Eq,      ///< "=" (single equal, e.g. assignment)

  // Bitwise operators.
  Amp,   ///< "&" (single ampersand)
  Pipe,  ///< "|" (single pipe)
  Caret, ///< "^"
  Tilde, ///< "~"

  // Other single-character C tokens.
  Comma, ///< ","
  Dot,   ///< "."

  // Atoms.
  Identifier,     ///< A C identifier, including clause-keyword names such as
                  ///< "requires", "ensures", "assigns".  The parser resolves
                  ///< their role from context.
  IntegerLiteral, ///< An integer literal (decimal, hex, octal, or binary),
                  ///< including any digit separators and optional suffix.
  FloatLiteral,   ///< A floating-point literal: has a decimal point in
                  ///< decimal context, or a binary exponent (p/P) in hex
                  ///< context.

  Unknown, ///< An unrecognised character or backslash sequence.  The parser
           ///< should report a diagnostic and recover.
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
///   // annotation.Loc points to the "/*@"/"//@ " delimiter; the body starts
///   // 3 bytes in (past the comment-open prefix).
///   ECSLLexer lexer(annotation.Body, annotation.Loc.getLocWithOffset(3));
///   lexer.Lex(tokens);
/// \endcode
///
/// Lex() always appends an Eof token as the final element.
class ECSLLexer {
public:
  /// Construct the lexer.
  ///
  /// \param body     The raw annotation body text (comment delimiters already
  ///                 stripped), as produced by ECSLCommentHandler.
  /// \param base_loc The SourceLocation of the first character of \p body.
  ///                 Callers can compute per-token locations via
  ///                 base_loc.getLocWithOffset(token.m_offset).
  ECSLLexer(llvm::StringRef body, SourceLocation base_loc);
  ECSLLexer(const ECSLLexer &) = delete;
  ECSLLexer &operator=(const ECSLLexer &) = delete;

  /// Tokenise the entire body, appending tokens (including a final Eof) to
  /// \p out.
  void Lex(llvm::SmallVectorImpl<ECSLToken> &out);

  /// Return the base source location passed at construction time.
  SourceLocation GetBaseLoc() const { return m_base_loc; }

private:
  void SkipWhitespace();
  ECSLToken LexOne();
  ECSLToken LexBackslashKeyword();
  ECSLToken LexIdentifier();
  ECSLToken LexNumber();
  ECSLToken LexOperator();
  ECSLToken MakeToken(ECSLTokenKind kind, unsigned start, unsigned end) const;

  llvm::StringRef m_body;
  SourceLocation m_base_loc;
  unsigned m_pos = 0;
};

} // namespace ecsl
} // namespace clang

#endif // LLVM_CLANG_ECSL_ECSLLEXER_H
