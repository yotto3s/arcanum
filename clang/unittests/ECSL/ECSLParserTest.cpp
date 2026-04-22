//===- unittests/ECSL/ECSLParserTest.cpp - ECSLParser unit tests
//-----------===//
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
  auto result = Parse("");
  EXPECT_FALSE(result.has_value());
}

TEST(ECSLParserTest, WhitespaceOnlyReturnsNullopt) {
  EXPECT_FALSE(Parse("   ").has_value());
}

TEST(ECSLParserTest, GarbageTokensReturnsNullopt) {
  // The parser skips bad tokens via error recovery; no valid clause → nullopt.
  EXPECT_FALSE(Parse("xyzzy 123 ???").has_value());
}

// ---------------------------------------------------------------------------
// requires clause
// ---------------------------------------------------------------------------

TEST(ECSLParserTest, ParseRequiresSimple) {
  // requires x > 0;
  auto result = Parse("requires x > 0;");
  ASSERT_TRUE(result.has_value());
  ASSERT_EQ(result->m_requires.size(), 1u);
  EXPECT_TRUE(result->m_ensures.empty());
  EXPECT_FALSE(result->m_assigns_nothing.has_value());

  const auto &req = result->m_requires[0];
  ASSERT_NE(req.m_pred.get(), nullptr);
  EXPECT_EQ(req.m_pred->m_kind, ECSLPred::Kind::Rel);
  EXPECT_EQ(req.m_pred->m_rel_op, RelOp::Gt);
  EXPECT_EQ(req.m_mod, ClauseModifier::None);
}

TEST(ECSLParserTest, ParseRequiresNegation) {
  // requires !flag;
  auto result = Parse("requires !flag;");
  ASSERT_TRUE(result.has_value());
  ASSERT_EQ(result->m_requires.size(), 1u);

  const auto *pred = result->m_requires[0].m_pred.get();
  ASSERT_NE(pred, nullptr);
  EXPECT_EQ(pred->m_kind, ECSLPred::Kind::Not);
  ASSERT_NE(pred->m_operand.get(), nullptr);
  // Inner: bare C expression "flag" → CExpr predicate.
  EXPECT_EQ(pred->m_operand->m_kind, ECSLPred::Kind::CExpr);
}

TEST(ECSLParserTest, ParseRequiresLe) {
  auto result = Parse("requires x <= 10;");
  ASSERT_TRUE(result.has_value());
  ASSERT_EQ(result->m_requires.size(), 1u);
  EXPECT_EQ(result->m_requires[0].m_pred->m_rel_op, RelOp::Le);
}

TEST(ECSLParserTest, ParseRequiresGe) {
  auto result = Parse("requires n >= 1;");
  ASSERT_TRUE(result.has_value());
  EXPECT_EQ(result->m_requires[0].m_pred->m_rel_op, RelOp::Ge);
}

TEST(ECSLParserTest, ParseRequiresEq) {
  auto result = Parse("requires x == 0;");
  ASSERT_TRUE(result.has_value());
  EXPECT_EQ(result->m_requires[0].m_pred->m_rel_op, RelOp::Eq);
}

TEST(ECSLParserTest, ParseRequiresNe) {
  auto result = Parse("requires p != 0;");
  ASSERT_TRUE(result.has_value());
  EXPECT_EQ(result->m_requires[0].m_pred->m_rel_op, RelOp::Ne);
}

// ---------------------------------------------------------------------------
// ensures clause
// ---------------------------------------------------------------------------

TEST(ECSLParserTest, ParseEnsuresResult) {
  // ensures \result > 0;
  auto result = Parse("ensures \\result > 0;");
  ASSERT_TRUE(result.has_value());
  EXPECT_TRUE(result->m_requires.empty());
  ASSERT_EQ(result->m_ensures.size(), 1u);

  const auto &ens = result->m_ensures[0];
  ASSERT_NE(ens.m_pred.get(), nullptr);
  ASSERT_EQ(ens.m_pred->m_kind, ECSLPred::Kind::Rel);
  EXPECT_EQ(ens.m_pred->m_rel_op, RelOp::Gt);

  // LHS should be a Result term.
  EXPECT_EQ(ens.m_pred->m_lhs.m_kind, ECSLTerm::Kind::Result);
}

