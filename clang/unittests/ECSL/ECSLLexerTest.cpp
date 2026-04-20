//===- unittests/ECSL/ECSLLexerTest.cpp - ECSLLexer unit tests ------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
///
/// \file
/// Unit tests for ECSLLexer.  These tests operate entirely on StringRef input
/// and do not require any Clang compilation infrastructure.
///
//===----------------------------------------------------------------------===//

#include "clang/ECSL/ECSLLexer.h"
#include "clang/Basic/SourceLocation.h"
#include "gtest/gtest.h"
#include <string>
#include <vector>

using namespace clang;
using namespace clang::ecsl;

namespace {

// ---------------------------------------------------------------------------
// Helper
// ---------------------------------------------------------------------------

/// Lex \p body and return the resulting token kinds (including the Eof).
static std::vector<ECSLTokenKind> Kinds(llvm::StringRef body) {
  llvm::SmallVector<ECSLToken> toks;
  ECSLLexer lexer(body, SourceLocation{});
  lexer.Lex(toks);
  std::vector<ECSLTokenKind> result;
  for (auto &t : toks)
    result.push_back(t.m_kind);
  return result;
}

/// Lex \p body and return (kind, text) pairs for every non-Eof token.
static std::vector<std::pair<ECSLTokenKind, std::string>> Tokens(
    llvm::StringRef body) {
  llvm::SmallVector<ECSLToken> toks;
  ECSLLexer lexer(body, SourceLocation{});
  lexer.Lex(toks);
  std::vector<std::pair<ECSLTokenKind, std::string>> result;
  for (auto &t : toks)
    if (t.m_kind != ECSLTokenKind::Eof)
      result.emplace_back(t.m_kind, t.m_text.str());
  return result;
}

using K = ECSLTokenKind;

// ---------------------------------------------------------------------------
// Empty / whitespace
// ---------------------------------------------------------------------------

TEST(ECSLLexerTest, EmptyBodyProducesEofOnly) {
  auto kinds = Kinds("");
  ASSERT_EQ(kinds.size(), 1u);
  EXPECT_EQ(kinds[0], K::Eof);
}

TEST(ECSLLexerTest, WhitespaceOnlyProducesEofOnly) {
  auto kinds = Kinds("   \t\n  ");
  ASSERT_EQ(kinds.size(), 1u);
  EXPECT_EQ(kinds[0], K::Eof);
}

// ---------------------------------------------------------------------------
// Clause keywords — context-sensitive
// ---------------------------------------------------------------------------

TEST(ECSLLexerTest, RequiresAtClausePosition) {
  auto kinds = Kinds("requires");
  EXPECT_EQ(kinds[0], K::KwRequires);
}

TEST(ECSLLexerTest, EnsuresAtClausePosition) {
  auto kinds = Kinds("ensures");
  EXPECT_EQ(kinds[0], K::KwEnsures);
}

TEST(ECSLLexerTest, AssignsAtClausePosition) {
  auto kinds = Kinds("assigns");
  EXPECT_EQ(kinds[0], K::KwAssigns);
}

TEST(ECSLLexerTest, ClauseKeywordAfterSemicolon) {
  // After a ";", the lexer is at clause position again.
  auto kinds = Kinds("requires x > 0; ensures \\result > 0;");
  EXPECT_EQ(kinds[0], K::KwRequires);
  // Find the "ensures" token.
  auto it = std::find(kinds.begin(), kinds.end(), K::KwEnsures);
  EXPECT_NE(it, kinds.end()) << "expected KwEnsures after semicolon";
}

TEST(ECSLLexerTest, ClauseKeywordInsideExpressionIsAtom) {
  // "x_requires" is NOT a keyword — it merely contains "requires" as a prefix.
  auto toks = Tokens("requires x_requires > 0;");
  ASSERT_GE(toks.size(), 2u);
  EXPECT_EQ(toks[0].first, K::KwRequires);
  EXPECT_EQ(toks[1].first, K::CAtom);
  EXPECT_EQ(toks[1].second, "x_requires");
}

TEST(ECSLLexerTest, ClauseKeywordInExprPositionIsAtom) {
  // Inside a predicate (not at clause position), "ensures" is a C identifier.
  // "requires ensures_ok > 0;" — after "requires", not at clause pos.
  auto toks = Tokens("requires ensures_ok > 0;");
  EXPECT_EQ(toks[0].first, K::KwRequires);
  EXPECT_EQ(toks[1].first, K::CAtom);
  EXPECT_EQ(toks[1].second, "ensures_ok");
}

// ---------------------------------------------------------------------------
// Backslash keywords
// ---------------------------------------------------------------------------

TEST(ECSLLexerTest, BackslashResult) {
  auto toks = Tokens("\\result");
  ASSERT_EQ(toks.size(), 1u);
  EXPECT_EQ(toks[0].first, K::BslashResult);
  EXPECT_EQ(toks[0].second, "\\result");
}

TEST(ECSLLexerTest, BackslashNothing) {
  auto toks = Tokens("\\nothing");
  ASSERT_EQ(toks.size(), 1u);
  EXPECT_EQ(toks[0].first, K::BslashNothing);
}

TEST(ECSLLexerTest, UnknownBackslashIsAtom) {
  auto toks = Tokens("\\vvalid");
  ASSERT_EQ(toks.size(), 1u);
  EXPECT_EQ(toks[0].first, K::CAtom);
  EXPECT_EQ(toks[0].second, "\\vvalid");
}

// ---------------------------------------------------------------------------
// ECSL relational operators
// ---------------------------------------------------------------------------

TEST(ECSLLexerTest, RelOpEqEq) {
  EXPECT_EQ(Tokens("==")[0].first, K::EqEq);
}

TEST(ECSLLexerTest, RelOpBangEq) {
  EXPECT_EQ(Tokens("!=")[0].first, K::BangEq);
}

TEST(ECSLLexerTest, RelOpLt) {
  EXPECT_EQ(Tokens("<")[0].first, K::Lt);
}

TEST(ECSLLexerTest, RelOpLe) {
  EXPECT_EQ(Tokens("<=")[0].first, K::Le);
}

TEST(ECSLLexerTest, RelOpGt) {
  EXPECT_EQ(Tokens(">")[0].first, K::Gt);
}

TEST(ECSLLexerTest, RelOpGe) {
  EXPECT_EQ(Tokens(">=")[0].first, K::Ge);
}

TEST(ECSLLexerTest, LeNotLtFollowedByEq) {
  // "<=" must produce Le, not Lt + CAtom("=").
  auto kinds = Kinds("<=");
  EXPECT_EQ(kinds[0], K::Le);
  EXPECT_EQ(kinds[1], K::Eof);
}

TEST(ECSLLexerTest, BangEqNotBangFollowedByEq) {
  auto kinds = Kinds("!=");
  EXPECT_EQ(kinds[0], K::BangEq);
  EXPECT_EQ(kinds[1], K::Eof);
}

// ---------------------------------------------------------------------------
// Logical operators
// ---------------------------------------------------------------------------

TEST(ECSLLexerTest, Amp2) {
  EXPECT_EQ(Tokens("&&")[0].first, K::Amp2);
}

TEST(ECSLLexerTest, Pipe2) {
  EXPECT_EQ(Tokens("||")[0].first, K::Pipe2);
}

TEST(ECSLLexerTest, Bang) {
  EXPECT_EQ(Tokens("!")[0].first, K::Bang);
}

TEST(ECSLLexerTest, SingleAmpIsAtom) {
  // "&" (bitwise) is not an ECSL operator.
  EXPECT_EQ(Tokens("&")[0].first, K::CAtom);
}

TEST(ECSLLexerTest, SinglePipeIsAtom) {
  EXPECT_EQ(Tokens("|")[0].first, K::CAtom);
}

// ---------------------------------------------------------------------------
// Punctuation
// ---------------------------------------------------------------------------

TEST(ECSLLexerTest, Semicolon) {
  EXPECT_EQ(Tokens(";")[0].first, K::Semi);
}

TEST(ECSLLexerTest, Parens) {
  auto toks = Tokens("()");
  EXPECT_EQ(toks[0].first, K::LParen);
  EXPECT_EQ(toks[1].first, K::RParen);
}

// ---------------------------------------------------------------------------
// C atoms
// ---------------------------------------------------------------------------

TEST(ECSLLexerTest, IdentifierIsAtom) {
  auto toks = Tokens("foo");
  ASSERT_EQ(toks.size(), 1u);
  EXPECT_EQ(toks[0].first, K::CAtom);
  EXPECT_EQ(toks[0].second, "foo");
}

TEST(ECSLLexerTest, IntegerLiteralIsAtom) {
  auto toks = Tokens("42");
  ASSERT_EQ(toks.size(), 1u);
  EXPECT_EQ(toks[0].first, K::CAtom);
  EXPECT_EQ(toks[0].second, "42");
}

TEST(ECSLLexerTest, ArithmeticOperatorsAreAtoms) {
  auto toks = Tokens("+ - * / %");
  for (auto &[kind, text] : toks)
    EXPECT_EQ(kind, K::CAtom) << "unexpected kind for '" << text << "'";
}

// ---------------------------------------------------------------------------
// Token text and offset
// ---------------------------------------------------------------------------

TEST(ECSLLexerTest, TokenTextMatchesBody) {
  llvm::SmallVector<ECSLToken> toks;
  llvm::StringRef body = "requires x > 0;";
  ECSLLexer lexer(body, SourceLocation{});
  lexer.Lex(toks);

  // Verify m_text is a slice of the original body at the right offset.
  for (auto &t : toks) {
    if (t.m_kind == K::Eof)
      continue;
    EXPECT_EQ(body.substr(t.m_offset, t.m_text.size()), t.m_text);
  }
}

TEST(ECSLLexerTest, OffsetsAreMonotonicallyIncreasing) {
  llvm::SmallVector<ECSLToken> toks;
  ECSLLexer lexer("requires x > 0;", SourceLocation{});
  lexer.Lex(toks);

  for (size_t i = 1; i < toks.size(); ++i)
    EXPECT_GE(toks[i].m_offset, toks[i - 1].m_offset + toks[i - 1].m_text.size())
        << "token " << i << " overlaps with previous";
}

// ---------------------------------------------------------------------------
// Full clause sequences
// ---------------------------------------------------------------------------

TEST(ECSLLexerTest, RequiresXGtZero) {
  using P = std::pair<K, std::string>;
  auto toks = Tokens("requires x > 0;");
  std::vector<P> expected = {
      {K::KwRequires, "requires"}, {K::CAtom, "x"}, {K::Gt, ">"},
      {K::CAtom, "0"},             {K::Semi, ";"},
  };
  EXPECT_EQ(toks, expected);
}

TEST(ECSLLexerTest, EnsuresResultEqZero) {
  using P = std::pair<K, std::string>;
  auto toks = Tokens("ensures \\result == 0;");
  std::vector<P> expected = {
      {K::KwEnsures, "ensures"},
      {K::BslashResult, "\\result"},
      {K::EqEq, "=="},
      {K::CAtom, "0"},
      {K::Semi, ";"},
  };
  EXPECT_EQ(toks, expected);
}

TEST(ECSLLexerTest, AssignsNothing) {
  using P = std::pair<K, std::string>;
  auto toks = Tokens("assigns \\nothing;");
  std::vector<P> expected = {
      {K::KwAssigns, "assigns"},
      {K::BslashNothing, "\\nothing"},
      {K::Semi, ";"},
  };
  EXPECT_EQ(toks, expected);
}

TEST(ECSLLexerTest, LogicalConnectives) {
  using P = std::pair<K, std::string>;
  auto toks = Tokens("requires x > 0 && y < 5;");
  std::vector<P> expected = {
      {K::KwRequires, "requires"}, {K::CAtom, "x"},    {K::Gt, ">"},
      {K::CAtom, "0"},             {K::Amp2, "&&"},     {K::CAtom, "y"},
      {K::Lt, "<"},                {K::CAtom, "5"},     {K::Semi, ";"},
  };
  EXPECT_EQ(toks, expected);
}

TEST(ECSLLexerTest, NegationBang) {
  using P = std::pair<K, std::string>;
  auto toks = Tokens("requires !flag;");
  std::vector<P> expected = {
      {K::KwRequires, "requires"},
      {K::Bang, "!"},
      {K::CAtom, "flag"},
      {K::Semi, ";"},
  };
  EXPECT_EQ(toks, expected);
}

TEST(ECSLLexerTest, MultipleClauses) {
  auto kinds = Kinds("requires x > 0; ensures \\result > 0; assigns \\nothing;");
  // Count clause keywords.
  int req = 0, ens = 0, asgn = 0;
  for (auto k : kinds) {
    if (k == K::KwRequires)
      ++req;
    if (k == K::KwEnsures)
      ++ens;
    if (k == K::KwAssigns)
      ++asgn;
  }
  EXPECT_EQ(req, 1);
  EXPECT_EQ(ens, 1);
  EXPECT_EQ(asgn, 1);
}

TEST(ECSLLexerTest, ArithmeticInTerm) {
  // "+", "-", "*" are C atoms; relational op is ECSL.
  auto toks = Tokens("requires x + y * z > 0;");
  EXPECT_EQ(toks[0].first, K::KwRequires);
  // Verify + and * are atoms.
  bool found_plus = false, found_star = false;
  for (auto &[kind, text] : toks) {
    if (kind == K::CAtom && text == "+")
      found_plus = true;
    if (kind == K::CAtom && text == "*")
      found_star = true;
  }
  EXPECT_TRUE(found_plus);
  EXPECT_TRUE(found_star);
}

} // namespace
