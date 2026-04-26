//===- clang/ECSL/ECSLLexer.cpp - ECSL annotation body lexer --------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "clang/ECSL/ECSLLexer.h"
#include <cassert>
#include <regex>

using namespace clang;
using namespace clang::ecsl;

// ---------------------------------------------------------------------------
// Character classification helpers (ASCII-only, no locale dependency).
// ---------------------------------------------------------------------------

namespace {

bool IsIdentStart(char c) {
  return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_';
}

bool IsIdentContinue(char c) {
  return IsIdentStart(c) || (c >= '0' && c <= '9');
}

bool IsDigit(char c) { return c >= '0' && c <= '9'; }

bool IsSpace(char c) {
  return c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\v' ||
         c == '\f';
}

} // namespace

// ---------------------------------------------------------------------------
// ECSLLexer implementation
// ---------------------------------------------------------------------------

ECSLLexer::ECSLLexer(llvm::StringRef body, SourceLocation base_loc)
    : m_body(body), m_base_loc(base_loc) {}

void ECSLLexer::SkipWhitespace() {
  while (m_pos < m_body.size() && IsSpace(m_body[m_pos])) {
    ++m_pos;
  }
}

ECSLToken ECSLLexer::MakeToken(ECSLTokenKind kind, unsigned start,
                               unsigned end) const {
  ECSLToken tok;
  tok.m_kind = kind;
  tok.m_offset = start;
  tok.m_text = m_body.slice(start, end);
  return tok;
}

ECSLToken ECSLLexer::LexBackslashKeyword() {
  unsigned start = m_pos;
  assert(m_pos < m_body.size() && m_body[m_pos] == '\\');
  ++m_pos; // consume '\'

  // Scan the keyword name: letters and underscores only (digits terminate).
  while (m_pos < m_body.size() && IsIdentStart(m_body[m_pos])) {
    ++m_pos;
  }

  llvm::StringRef name = m_body.slice(start + 1, m_pos);

  if (name == "result") {
    return MakeToken(ECSLTokenKind::BslashResult, start, m_pos);
  }
  if (name == "nothing") {
    return MakeToken(ECSLTokenKind::BslashNothing, start, m_pos);
  }
  return MakeToken(ECSLTokenKind::Unknown, start, m_pos);
}

ECSLToken ECSLLexer::LexIdentifier() {
  unsigned start = m_pos;
  while (m_pos < m_body.size() && IsIdentContinue(m_body[m_pos])) {
    ++m_pos;
  }
  return MakeToken(ECSLTokenKind::Identifier, start, m_pos);
}

ECSLToken ECSLLexer::LexNumber() {
  unsigned start = m_pos;
  llvm::StringRef tail = m_body.substr(m_pos);

  // Float pattern: try first.
  //   1. Hex float:   0[xX] hex-digits (. hex-digits)? p/P sign? decimal-digits
  //   2. Decimal frac: digits? . digits (e/E sign? digits)?
  //   3. Decimal exp:  digits e/E sign? digits
  // Digit separators (') are permitted throughout.  Optional suffix: f F l L.
  static const std::regex kFloat(
      R"((?:0[xX][0-9a-fA-F']*(?:\.[0-9a-fA-F']*)?[pP][+-]?\d+)"
      R"(|(?:\d[\d']*\.[\d']*|\.[\d']+)(?:[eE][+-]?\d[\d']*)?)"
      R"(|\d[\d']*[eE][+-]?\d[\d']*)[fFlLdD]?)",
      std::regex::ECMAScript | std::regex::optimize);

  // Integer pattern: fallback.
  //   Hex, binary, or decimal/octal, with optional integer suffix.
  static const std::regex kInteger(
      R"((?:0[xX][0-9a-fA-F']+|0[bB][01']+|\d[\d']*)[uUlL]*)",
      std::regex::ECMAScript | std::regex::optimize);

  std::cmatch match;
  const char *begin = tail.data();
  const char *end = begin + tail.size();

  if (std::regex_search(begin, end, match, kFloat,
                        std::regex_constants::match_continuous)) {
    m_pos += static_cast<unsigned>(match.length());
    return MakeToken(ECSLTokenKind::FloatLiteral, start, m_pos);
  }

  if (std::regex_search(begin, end, match, kInteger,
                        std::regex_constants::match_continuous)) {
    m_pos += static_cast<unsigned>(match.length());
    return MakeToken(ECSLTokenKind::IntegerLiteral, start, m_pos);
  }

  // Fallback: consume one character.  Unreachable when called from LexOne()
  // (which guarantees the current character is a digit), but kept for safety.
  ++m_pos;
  return MakeToken(ECSLTokenKind::IntegerLiteral, start, m_pos);
}