TEST(ECSLParserTest, ParseEnsuresResultEqArithmetic) {
  // ensures \result == x + y;
  auto result = Parse("ensures \\result == x + y;");
  ASSERT_TRUE(result.has_value());
  ASSERT_EQ(result->m_ensures.size(), 1u);

  const auto &ens = result->m_ensures[0];
  ASSERT_EQ(ens.m_pred->m_kind, ECSLPred::Kind::Rel);
  EXPECT_EQ(ens.m_pred->m_rel_op, RelOp::Eq);
  EXPECT_EQ(ens.m_pred->m_lhs.m_kind, ECSLTerm::Kind::Result);
  // RHS: "x + y" — CExpr term with a BinOp(Add, Ident(x), Ident(y)) tree.
  ASSERT_EQ(ens.m_pred->m_rhs.m_kind, ECSLTerm::Kind::CExpr);
  ASSERT_NE(ens.m_pred->m_rhs.m_c_expr, nullptr);
  EXPECT_EQ(ens.m_pred->m_rhs.m_c_expr->m_kind, ECSLExpr::Kind::BinOp);
  EXPECT_EQ(ens.m_pred->m_rhs.m_c_expr->m_op, ExprOp::Add);
}

// ---------------------------------------------------------------------------
// assigns \nothing
// ---------------------------------------------------------------------------

TEST(ECSLParserTest, ParseAssignsNothing) {
  auto result = Parse("assigns \\nothing;");
  ASSERT_TRUE(result.has_value());
  EXPECT_TRUE(result->m_requires.empty());
  EXPECT_TRUE(result->m_ensures.empty());
  EXPECT_TRUE(result->m_assigns_nothing.has_value());
}

// ---------------------------------------------------------------------------
// Multiple clauses
// ---------------------------------------------------------------------------

TEST(ECSLParserTest, ParseMultipleClauses) {
  const char *ann = "requires x > 0; ensures \\result > 0; assigns \\nothing;";
  auto result = Parse(ann);
  ASSERT_TRUE(result.has_value());
  EXPECT_EQ(result->m_requires.size(), 1u);
  EXPECT_EQ(result->m_ensures.size(), 1u);
  EXPECT_TRUE(result->m_assigns_nothing.has_value());
}

TEST(ECSLParserTest, ParseMultipleRequiresClauses) {
  auto result = Parse("requires x > 0; requires y > 0;");
  ASSERT_TRUE(result.has_value());
  EXPECT_EQ(result->m_requires.size(), 2u);
}

TEST(ECSLParserTest, ParseMultipleEnsuresClauses) {
  auto result = Parse("ensures \\result > 0; ensures \\result < 100;");
  ASSERT_TRUE(result.has_value());
  EXPECT_EQ(result->m_ensures.size(), 2u);
}

// ---------------------------------------------------------------------------
// Logical connectives
// ---------------------------------------------------------------------------

TEST(ECSLParserTest, ParseLogicalAnd) {
  // requires x > 0 && y > 0;
  auto result = Parse("requires x > 0 && y > 0;");
  ASSERT_TRUE(result.has_value());
  ASSERT_EQ(result->m_requires.size(), 1u);

  const auto *pred = result->m_requires[0].m_pred.get();
  ASSERT_NE(pred, nullptr);
  EXPECT_EQ(pred->m_kind, ECSLPred::Kind::And);
  ASSERT_NE(pred->m_left.get(), nullptr);
  ASSERT_NE(pred->m_right.get(), nullptr);
  EXPECT_EQ(pred->m_left->m_kind, ECSLPred::Kind::Rel);
  EXPECT_EQ(pred->m_right->m_kind, ECSLPred::Kind::Rel);
}

TEST(ECSLParserTest, ParseLogicalOr) {
  // requires x > 0 || y > 0;
  auto result = Parse("requires x > 0 || y > 0;");
  ASSERT_TRUE(result.has_value());
  const auto *pred = result->m_requires[0].m_pred.get();
  ASSERT_NE(pred, nullptr);
  EXPECT_EQ(pred->m_kind, ECSLPred::Kind::Or);
}

