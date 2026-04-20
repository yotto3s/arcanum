//===- clang/ECSL/ECSLLexer.cpp - ECSL annotation body lexer --------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "clang/ECSL/ECSLLexer.h"
#include <cassert>

using namespace clang;
using namespace clang::ecsl;

// ---------------------------------------------------------------------------
// Character classification helpers (ASCII-only, no locale dependency).
// ---------------------------------------------------------------------------

static bool IsIdentStart(char c) {
  return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_';
}

static bool IsIdentContinue(char c) {
  return IsIdentStart(c) || (c >= '0' && c <= '9');
}

static bool IsDigit(char c) { return c >= '0' && c <= '9'; }

static bool IsSpace(char c) {
  return c == ' ' || c == '\t' || c == '\n' || c == '\r';
}

// ---------------------------------------------------------------------------
// ECSLLexer implementation
// ---------------------------------------------------------------------------

ECSLLexer::ECSLLexer(llvm::StringRef body, SourceLocation base_loc)
    : m_body(body), m_base_loc(base_loc) {}

void ECSLLexer::SkipWhitespace() {
  while (m_pos < m_body.size() && IsSpace(m_body[m_pos]))
    ++m_pos;
}

ECSLToken ECSLLexer::MakeToken(ECSLTokenKind kind, unsigned start) const {
  ECSLToken tok;
  tok.m_kind = kind;
  tok.m_offset = start;
  tok.m_text = m_body.slice(start, m_pos);
  return tok;
}

ECSLToken ECSLLexer::LexBackslashKeyword() {
  unsigned start = m_pos;
  assert(m_pos < m_body.size() && m_body[m_pos] == '\\');
  ++m_pos; // consume '\'

  // Scan the keyword name: letters and underscores only.
  while (m_pos < m_body.size() && IsIdentContinue(m_body[m_pos]))
    ++m_pos;

  llvm::StringRef name = m_body.slice(start + 1, m_pos);

  ECSLTokenKind kind;
  if (name == "result")
    kind = ECSLTokenKind::BslashResult;
  else if (name == "nothing")
    kind = ECSLTokenKind::BslashNothing;
  else
    kind = ECSLTokenKind::CAtom; // Unknown backslash keyword; pass through.

  return MakeToken(kind, start);
}

ECSLToken ECSLLexer::LexWord() {
  unsigned start = m_pos;
  while (m_pos < m_body.size() && IsIdentContinue(m_body[m_pos]))
    ++m_pos;

  llvm::StringRef word = m_body.slice(start, m_pos);

  if (m_at_clause_pos) {
    if (word == "requires")
      return MakeToken(ECSLTokenKind::KwRequires, start);
    if (word == "ensures")
      return MakeToken(ECSLTokenKind::KwEnsures, start);
    if (word == "assigns")
      return MakeToken(ECSLTokenKind::KwAssigns, start);
  }

  return MakeToken(ECSLTokenKind::CAtom, start);
}

ECSLToken ECSLLexer::LexNumber() {
  unsigned start = m_pos;
  // Consume digits, decimal point, hex prefix (0x/0X), and C++14 digit
  // separators ('). Stop at anything else; Clang will validate the literal.
  while (m_pos < m_body.size()) {
    char c = m_body[m_pos];
    if (IsDigit(c) || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F') ||
        c == 'x' || c == 'X' || c == '.' || c == '\'')
      ++m_pos;
    else
      break;
  }
  // Consume any trailing integer/float suffix (u, l, f, etc.).
  while (m_pos < m_body.size() && IsIdentStart(m_body[m_pos]))
    ++m_pos;
  return MakeToken(ECSLTokenKind::CAtom, start);
}

ECSLToken ECSLLexer::LexOne() {
  SkipWhitespace();

  if (m_pos >= m_body.size())
    return MakeToken(ECSLTokenKind::Eof, m_pos);

  char c = m_body[m_pos];
  unsigned start = m_pos;

  // Backslash keyword (\result, \nothing, ...).
  if (c == '\\')
    return LexBackslashKeyword();

  // Identifier or context-sensitive clause keyword.
  if (IsIdentStart(c))
    return LexWord();

  // Number literal.
  if (IsDigit(c))
    return LexNumber();

  // Two-character ECSL operators — checked before single-char to avoid
  // e.g., producing Bang then Eq for "!=".
  if (m_pos + 1 < m_body.size()) {
    char n = m_body[m_pos + 1];
    if (c == '&' && n == '&') {
      m_pos += 2;
      return MakeToken(ECSLTokenKind::Amp2, start);
    }
    if (c == '|' && n == '|') {
      m_pos += 2;
      return MakeToken(ECSLTokenKind::Pipe2, start);
    }
    if (c == '=' && n == '=') {
      m_pos += 2;
      return MakeToken(ECSLTokenKind::EqEq, start);
    }
    if (c == '!' && n == '=') {
      m_pos += 2;
      return MakeToken(ECSLTokenKind::BangEq, start);
    }
    if (c == '<' && n == '=') {
      m_pos += 2;
      return MakeToken(ECSLTokenKind::Le, start);
    }
    if (c == '>' && n == '=') {
      m_pos += 2;
      return MakeToken(ECSLTokenKind::Ge, start);
    }
  }

  // Single-character tokens.
  ++m_pos;
  switch (c) {
  case '!':
    return MakeToken(ECSLTokenKind::Bang, start);
  case '<':
    return MakeToken(ECSLTokenKind::Lt, start);
  case '>':
    return MakeToken(ECSLTokenKind::Gt, start);
  case ';':
    return MakeToken(ECSLTokenKind::Semi, start);
  case '(':
    return MakeToken(ECSLTokenKind::LParen, start);
  case ')':
    return MakeToken(ECSLTokenKind::RParen, start);
  default:
    // Single-char C atom: +, -, *, /, %, =, &, |, ^, ~, comma, dot, etc.
    return MakeToken(ECSLTokenKind::CAtom, start);
  }
}

void ECSLLexer::Lex(llvm::SmallVectorImpl<ECSLToken> &out) {
  while (true) {
    ECSLToken tok = LexOne();

    // Update clause-position flag based on what was just lexed, so the next
    // call to LexOne sees the correct state.
    if (tok.m_kind == ECSLTokenKind::Semi)
      m_at_clause_pos = true;
    else if (tok.m_kind != ECSLTokenKind::Eof)
      m_at_clause_pos = false;

    out.push_back(tok);

    if (tok.m_kind == ECSLTokenKind::Eof)
      break;
  }
}