ECSLToken ECSLLexer::LexOperator() {
  unsigned start = m_pos;
  char c = m_body[m_pos];

  // Two-character operators — checked before single-char to avoid e.g.
  // producing Bang then Eq for "!=".
  if (m_pos + 1 < m_body.size()) {
    char n = m_body[m_pos + 1];
    if (c == '&' && n == '&') {
      m_pos += 2;
      return MakeToken(ECSLTokenKind::AmpAmp, start, m_pos);
    }
    if (c == '|' && n == '|') {
      m_pos += 2;
      return MakeToken(ECSLTokenKind::PipePipe, start, m_pos);
    }
    if (c == '=' && n == '=') {
      m_pos += 2;
      return MakeToken(ECSLTokenKind::EqEq, start, m_pos);
    }
    if (c == '!' && n == '=') {
      m_pos += 2;
      return MakeToken(ECSLTokenKind::BangEq, start, m_pos);
    }
    if (c == '<' && n == '=') {
      m_pos += 2;
      return MakeToken(ECSLTokenKind::Le, start, m_pos);
    }
    if (c == '>' && n == '=') {
      m_pos += 2;
      return MakeToken(ECSLTokenKind::Ge, start, m_pos);
    }
  }

  // Single-character tokens.
  ++m_pos;
  switch (c) {
  case '!':
    return MakeToken(ECSLTokenKind::Bang, start, m_pos);
  case '<':
    return MakeToken(ECSLTokenKind::Lt, start, m_pos);
  case '>':
    return MakeToken(ECSLTokenKind::Gt, start, m_pos);
  case ';':
    return MakeToken(ECSLTokenKind::Semicolon, start, m_pos);
  case '(':
    return MakeToken(ECSLTokenKind::LParen, start, m_pos);
  case ')':
    return MakeToken(ECSLTokenKind::RParen, start, m_pos);
  case '+':
    return MakeToken(ECSLTokenKind::Plus, start, m_pos);
  case '-':
    return MakeToken(ECSLTokenKind::Minus, start, m_pos);
  case '*':
    return MakeToken(ECSLTokenKind::Star, start, m_pos);
  case '/':
    return MakeToken(ECSLTokenKind::Slash, start, m_pos);
  case '%':
    return MakeToken(ECSLTokenKind::Percent, start, m_pos);
  case '=':
    return MakeToken(ECSLTokenKind::Eq, start, m_pos);
  case '&':
    return MakeToken(ECSLTokenKind::Amp, start, m_pos);
  case '|':
    return MakeToken(ECSLTokenKind::Pipe, start, m_pos);
  case '^':
    return MakeToken(ECSLTokenKind::Caret, start, m_pos);
  case '~':
    return MakeToken(ECSLTokenKind::Tilde, start, m_pos);
  case ',':
    return MakeToken(ECSLTokenKind::Comma, start, m_pos);
  case '.':
    return MakeToken(ECSLTokenKind::Dot, start, m_pos);
  default:
    return MakeToken(ECSLTokenKind::Unknown, start, m_pos);
  }
}

ECSLToken ECSLLexer::LexOne() {
  SkipWhitespace();

  if (m_pos >= m_body.size()) {
    return MakeToken(ECSLTokenKind::Eof, m_pos, m_pos);
  }

  char c = m_body[m_pos];

  // Backslash keyword (\result, \nothing, ...).
  if (c == '\\') {
    return LexBackslashKeyword();
  }

  // Identifier.
  if (IsIdentStart(c)) {
    return LexIdentifier();
  }

  // Number literal.
  if (IsDigit(c)) {
    return LexNumber();
  }

  // Float literal with a leading decimal point (e.g. ".5").
  if (c == '.' && m_pos + 1 < m_body.size() && IsDigit(m_body[m_pos + 1])) {
    return LexNumber();
  }

  return LexOperator();
}

void ECSLLexer::Lex(llvm::SmallVectorImpl<ECSLToken> &out) {
  while (true) {
    ECSLToken tok = LexOne();
    out.push_back(tok);
    if (tok.m_kind == ECSLTokenKind::Eof) {
      break;
    }
  }
}
