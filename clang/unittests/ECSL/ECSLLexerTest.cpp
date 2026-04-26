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
#include "gtest/gtest.h"
#include <algorithm>
#include <string>
#include <utility>
#include <vector>

using namespace clang;
using namespace clang::ecsl;

namespace {

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

/// Lex \p body and return the resulting token kinds (including the Eof).
static std::vector<ECSLTokenKind> Kinds(llvm::StringRef body) {
  llvm::SmallVector<ECSLToken> toks;
  ECSLLexer lexer(body, SourceLocation{});
  lexer.Lex(toks);
  std::vector<ECSLTokenKind> result;
  for (auto &t : toks) {
    result.push_back(t.m_kind);
  }
  return result;
}

/// Lex \p body and return (kind, text) pairs for every non-Eof token.
static std::vector<std::pair<ECSLTokenKind, std::string>>
Tokens(llvm::StringRef body) {
  llvm::SmallVector<ECSLToken> toks;
  ECSLLexer lexer(body, SourceLocation{});
  lexer.Lex(toks);
  std::vector<std::pair<ECSLTokenKind, std::string>> result;
  for (auto &t : toks) {
    if (t.m_kind != ECSLTokenKind::Eof) {
      result.emplace_back(t.m_kind, t.m_text.str());
    }
  }
  return result;
}

using K = ECSLTokenKind;
using TokenPair = std::pair<K, std::string>;

// ---------------------------------------------------------------------------
// Name helper for parameterized test instances
// ---------------------------------------------------------------------------

static std::string SanitizeName(const char *s) {
  std::string name(s);
  for (char &c : name) {
    if (!std::isalnum(static_cast<unsigned char>(c))) {
      c = '_';
    }
  }
  if (!name.empty() && !std::isalpha(static_cast<unsigned char>(name[0]))) {
    name = "t_" + name;
  }
  return name;
}

// ---------------------------------------------------------------------------
// ECSLSingleTokenTest — bodies that produce exactly one non-Eof token
// ---------------------------------------------------------------------------
//
// Covers: clause keywords, backslash keywords, relational/logical/arithmetic
// operators (including the "not two tokens" property for two-char ops), a plain
// identifier, an unclassified character, and all number literal forms.

struct SingleTokenCase {
  const char *test_name;
  const char *body;
  K kind;
  const char *text;
};

class ECSLSingleTokenTest : public ::testing::TestWithParam<SingleTokenCase> {};

TEST_P(ECSLSingleTokenTest, SingleToken) {
  const auto &p = GetParam();
  auto toks = Tokens(p.body);
  ASSERT_EQ(toks.size(), 1u) << "body: " << p.body;
  EXPECT_EQ(toks[0].first, p.kind);
  EXPECT_EQ(toks[0].second, p.text);
}

INSTANTIATE_TEST_SUITE_P(
    ECSLLexer, ECSLSingleTokenTest,
    ::testing::Values(
        // Clause keywords — always Identifier; parser resolves from context.
        SingleTokenCase{"requires", "requires", K::Identifier, "requires"},
        SingleTokenCase{"ensures", "ensures", K::Identifier, "ensures"},
        SingleTokenCase{"assigns", "assigns", K::Identifier, "assigns"},
        // Backslash keywords.
        SingleTokenCase{"bslash_result", "\\result", K::BslashResult,
                        "\\result"},
        SingleTokenCase{"bslash_nothing", "\\nothing", K::BslashNothing,
                        "\\nothing"},
        SingleTokenCase{"bslash_unknown", "\\vvalid", K::Unknown, "\\vvalid"},
        // Relational operators — size==1 also verifies no split into two
        // tokens.
        SingleTokenCase{"rel_eqeq", "==", K::EqEq, "=="},
        SingleTokenCase{"rel_bangeq", "!=", K::BangEq, "!="},
        SingleTokenCase{"rel_lt", "<", K::Lt, "<"},
        SingleTokenCase{"rel_le", "<=", K::Le, "<="},
        SingleTokenCase{"rel_gt", ">", K::Gt, ">"},
        SingleTokenCase{"rel_ge", ">=", K::Ge, ">="},
        // Logical operators — "&&"/"||" must not split into two tokens.
        SingleTokenCase{"log_ampamp", "&&", K::AmpAmp, "&&"},
        SingleTokenCase{"log_pipepipe", "||", K::PipePipe, "||"},
        SingleTokenCase{"log_bang", "!", K::Bang, "!"},
        SingleTokenCase{"log_amp", "&", K::Amp, "&"},
        SingleTokenCase{"log_pipe", "|", K::Pipe, "|"},
        // Punctuation.
        SingleTokenCase{"semicolon", ";", K::Semicolon, ";"},
        SingleTokenCase{"eq", "=", K::Eq, "="},
        // Identifier and unclassified character.
        SingleTokenCase{"identifier", "foo", K::Identifier, "foo"},
        SingleTokenCase{"unknown_char", "@", K::Unknown, "@"},
        // Number literals — integers and floats.
        SingleTokenCase{"num_integer", "42", K::IntegerLiteral, "42"},
        SingleTokenCase{"num_hex", "0xFF", K::IntegerLiteral, "0xFF"},
        SingleTokenCase{"num_float_exp_pos", "1.5e+10", K::FloatLiteral,
                        "1.5e+10"},
        SingleTokenCase{"num_float_exp_neg", "1.5E-3", K::FloatLiteral,
                        "1.5E-3"},
        SingleTokenCase{"num_int_exp_pos", "1e+10", K::FloatLiteral, "1e+10"},
        SingleTokenCase{"num_int_exp_neg", "1E-3", K::FloatLiteral, "1E-3"},
        SingleTokenCase{"num_float", "3.14", K::FloatLiteral, "3.14"},
        SingleTokenCase{"num_suffix_int", "42u", K::IntegerLiteral, "42u"},
        SingleTokenCase{"num_suffix_float", "3.14f", K::FloatLiteral, "3.14f"},
        SingleTokenCase{"num_separator", "1'000", K::IntegerLiteral, "1'000"},
        SingleTokenCase{"num_hex_float", "0x1p+2", K::FloatLiteral, "0x1p+2"}),
    [](const ::testing::TestParamInfo<SingleTokenCase> &info) {
      return std::string(info.param.test_name);
    });

// ---------------------------------------------------------------------------
// ECSLWhitespaceBodyTest — bodies that produce only an Eof token
// ---------------------------------------------------------------------------

class ECSLWhitespaceBodyTest : public ::testing::TestWithParam<std::string> {};

TEST_P(ECSLWhitespaceBodyTest, ProducesEofOnly) {
  auto kinds = Kinds(GetParam());
  ASSERT_EQ(kinds.size(), 1u);
  EXPECT_EQ(kinds[0], K::Eof);
}

INSTANTIATE_TEST_SUITE_P(
    ECSLLexer, ECSLWhitespaceBodyTest,
    ::testing::Values(std::string(""), std::string("   \t\n\r  "),
                      std::string(3, '\v'), std::string(2, '\f')),
    [](const ::testing::TestParamInfo<std::string> &info) -> std::string {
      switch (info.index) {
      case 0:
        return "empty";
      case 1:
        return "mixed_whitespace";
      case 2:
        return "vertical_tabs_only";
      default:
        return "form_feeds_only";
      }
    });

// ---------------------------------------------------------------------------
// ECSLLexerFixture — shared setup for token-text and offset tests
// ---------------------------------------------------------------------------

class ECSLLexerFixture : public ::testing::Test {
protected:
  void LexBody(llvm::StringRef body) {
    ECSLLexer lexer(body, SourceLocation{});
    lexer.Lex(m_toks);
  }

