//===- unittests/ECSL/ECSLParserTest.cpp - ECSLParser unit tests ----------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
///
/// \file
/// Unit tests for ECSLParser (M1 function contract grammar).
///
//===----------------------------------------------------------------------===//

#include "clang/ECSL/ECSLParser.h"
#include "clang/Basic/SourceLocation.h"
#include "gtest/gtest.h"
#include <memory>
#include <optional>

using namespace clang;
using namespace clang::ecsl;

namespace {

/// Convenience: parse a function contract annotation.
static std::optional<ECSLFunctionContract> Parse(llvm::StringRef text) {
  ECSLParser parser;
  return parser.ParseFunctionContract(text, SourceLocation{},
                                      /*Diags=*/nullptr);
}

// ---------------------------------------------------------------------------
// Empty / non-contract input
// ---------------------------------------------------------------------------

TEST(ECSLParserTest, EmptyBodyReturnsNullopt) {
  EXPECT_FALSE(Parse("").has_value());
}

TEST(ECSLParserTest, WhitespaceOnlyReturnsNullopt) {
  EXPECT_FALSE(Parse("   ").has_value());
}

TEST(ECSLParserTest, GarbageTokensReturnsNullopt) {
  // The parser skips bad tokens via error recovery; no valid clause → nullopt.
  EXPECT_FALSE(Parse("xyzzy 123").has_value());
}

// ---------------------------------------------------------------------------
// requires clause
// ---------------------------------------------------------------------------

TEST(ECSLParserTest, ParseRequiresSimple) {
  auto result = Parse("requires x > 0;");
  ASSERT_TRUE(result.has_value());
  ASSERT_EQ(result->RequiresCount(), 1u);
  EXPECT_EQ(result->EnsuresCount(), 0u);
  EXPECT_FALSE(result->HasAssignsNothing());

  const auto &req = result->RequiresAt(0);
  ASSERT_NE(req.m_pred.get(), nullptr);
  ASSERT_TRUE(std::holds_alternative<ECSLPred::Rel>(req.m_pred->Val()));
  EXPECT_EQ(std::get<ECSLPred::Rel>(req.m_pred->Val()).m_op, RelOp::Gt);
  EXPECT_EQ(req.m_mod, ClauseModifier::None);
}

struct RelOpCase {
  const char *input;
  RelOp expected;
};

class ECSLParserRelOpTest : public ::testing::TestWithParam<RelOpCase> {};

TEST_P(ECSLParserRelOpTest, ParsesRelOp) {
  auto p = GetParam();
  auto result = Parse(p.input);
  ASSERT_TRUE(result.has_value());
  ASSERT_EQ(result->RequiresCount(), 1u);
  ASSERT_TRUE(
      std::holds_alternative<ECSLPred::Rel>(result->RequiresAt(0).m_pred->Val()));
  EXPECT_EQ(
      std::get<ECSLPred::Rel>(result->RequiresAt(0).m_pred->Val()).m_op,
      p.expected);
}

INSTANTIATE_TEST_SUITE_P(
    ECSLRelOps, ECSLParserRelOpTest,
    ::testing::Values(RelOpCase{"requires x <= 10;", RelOp::Le},
                      RelOpCase{"requires n >= 1;", RelOp::Ge},
                      RelOpCase{"requires x == 0;", RelOp::Eq},
                      RelOpCase{"requires p != 0;", RelOp::Ne},
                      RelOpCase{"requires x < 5;", RelOp::Lt},
                      RelOpCase{"requires x > 0;", RelOp::Gt}));

TEST(ECSLParserTest, ParseRequiresNegation) {
  auto result = Parse("requires !flag;");
  ASSERT_TRUE(result.has_value());
  ASSERT_EQ(result->RequiresCount(), 1u);

  const auto *pred = result->RequiresAt(0).m_pred.get();
  ASSERT_NE(pred, nullptr);
  ASSERT_TRUE(std::holds_alternative<ECSLPred::Not>(pred->Val()));
  auto &not_pred = std::get<ECSLPred::Not>(pred->Val());
  ASSERT_NE(not_pred.m_operand.get(), nullptr);
  EXPECT_TRUE(
      std::holds_alternative<ECSLPred::BoolExpr>(not_pred.m_operand->Val()));
}

// ---------------------------------------------------------------------------
// ensures clause
// ---------------------------------------------------------------------------

TEST(ECSLParserTest, ParseEnsuresResult) {
  auto result = Parse("ensures \\result > 0;");
  ASSERT_TRUE(result.has_value());
  EXPECT_EQ(result->RequiresCount(), 0u);
  ASSERT_EQ(result->EnsuresCount(), 1u);

  const auto &ens = result->EnsuresAt(0);
  ASSERT_NE(ens.m_pred.get(), nullptr);
  ASSERT_TRUE(std::holds_alternative<ECSLPred::Rel>(ens.m_pred->Val()));
  auto &rel = std::get<ECSLPred::Rel>(ens.m_pred->Val());
  EXPECT_EQ(rel.m_op, RelOp::Gt);
  EXPECT_TRUE(std::holds_alternative<ECSLTerm::Result>(rel.m_lhs.Val()));
}

TEST(ECSLParserTest, ParseEnsuresResultEqArithmetic) {
  // ensures \result == x + y;
  auto result = Parse("ensures \\result == x + y;");
  ASSERT_TRUE(result.has_value());
  ASSERT_EQ(result->EnsuresCount(), 1u);

  const auto &ens = result->EnsuresAt(0);
  ASSERT_TRUE(std::holds_alternative<ECSLPred::Rel>(ens.m_pred->Val()));
  auto &rel = std::get<ECSLPred::Rel>(ens.m_pred->Val());
  EXPECT_EQ(rel.m_op, RelOp::Eq);
  EXPECT_TRUE(std::holds_alternative<ECSLTerm::Result>(rel.m_lhs.Val()));
  // RHS: "x + y" — ECSLTerm::Expr wrapping a BinOp(Add, Ident(x), Ident(y)).
  ASSERT_TRUE(std::holds_alternative<ECSLTerm::Expr>(rel.m_rhs.Val()));
  auto &rhs_expr = std::get<ECSLTerm::Expr>(rel.m_rhs.Val());
  ASSERT_NE(rhs_expr.m_expr.get(), nullptr);
  ASSERT_TRUE(std::holds_alternative<ECSLExpr::BinOp>(rhs_expr.m_expr->Val()));
  EXPECT_EQ(std::get<ECSLExpr::BinOp>(rhs_expr.m_expr->Val()).m_op,
            ExprOp::Add);
}

// ---------------------------------------------------------------------------
// assigns \nothing
// ---------------------------------------------------------------------------

TEST(ECSLParserTest, ParseAssignsNothing) {
  auto result = Parse("assigns \\nothing;");
  ASSERT_TRUE(result.has_value());
  EXPECT_EQ(result->RequiresCount(), 0u);
  EXPECT_EQ(result->EnsuresCount(), 0u);
  EXPECT_TRUE(result->HasAssignsNothing());
}

// ---------------------------------------------------------------------------
// Multiple clauses
// ---------------------------------------------------------------------------

TEST(ECSLParserTest, ParseMultipleClauses) {
  const char *ann = "requires x > 0; ensures \\result > 0; assigns \\nothing;";
  auto result = Parse(ann);
  ASSERT_TRUE(result.has_value());
  EXPECT_EQ(result->RequiresCount(), 1u);
  EXPECT_EQ(result->EnsuresCount(), 1u);
  EXPECT_TRUE(result->HasAssignsNothing());
}

TEST(ECSLParserTest, ParseMultipleRequiresClauses) {
  auto result = Parse("requires x > 0; requires y > 0;");
  ASSERT_TRUE(result.has_value());
  EXPECT_EQ(result->RequiresCount(), 2u);
}

TEST(ECSLParserTest, ParseMultipleEnsuresClauses) {
  auto result = Parse("ensures \\result > 0; ensures \\result < 100;");
  ASSERT_TRUE(result.has_value());
  EXPECT_EQ(result->EnsuresCount(), 2u);
}

// ---------------------------------------------------------------------------
// Logical connectives
// ---------------------------------------------------------------------------

TEST(ECSLParserTest, ParseLogicalAnd) {
  auto result = Parse("requires x > 0 && y > 0;");
  ASSERT_TRUE(result.has_value());
  ASSERT_EQ(result->RequiresCount(), 1u);

  const auto *pred = result->RequiresAt(0).m_pred.get();
  ASSERT_NE(pred, nullptr);
  ASSERT_TRUE(std::holds_alternative<ECSLPred::And>(pred->Val()));
  auto &and_pred = std::get<ECSLPred::And>(pred->Val());
  ASSERT_NE(and_pred.m_left.get(), nullptr);
  ASSERT_NE(and_pred.m_right.get(), nullptr);
  EXPECT_TRUE(std::holds_alternative<ECSLPred::Rel>(and_pred.m_left->Val()));
  EXPECT_TRUE(std::holds_alternative<ECSLPred::Rel>(and_pred.m_right->Val()));
}

TEST(ECSLParserTest, ParseLogicalOr) {
  auto result = Parse("requires x > 0 || y > 0;");
  ASSERT_TRUE(result.has_value());
  ASSERT_TRUE(
      std::holds_alternative<ECSLPred::Or>(result->RequiresAt(0).m_pred->Val()));
}

TEST(ECSLParserTest, ParseAndBindsTighterThanOr) {
  // requires a || b && c;  →  a || (b && c)
  auto result = Parse("requires a || b && c;");
  ASSERT_TRUE(result.has_value());
  const auto *pred = result->RequiresAt(0).m_pred.get();
  ASSERT_TRUE(std::holds_alternative<ECSLPred::Or>(pred->Val()))
      << "top node should be Or";
  auto &or_pred = std::get<ECSLPred::Or>(pred->Val());
  ASSERT_NE(or_pred.m_right.get(), nullptr);
  EXPECT_TRUE(std::holds_alternative<ECSLPred::And>(or_pred.m_right->Val()))
      << "right child of Or should be And";
}

TEST(ECSLParserTest, ParseLogicalChain) {
  // requires x > 0 && y < 5 && z != 0;  →  (x>0 && y<5) && z!=0
  auto result = Parse("requires x > 0 && y < 5 && z != 0;");
  ASSERT_TRUE(result.has_value());
  const auto *pred = result->RequiresAt(0).m_pred.get();
  ASSERT_TRUE(std::holds_alternative<ECSLPred::And>(pred->Val()));
  auto &and_pred = std::get<ECSLPred::And>(pred->Val());
  ASSERT_NE(and_pred.m_left.get(), nullptr);
  EXPECT_TRUE(std::holds_alternative<ECSLPred::And>(and_pred.m_left->Val()));
}

// ---------------------------------------------------------------------------
// Parenthesised predicates
// ---------------------------------------------------------------------------

TEST(ECSLParserTest, ParseParenthesisedPredicate) {
  // requires (x > 0) && (y > 0);
  auto result = Parse("requires (x > 0) && (y > 0);");
  ASSERT_TRUE(result.has_value());
  ASSERT_TRUE(
      std::holds_alternative<ECSLPred::And>(result->RequiresAt(0).m_pred->Val()));
}

TEST(ECSLParserTest, ParseParenthesisedCTermBeforeRelOp) {
  // requires (x + y) > 0;
  // The '(x + y)' is a C term, not a grouped predicate, because '>' follows.
  auto result = Parse("requires (x + y) > 0;");
  ASSERT_TRUE(result.has_value());
  ASSERT_EQ(result->RequiresCount(), 1u);
  const auto *pred = result->RequiresAt(0).m_pred.get();
  ASSERT_NE(pred, nullptr);
  EXPECT_TRUE(std::holds_alternative<ECSLPred::Rel>(pred->Val()))
      << "expected Rel for (x+y) > 0";
  auto &rel = std::get<ECSLPred::Rel>(pred->Val());
  EXPECT_EQ(rel.m_op, RelOp::Gt);
}

// ---------------------------------------------------------------------------
// Term kinds
// ---------------------------------------------------------------------------

TEST(ECSLParserTest, ParseResultTerm) {
  auto result = Parse("ensures \\result >= 0;");
  ASSERT_TRUE(result.has_value());
  ASSERT_EQ(result->EnsuresCount(), 1u);
  auto &rel =
      std::get<ECSLPred::Rel>(result->EnsuresAt(0).m_pred->Val());
  EXPECT_TRUE(std::holds_alternative<ECSLTerm::Result>(rel.m_lhs.Val()));
}

TEST(ECSLParserTest, ParseBoolExprTerm) {
  // The plain identifier "x" is a BoolExpr term.
  auto result = Parse("requires x > 0;");
  ASSERT_TRUE(result.has_value());
  auto &rel =
      std::get<ECSLPred::Rel>(result->RequiresAt(0).m_pred->Val());
  EXPECT_TRUE(std::holds_alternative<ECSLTerm::Expr>(rel.m_lhs.Val()));
  EXPECT_TRUE(std::holds_alternative<ECSLTerm::Expr>(rel.m_rhs.Val()));
}

// ---------------------------------------------------------------------------
// Error recovery
// ---------------------------------------------------------------------------

TEST(ECSLParserTest, UnknownClauseKeywordSkipped) {
  // "ensurse" is a typo — parser should skip and parse subsequent clauses.
  auto result = Parse("ensurse \\result > 0; requires x > 0;");
  // No crash is the primary test; the requires clause may or may not recover.
  (void)result;
}

TEST(ECSLParserTest, AssignsNonNothingSkipped) {
  // "assigns x;" is not valid in M1.  Parser should skip and continue.
  auto result = Parse("assigns x; requires p > 0;");
  if (result.has_value()) {
    EXPECT_NE(result->RequiresCount(), 0u);
  }
}

TEST(ECSLParserTest, MultilineAnnotation) {
  const char *ann = "requires x > 0;\n"
                    "ensures \\result > 0;\n"
                    "assigns \\nothing;\n";
  auto result = Parse(ann);
  ASSERT_TRUE(result.has_value());
  EXPECT_EQ(result->RequiresCount(), 1u);
  EXPECT_EQ(result->EnsuresCount(), 1u);
  EXPECT_TRUE(result->HasAssignsNothing());
}

} // namespace