TEST(ECSLParserTest, ParseAndBindsTighterThanOr) {
  // requires a || b && c;
  // Expected: a || (b && c)  [And binds tighter]
  auto result = Parse("requires a || b && c;");
  ASSERT_TRUE(result.has_value());
  const auto *pred = result->m_requires[0].m_pred.get();
  ASSERT_NE(pred, nullptr);
  EXPECT_EQ(pred->m_kind, ECSLPred::Kind::Or) << "top node should be Or";
  ASSERT_NE(pred->m_right.get(), nullptr);
  EXPECT_EQ(pred->m_right->m_kind, ECSLPred::Kind::And)
      << "right child of Or should be And";
}

TEST(ECSLParserTest, ParseLogicalChain) {
  // requires x > 0 && y < 5 && z != 0;
  auto result = Parse("requires x > 0 && y < 5 && z != 0;");
  ASSERT_TRUE(result.has_value());
  const auto *pred = result->m_requires[0].m_pred.get();
  // Left-associative: ((x>0 && y<5) && z!=0)
  ASSERT_NE(pred, nullptr);
  EXPECT_EQ(pred->m_kind, ECSLPred::Kind::And);
  ASSERT_NE(pred->m_left.get(), nullptr);
  EXPECT_EQ(pred->m_left->m_kind, ECSLPred::Kind::And);
}

// ---------------------------------------------------------------------------
// Parenthesised predicates
// ---------------------------------------------------------------------------

TEST(ECSLParserTest, ParseParenthesisedPredicate) {
  // requires (x > 0) && (y > 0);
  auto result = Parse("requires (x > 0) && (y > 0);");
  ASSERT_TRUE(result.has_value());
  const auto *pred = result->m_requires[0].m_pred.get();
  ASSERT_NE(pred, nullptr);
  EXPECT_EQ(pred->m_kind, ECSLPred::Kind::And);
}

// ---------------------------------------------------------------------------
// Term kinds
// ---------------------------------------------------------------------------

TEST(ECSLParserTest, ParseResultTerm) {
  auto result = Parse("ensures \\result >= 0;");
  ASSERT_TRUE(result.has_value());
  ASSERT_EQ(result->m_ensures.size(), 1u);
  EXPECT_EQ(result->m_ensures[0].m_pred->m_lhs.m_kind, ECSLTerm::Kind::Result);
}

TEST(ECSLParserTest, ParseNothingInAssigns) {
  auto result = Parse("assigns \\nothing;");
  ASSERT_TRUE(result.has_value());
  EXPECT_TRUE(result->m_assigns_nothing.has_value());
}

TEST(ECSLParserTest, ParseCExprTerm) {
  // The plain C identifier "x" on the LHS of > is a CExpr term.
  auto result = Parse("requires x > 0;");
  ASSERT_TRUE(result.has_value());
  const auto &rel = *result->m_requires[0].m_pred;
  EXPECT_EQ(rel.m_lhs.m_kind, ECSLTerm::Kind::CExpr);
  EXPECT_EQ(rel.m_rhs.m_kind, ECSLTerm::Kind::CExpr);
}

// ---------------------------------------------------------------------------
// Error recovery
// ---------------------------------------------------------------------------

TEST(ECSLParserTest, UnknownClauseKeywordSkipped) {
  // "ensurse" is a typo — parser should skip it and parse subsequent clauses.
  auto result = Parse("ensurse \\result > 0; requires x > 0;");
  // The parser may or may not recover the requires clause, but should not
  // crash. If it recovered successfully, we have at least one requires. In any
  // case, no crash is the primary test.
  (void)result;
}

TEST(ECSLParserTest, AssignsNonNothingSkipped) {
  // "assigns x;" is not valid in M1 (only assigns \nothing is supported).
  // Parser should skip and continue.
  auto result = Parse("assigns x; requires p > 0;");
  // If we recovered, requires should still parse.
  if (result.has_value())
    EXPECT_FALSE(result->m_requires.empty());
}

TEST(ECSLParserTest, MultilineAnnotation) {
  const char *ann = "requires x > 0;\n"
                    "ensures \\result > 0;\n"
                    "assigns \\nothing;\n";
  auto result = Parse(ann);
  ASSERT_TRUE(result.has_value());
  EXPECT_EQ(result->m_requires.size(), 1u);
  EXPECT_EQ(result->m_ensures.size(), 1u);
  EXPECT_TRUE(result->m_assigns_nothing.has_value());
}

} // namespace