  llvm::SmallVector<ECSLToken> m_toks;
};

TEST_F(ECSLLexerFixture, TokenTextMatchesBody) {
  llvm::StringRef body = "requires x > 0;";
  LexBody(body);
  for (auto &t : m_toks) {
    if (t.m_kind == K::Eof) {
      continue;
    }
    EXPECT_EQ(body.substr(t.m_offset, t.m_text.size()), t.m_text);
  }
}

TEST_F(ECSLLexerFixture, OffsetsAreMonotonicallyIncreasing) {
  LexBody("requires x > 0;");
  for (size_t i = 1; i < m_toks.size(); ++i) {
    EXPECT_GE(m_toks[i].m_offset,
              m_toks[i - 1].m_offset + m_toks[i - 1].m_text.size())
        << "token " << i << " overlaps with previous";
  }
}

TEST_F(ECSLLexerFixture, SpecificOffsetsAreCorrect) {
  // "requires x > 0;"
  //  0        9 11 1314
  LexBody("requires x > 0;");
  ASSERT_GE(m_toks.size(), 5u);
  EXPECT_EQ(m_toks[0].m_offset, 0u);  // "requires"
  EXPECT_EQ(m_toks[1].m_offset, 9u);  // "x"
  EXPECT_EQ(m_toks[2].m_offset, 11u); // ">"
  EXPECT_EQ(m_toks[3].m_offset, 13u); // "0"
  EXPECT_EQ(m_toks[4].m_offset, 14u); // ";"
}

TEST_F(ECSLLexerFixture, EofTokenHasCorrectFields) {
  llvm::StringRef body = "x > 0";
  LexBody(body);
  ASSERT_FALSE(m_toks.empty());
  const ECSLToken &eof = m_toks.back();
  EXPECT_EQ(eof.m_kind, K::Eof);
  EXPECT_EQ(eof.m_offset, body.size());
  EXPECT_EQ(eof.m_text, "");
}

// ---------------------------------------------------------------------------
// ECSLTokenSequenceTest — full token stream verification
// ---------------------------------------------------------------------------

struct TokenSequenceCase {
  const char *body;
  std::vector<TokenPair> expected;
};

class ECSLTokenSequenceTest
    : public ::testing::TestWithParam<TokenSequenceCase> {};

TEST_P(ECSLTokenSequenceTest, MatchesExpectedSequence) {
  const auto &p = GetParam();
  EXPECT_EQ(Tokens(p.body), p.expected);
}

INSTANTIATE_TEST_SUITE_P(
    ECSLLexer, ECSLTokenSequenceTest,
    ::testing::Values(TokenSequenceCase{"requires x > 0;",
                                        {{K::Identifier, "requires"},
                                         {K::Identifier, "x"},
                                         {K::Gt, ">"},
                                         {K::IntegerLiteral, "0"},
                                         {K::Semicolon, ";"}}},
                      TokenSequenceCase{"ensures \\result == 0;",
                                        {{K::Identifier, "ensures"},
                                         {K::BslashResult, "\\result"},
                                         {K::EqEq, "=="},
                                         {K::IntegerLiteral, "0"},
                                         {K::Semicolon, ";"}}},
                      TokenSequenceCase{"assigns \\nothing;",
                                        {{K::Identifier, "assigns"},
                                         {K::BslashNothing, "\\nothing"},
                                         {K::Semicolon, ";"}}},
                      TokenSequenceCase{"requires x > 0 && y < 5;",
                                        {{K::Identifier, "requires"},
                                         {K::Identifier, "x"},
                                         {K::Gt, ">"},
                                         {K::IntegerLiteral, "0"},
                                         {K::AmpAmp, "&&"},
                                         {K::Identifier, "y"},
                                         {K::Lt, "<"},
                                         {K::IntegerLiteral, "5"},
                                         {K::Semicolon, ";"}}},
                      TokenSequenceCase{"requires !flag;",
                                        {{K::Identifier, "requires"},
                                         {K::Bang, "!"},
                                         {K::Identifier, "flag"},
                                         {K::Semicolon, ";"}}}),
    [](const ::testing::TestParamInfo<TokenSequenceCase> &info) {
      return SanitizeName(info.param.body);
    });

// ---------------------------------------------------------------------------
// Clause keywords — structural tests not covered by single-token suite
// ---------------------------------------------------------------------------

TEST(ECSLLexerTest, ClauseWordsAlwaysIdentifier) {
  // Clause words are plain identifiers regardless of position in annotation.
  auto toks = Tokens("requires ensures > 0;");
  ASSERT_GE(toks.size(), 2u);
  EXPECT_EQ(toks[0].first, K::Identifier);
  EXPECT_EQ(toks[0].second, "requires");
  EXPECT_EQ(toks[1].first, K::Identifier);
  EXPECT_EQ(toks[1].second, "ensures");
}

TEST(ECSLLexerTest, ClauseWordAfterSemicolonIsIdentifier) {
  auto toks = Tokens("requires x > 0; ensures \\result > 0;");
  // Both "requires" and "ensures" are Identifier regardless of position.
  ASSERT_GE(toks.size(), 1u);
  EXPECT_EQ(toks[0].first, K::Identifier);
  EXPECT_EQ(toks[0].second, "requires");
  bool found_ensures = false;
  for (auto &[kind, text] : toks) {
    if (kind == K::Identifier && text == "ensures") {
      found_ensures = true;
    }
  }
  EXPECT_TRUE(found_ensures)
      << "expected Identifier('ensures') in token stream";
}

TEST(ECSLLexerTest, IdentifierWithClauseWordPrefix) {
  // "x_requires" is not a keyword — it is a plain identifier.
  auto toks = Tokens("requires x_requires > 0;");
  ASSERT_GE(toks.size(), 2u);
  EXPECT_EQ(toks[0].first, K::Identifier);
  EXPECT_EQ(toks[1].first, K::Identifier);
  EXPECT_EQ(toks[1].second, "x_requires");
}

// ---------------------------------------------------------------------------
// Backslash keywords — edge cases not covered by single-token suite
// ---------------------------------------------------------------------------

TEST(ECSLLexerTest, LoneBackslashIsUnknown) {
  // A lone '\' with no following word must produce Unknown("\\") + Eof.
  auto kinds = Kinds("\\");
  ASSERT_EQ(kinds.size(), 2u);
  EXPECT_EQ(kinds[0], K::Unknown);
  EXPECT_EQ(kinds[1], K::Eof);
  auto toks = Tokens("\\");
  ASSERT_EQ(toks.size(), 1u);
  EXPECT_EQ(toks[0].second, "\\");
}

// ---------------------------------------------------------------------------
// Punctuation and multi-token operator groups
// ---------------------------------------------------------------------------

TEST(ECSLLexerTest, Parens) {
  auto toks = Tokens("()");
  ASSERT_EQ(toks.size(), 2u);
  EXPECT_EQ(toks[0].first, K::LParen);
  EXPECT_EQ(toks[1].first, K::RParen);
}

TEST(ECSLLexerTest, ArithmeticOperatorsHaveSpecificKinds) {
  auto toks = Tokens("+ - * / %");
  ASSERT_EQ(toks.size(), 5u);
  EXPECT_EQ(toks[0].first, K::Plus);
  EXPECT_EQ(toks[1].first, K::Minus);
  EXPECT_EQ(toks[2].first, K::Star);
  EXPECT_EQ(toks[3].first, K::Slash);
  EXPECT_EQ(toks[4].first, K::Percent);
}

// ---------------------------------------------------------------------------
// Numbers — edge cases not covered by single-token suite
// ---------------------------------------------------------------------------

TEST(ECSLLexerTest, HexIntEndingInEPlusIsNotFloat) {
  // 0x1e is a hex integer; the '+' must NOT be absorbed into the number token.
  auto toks = Tokens("0x1e+1");
  ASSERT_EQ(toks.size(), 3u);
  EXPECT_EQ(toks[0].first, K::IntegerLiteral);
  EXPECT_EQ(toks[0].second, "0x1e");
}

TEST(ECSLLexerTest, LeadingDotFloatIsFloatLiteral) {
  // A decimal point followed by digits is a float literal, not Dot + integer.
  auto toks = Tokens(".5");
  ASSERT_EQ(toks.size(), 1u);
  EXPECT_EQ(toks[0].first, K::FloatLiteral);
  EXPECT_EQ(toks[0].second, ".5");
}

TEST(ECSLLexerTest, LeadingDotFloatWithExponentIsFloatLiteral) {
  auto toks = Tokens(".5e2");
  ASSERT_EQ(toks.size(), 1u);
  EXPECT_EQ(toks[0].first, K::FloatLiteral);
  EXPECT_EQ(toks[0].second, ".5e2");
}

TEST(ECSLLexerTest, LeadingDotFloatWithSuffixIsFloatLiteral) {
  auto toks = Tokens(".5f");
  ASSERT_EQ(toks.size(), 1u);
  EXPECT_EQ(toks[0].first, K::FloatLiteral);
  EXPECT_EQ(toks[0].second, ".5f");
}

// ---------------------------------------------------------------------------
// Complex clause structure tests
// ---------------------------------------------------------------------------

TEST(ECSLLexerTest, MultipleClauses) {
  auto toks =
      Tokens("requires x > 0; ensures \\result > 0; assigns \\nothing;");
  int req = 0, ens = 0, asgn = 0;
  for (auto &[kind, text] : toks) {
    if (kind == K::Identifier && text == "requires") {
      ++req;
    }
    if (kind == K::Identifier && text == "ensures") {
      ++ens;
    }
    if (kind == K::Identifier && text == "assigns") {
      ++asgn;
    }
  }
  EXPECT_EQ(req, 1);
  EXPECT_EQ(ens, 1);
  EXPECT_EQ(asgn, 1);
}

TEST(ECSLLexerTest, ArithmeticInTerm) {
  // "+", "-", "*" are specific arithmetic kinds; relational op is ECSL.
  auto toks = Tokens("requires x + y * z > 0;");
  ASSERT_GE(toks.size(), 1u);
  EXPECT_EQ(toks[0].first, K::Identifier);
  EXPECT_EQ(toks[0].second, "requires");
  // Verify + and * have the correct specific kinds.
  bool found_plus = false, found_star = false;
  for (auto &[kind, text] : toks) {
    if (kind == K::Plus && text == "+") {
      found_plus = true;
    }
    if (kind == K::Star && text == "*") {
      found_star = true;
    }
  }
  EXPECT_TRUE(found_plus);
  EXPECT_TRUE(found_star);
}

// ===========================================================================
// Backslash keyword and whitespace boundary tests
// ===========================================================================

// ---------------------------------------------------------------------------
// Backslash keyword must not consume a trailing digit
// ---------------------------------------------------------------------------

struct BslashDigitCase {
  const char *test_name;
  const char *body;
  K bslash_kind;
  const char *bslash_text;
  const char *digit_text;
};

class ECSLBslashDigitBoundaryTest
    : public ::testing::TestWithParam<BslashDigitCase> {};

TEST_P(ECSLBslashDigitBoundaryTest, SplitsIntoTwoTokens) {
  const auto &p = GetParam();
  auto toks = Tokens(p.body);
  ASSERT_GE(toks.size(), 2u)
      << "expected [bslash_token, IntegerLiteral], got " << toks.size();
  EXPECT_EQ(toks[0].first, p.bslash_kind)
      << "wrong bslash kind for body '" << p.body << "'";
  EXPECT_EQ(toks[0].second, p.bslash_text)
      << "bslash token text must not include trailing digit";
  EXPECT_EQ(toks[1].first, K::IntegerLiteral)
      << "second token should be IntegerLiteral";
  EXPECT_EQ(toks[1].second, p.digit_text) << "wrong digit text";
}

INSTANTIATE_TEST_SUITE_P(
    ECSLBslashDigit, ECSLBslashDigitBoundaryTest,
    ::testing::Values(BslashDigitCase{"bslash_result", "\\result0",
                                      K::BslashResult, "\\result", "0"},
                      BslashDigitCase{"bslash_nothing", "\\nothing1",
                                      K::BslashNothing, "\\nothing", "1"}),
    [](const ::testing::TestParamInfo<BslashDigitCase> &info) {
      return std::string(info.param.test_name);
    });

// ---------------------------------------------------------------------------
// Vertical tab and form feed must be treated as whitespace
// ---------------------------------------------------------------------------

struct F2BeforeCase {
  char ws_char;
  const char *body_suffix;
  const char *first_ident_text;
};

class ECSLF2WhitespaceBeforeKeywordTest
    : public ::testing::TestWithParam<F2BeforeCase> {};

TEST_P(ECSLF2WhitespaceBeforeKeywordTest, FirstTokenIsIdentifier) {
  const auto &p = GetParam();
  const std::string body = std::string(1, p.ws_char) + p.body_suffix;
  auto toks = Tokens(body);
  ASSERT_FALSE(toks.empty());
  EXPECT_EQ(toks[0].first, K::Identifier)
      << "whitespace char before '" << p.first_ident_text
      << "' should be skipped";
  EXPECT_EQ(toks[0].second, p.first_ident_text);
}

INSTANTIATE_TEST_SUITE_P(
    ECSLWhitespaceBefore, ECSLF2WhitespaceBeforeKeywordTest,
    ::testing::Values(F2BeforeCase{'\v', "requires x > 0;", "requires"},
                      F2BeforeCase{'\f', "ensures \\result > 0;", "ensures"}),
    [](const ::testing::TestParamInfo<F2BeforeCase> &info) -> std::string {
      return std::string(info.param.ws_char == '\v' ? "VT" : "FF") + "_" +
             info.param.first_ident_text;
    });

struct F2AfterCase {
  char ws_char;
  const char *prefix;
  const char *suffix;
  const char *expected_ident;
};

class ECSLF2WhitespaceAfterSemicolonTest
    : public ::testing::TestWithParam<F2AfterCase> {};

TEST_P(ECSLF2WhitespaceAfterSemicolonTest, IdentifierIsRecognised) {
  const auto &p = GetParam();
  const std::string body =
      std::string(p.prefix) + std::string(1, p.ws_char) + p.suffix;
  auto toks = Tokens(body);
  bool found = false;
  for (auto &[kind, text] : toks) {
    if (kind == K::Identifier && text == p.expected_ident) {
      found = true;
    }
  }
  EXPECT_TRUE(found) << "whitespace char between ';' and '" << p.expected_ident
                     << "' should be skipped";
}

INSTANTIATE_TEST_SUITE_P(
    ECSLWhitespaceAfter, ECSLF2WhitespaceAfterSemicolonTest,
    ::testing::Values(F2AfterCase{'\v', "requires x > 0;",
                                  "ensures \\result > 0;", "ensures"},
                      F2AfterCase{'\f', "assigns \\nothing;", "requires x > 0;",
                                  "requires"}),
    [](const ::testing::TestParamInfo<F2AfterCase> &info) -> std::string {
      return std::string(info.param.ws_char == '\v' ? "VT" : "FF") + "_" +
             info.param.expected_ident;
    });

} // namespace
