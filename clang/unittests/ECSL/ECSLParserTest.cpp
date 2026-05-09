//===- unittests/ECSL/ECSLParserTest.cpp - ECSLParser unit tests ----------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
///
/// \file
/// Unit tests for ECSLParser.
///
//===----------------------------------------------------------------------===//

#include "clang/ECSL/ECSLParser.h"
#include "clang/Basic/Diagnostic.h"
#include "clang/Basic/DiagnosticECSL.h"
#include "clang/Basic/DiagnosticIDs.h"
#include "clang/Basic/DiagnosticOptions.h"
#include "clang/Basic/SourceLocation.h"
#include "gtest/gtest.h"
#include <memory>
#include <optional>
#include <vector>

using namespace clang;
using namespace clang::ecsl;

namespace {

// ---------------------------------------------------------------------------
// Diagnostic emission helpers
// ---------------------------------------------------------------------------

/// Records diag IDs emitted through a DiagnosticsEngine.
struct RecordingConsumer : public DiagnosticConsumer {
  std::vector<unsigned> m_ids;

  void HandleDiagnostic(DiagnosticsEngine::Level,
                        const Diagnostic &info) override {
    m_ids.push_back(info.getID());
  }
};

/// Convenience: parse \p text with a live DiagnosticsEngine and return the
/// recorded diagnostic IDs.
static std::vector<unsigned> ParseWithDiags(llvm::StringRef text) {
  auto consumer = std::make_unique<RecordingConsumer>();
  RecordingConsumer *consumer_ptr = consumer.get();

  IntrusiveRefCntPtr<DiagnosticIDs> diag_id = DiagnosticIDs::create();
  DiagnosticOptions diag_opts;
  DiagnosticsEngine diags(diag_id, diag_opts, consumer.release());

  ECSLParser parser;
  parser.ParseFunctionContract(text, SourceLocation::getFromRawEncoding(1),
                               &diags);
  return consumer_ptr->m_ids;
}

// ---------------------------------------------------------------------------

/// Convenience: parse a function contract annotation.
static std::optional<ECSLFunctionContract> Parse(llvm::StringRef text) {
  ECSLParser parser;
  // Use a synthetic valid SourceLocation; ECSLExprParser::LocOf asserts
  // base_loc.isValid(), so SourceLocation{} would crash in assert builds.
  return parser.ParseFunctionContract(text,
                                      SourceLocation::getFromRawEncoding(1),
                                      /*Diags=*/nullptr);
}

/// Test fixture for ECSLParser contract tests.
///
/// Contains no state; exists so TEST_F tests share the same test-suite name.
/// All tests call the free-function \c Parse() above, which is the single
/// canonical helper reused by both TEST_F and parameterised TEST_P tests.
class ECSLParserFixture : public ::testing::Test {};

// ---------------------------------------------------------------------------
// Empty / non-contract input
// ---------------------------------------------------------------------------

TEST_F(ECSLParserFixture, EmptyBodyReturnsNullopt) {
  EXPECT_FALSE(Parse("").has_value());
}

TEST_F(ECSLParserFixture, WhitespaceOnlyReturnsNullopt) {
  EXPECT_FALSE(Parse("   ").has_value());
}

TEST_F(ECSLParserFixture, GarbageTokensReturnsNullopt) {
  // The parser skips bad tokens via error recovery; no valid clause → nullopt.
  EXPECT_FALSE(Parse("xyzzy 123").has_value());
}

// ---------------------------------------------------------------------------
// requires clause
// ---------------------------------------------------------------------------

TEST_F(ECSLParserFixture, ParseRequiresSimple) {
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
  ASSERT_TRUE(std::holds_alternative<ECSLPred::Rel>(
      result->RequiresAt(0).m_pred->Val()));
  EXPECT_EQ(std::get<ECSLPred::Rel>(result->RequiresAt(0).m_pred->Val()).m_op,
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

TEST_F(ECSLParserFixture, ParseRequiresNegation) {
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

TEST_F(ECSLParserFixture, ParseEnsuresResult) {
  auto result = Parse("ensures \\result > 0;");
  ASSERT_TRUE(result.has_value());
  EXPECT_EQ(result->RequiresCount(), 0u);
  ASSERT_EQ(result->EnsuresCount(), 1u);

  const auto &ens = result->EnsuresAt(0);
  ASSERT_NE(ens.m_pred.get(), nullptr);
  ASSERT_TRUE(std::holds_alternative<ECSLPred::Rel>(ens.m_pred->Val()));
  auto &rel = std::get<ECSLPred::Rel>(ens.m_pred->Val());
  EXPECT_EQ(rel.m_op, RelOp::Gt);
  // \result maps to ECSLExpr::Result inside the lhs expression.
  auto *lhs_expr = rel.m_lhs.GetExpr();
  ASSERT_NE(lhs_expr, nullptr);
  EXPECT_TRUE(std::holds_alternative<ECSLExpr::Result>(lhs_expr->Val()));
}

TEST_F(ECSLParserFixture, ParseEnsuresResultEqArithmetic) {
  // ensures \result == x + y;
  auto result = Parse("ensures \\result == x + y;");
  ASSERT_TRUE(result.has_value());
  ASSERT_EQ(result->EnsuresCount(), 1u);

  const auto &ens = result->EnsuresAt(0);
  ASSERT_TRUE(std::holds_alternative<ECSLPred::Rel>(ens.m_pred->Val()));
  auto &rel = std::get<ECSLPred::Rel>(ens.m_pred->Val());
  EXPECT_EQ(rel.m_op, RelOp::Eq);
  // LHS: \result — ECSLExpr::Result.
  auto *lhs_expr = rel.m_lhs.GetExpr();
  ASSERT_NE(lhs_expr, nullptr);
  EXPECT_TRUE(std::holds_alternative<ECSLExpr::Result>(lhs_expr->Val()));
  // RHS: "x + y" — BinOp(Add, Ident(x), Ident(y)).
  auto *rhs_expr = rel.m_rhs.GetExpr();
  ASSERT_NE(rhs_expr, nullptr);
  ASSERT_TRUE(std::holds_alternative<ECSLExpr::BinOp>(rhs_expr->Val()));
  EXPECT_EQ(std::get<ECSLExpr::BinOp>(rhs_expr->Val()).m_op, ExprOp::Add);
}

TEST_F(ECSLParserFixture, ParseEnsuresResultArithmeticLhs) {
  // ensures \result + 1 > 5;
  // LHS is BinOp(Add, Result, IntLit("1")); RHS is IntLit("5").
  auto result = Parse("ensures \\result + 1 > 5;");
  ASSERT_TRUE(result.has_value());
  ASSERT_EQ(result->EnsuresCount(), 1u);

  auto &rel = std::get<ECSLPred::Rel>(result->EnsuresAt(0).m_pred->Val());
  EXPECT_EQ(rel.m_op, RelOp::Gt);
  // LHS: BinOp(Add, Result, IntLit("1"))
  auto *lhs_expr = rel.m_lhs.GetExpr();
  ASSERT_NE(lhs_expr, nullptr);
  ASSERT_TRUE(std::holds_alternative<ECSLExpr::BinOp>(lhs_expr->Val()));
  auto &binop = std::get<ECSLExpr::BinOp>(lhs_expr->Val());
  EXPECT_EQ(binop.m_op, ExprOp::Add);
  ASSERT_NE(binop.m_lhs.get(), nullptr);
  EXPECT_TRUE(std::holds_alternative<ECSLExpr::Result>(binop.m_lhs->Val()));
  ASSERT_NE(binop.m_rhs.get(), nullptr);
  ASSERT_TRUE(std::holds_alternative<ECSLExpr::IntLit>(binop.m_rhs->Val()));
  EXPECT_EQ(std::get<ECSLExpr::IntLit>(binop.m_rhs->Val()).m_val, "1");
  // RHS: IntLit("5")
  auto *rhs_expr = rel.m_rhs.GetExpr();
  ASSERT_NE(rhs_expr, nullptr);
  ASSERT_TRUE(std::holds_alternative<ECSLExpr::IntLit>(rhs_expr->Val()));
  EXPECT_EQ(std::get<ECSLExpr::IntLit>(rhs_expr->Val()).m_val, "5");
}

TEST_F(ECSLParserFixture, ParseEnsuresResultMultiply) {
  // ensures \result * 2 == x;
  auto result = Parse("ensures \\result * 2 == x;");
  ASSERT_TRUE(result.has_value());
  ASSERT_EQ(result->EnsuresCount(), 1u);

  auto &rel = std::get<ECSLPred::Rel>(result->EnsuresAt(0).m_pred->Val());
  EXPECT_EQ(rel.m_op, RelOp::Eq);
  auto *lhs_expr = rel.m_lhs.GetExpr();
  ASSERT_NE(lhs_expr, nullptr);
  ASSERT_TRUE(std::holds_alternative<ECSLExpr::BinOp>(lhs_expr->Val()));
  EXPECT_EQ(std::get<ECSLExpr::BinOp>(lhs_expr->Val()).m_op, ExprOp::Mul);
}

// ---------------------------------------------------------------------------
// assigns \nothing
// ---------------------------------------------------------------------------

TEST_F(ECSLParserFixture, ParseAssignsNothing) {
  auto result = Parse("assigns \\nothing;");
  ASSERT_TRUE(result.has_value());
  EXPECT_EQ(result->RequiresCount(), 0u);
  EXPECT_EQ(result->EnsuresCount(), 0u);
  EXPECT_TRUE(result->HasAssignsNothing());
}

// ---------------------------------------------------------------------------
// Multiple clauses
// ---------------------------------------------------------------------------

TEST_F(ECSLParserFixture, ParseMultipleClauses) {
  const char *ann = "requires x > 0; ensures \\result > 0; assigns \\nothing;";
  auto result = Parse(ann);
  ASSERT_TRUE(result.has_value());
  EXPECT_EQ(result->RequiresCount(), 1u);
  EXPECT_EQ(result->EnsuresCount(), 1u);
  EXPECT_TRUE(result->HasAssignsNothing());
}

TEST_F(ECSLParserFixture, ParseMultipleRequiresClauses) {
  auto result = Parse("requires x > 0; requires y > 0;");
  ASSERT_TRUE(result.has_value());
  EXPECT_EQ(result->RequiresCount(), 2u);
}

TEST_F(ECSLParserFixture, ParseMultipleEnsuresClauses) {
  auto result = Parse("ensures \\result > 0; ensures \\result < 100;");
  ASSERT_TRUE(result.has_value());
  EXPECT_EQ(result->EnsuresCount(), 2u);
}

// ---------------------------------------------------------------------------
// Logical connectives
// ---------------------------------------------------------------------------

TEST_F(ECSLParserFixture, ParseLogicalAnd) {
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

TEST_F(ECSLParserFixture, ParseLogicalOr) {
  auto result = Parse("requires x > 0 || y > 0;");
  ASSERT_TRUE(result.has_value());
  ASSERT_TRUE(std::holds_alternative<ECSLPred::Or>(
      result->RequiresAt(0).m_pred->Val()));
}

TEST_F(ECSLParserFixture, ParseAndBindsTighterThanOr) {
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

TEST_F(ECSLParserFixture, ParseLogicalChain) {
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

TEST_F(ECSLParserFixture, ParseParenthesisedPredicate) {
  // requires (x > 0) && (y > 0);
  auto result = Parse("requires (x > 0) && (y > 0);");
  ASSERT_TRUE(result.has_value());
  ASSERT_TRUE(std::holds_alternative<ECSLPred::And>(
      result->RequiresAt(0).m_pred->Val()));
}

TEST_F(ECSLParserFixture, ParseParenthesisedCTermBeforeRelOp) {
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

TEST_F(ECSLParserFixture, ParseResultTerm) {
  auto result = Parse("ensures \\result >= 0;");
  ASSERT_TRUE(result.has_value());
  ASSERT_EQ(result->EnsuresCount(), 1u);
  auto &rel = std::get<ECSLPred::Rel>(result->EnsuresAt(0).m_pred->Val());
  auto *lhs_expr = rel.m_lhs.GetExpr();
  ASSERT_NE(lhs_expr, nullptr);
  EXPECT_TRUE(std::holds_alternative<ECSLExpr::Result>(lhs_expr->Val()));
}

TEST_F(ECSLParserFixture, ParseExprTermInRelPred) {
  // The plain identifier "x" and integer "0" are Expr terms in a Rel predicate.
  auto result = Parse("requires x > 0;");
  ASSERT_TRUE(result.has_value());
  auto &rel = std::get<ECSLPred::Rel>(result->RequiresAt(0).m_pred->Val());
  // Both sides carry a valid expression.
  EXPECT_NE(rel.m_lhs.GetExpr(), nullptr);
  EXPECT_NE(rel.m_rhs.GetExpr(), nullptr);
}

TEST_F(ECSLParserFixture, ParseBareBoolExprPred) {
  // "requires !flag;" — the inner "flag" is a bare BoolExpr predicate.
  auto result = Parse("requires !flag;");
  ASSERT_TRUE(result.has_value());
  ASSERT_EQ(result->RequiresCount(), 1u);
  const auto *pred = result->RequiresAt(0).m_pred.get();
  ASSERT_TRUE(std::holds_alternative<ECSLPred::Not>(pred->Val()));
  const auto &not_pred = std::get<ECSLPred::Not>(pred->Val());
  ASSERT_NE(not_pred.m_operand.get(), nullptr);
  EXPECT_TRUE(
      std::holds_alternative<ECSLPred::BoolExpr>(not_pred.m_operand->Val()));
}

// ---------------------------------------------------------------------------
// Error recovery
// ---------------------------------------------------------------------------

TEST_F(ECSLParserFixture, UnknownClauseKeywordSkipped) {
  // "ensurse" is a typo — parser should skip it and recover to the later
  // valid requires clause.
  auto result = Parse("ensurse \\result > 0; requires x > 0;");
  ASSERT_TRUE(result.has_value());
  EXPECT_EQ(result->RequiresCount(), 1u);
}

TEST_F(ECSLParserFixture, AssignsNonNothingSkipped) {
  // "assigns x;" is not valid in M1.  Parser should skip and continue,
  // leaving the subsequent requires clause intact.
  auto result = Parse("assigns x; requires p > 0;");
  ASSERT_TRUE(result.has_value());
  EXPECT_EQ(result->RequiresCount(), 1u);
  EXPECT_FALSE(result->HasAssignsNothing());
}

TEST_F(ECSLParserFixture, AssignsMissingSemiRecovery) {
  // "assigns \nothing" without a trailing ';' — recovery calls SkipToSemi(),
  // which consumes everything through the next ';' (including "requires x>0").
  // No valid clause is recovered, so the result is nullopt.
  auto result = Parse("assigns \\nothing requires x > 0;");
  EXPECT_FALSE(result.has_value());
}

TEST_F(ECSLParserFixture, RelNullRhsRecovery) {
  // "x > ;" — the RHS term is empty, so ParseCTerm returns a null-expr Expr.
  // ParseComparison must propagate the failure; ParseRequiresClause skips to
  // the next ';' and the assigns clause is still parsed.
  auto result = Parse("requires x > ; assigns \\nothing;");
  ASSERT_TRUE(result.has_value());
  EXPECT_EQ(result->RequiresCount(), 0u);
  EXPECT_TRUE(result->HasAssignsNothing());
}

TEST_F(ECSLParserFixture, MissingClosingParenReturnsNullopt) {
  // "(x > 0" without closing ')' — ParseAtom emits an error and returns
  // nullptr, propagating failure so the clause is not accepted.
  auto result = Parse("requires (x > 0;");
  EXPECT_FALSE(result.has_value());
}

TEST_F(ECSLParserFixture, MultilineAnnotation) {
  const char *ann = "requires x > 0;\n"
                    "ensures \\result > 0;\n"
                    "assigns \\nothing;\n";
  auto result = Parse(ann);
  ASSERT_TRUE(result.has_value());
  EXPECT_EQ(result->RequiresCount(), 1u);
  EXPECT_EQ(result->EnsuresCount(), 1u);
  EXPECT_TRUE(result->HasAssignsNothing());
}

// ---------------------------------------------------------------------------
// Diagnostic emission
// ---------------------------------------------------------------------------

TEST_F(ECSLParserFixture, DiagMissingClosingParen) {
  auto ids = ParseWithDiags("requires (x > 0;");
  ASSERT_EQ(ids.size(), 1u);
  EXPECT_EQ(ids[0], (unsigned)diag::err_ecsl_expected_rparen);
}

TEST_F(ECSLParserFixture, DiagMissingSemiAfterRequires) {
  auto ids = ParseWithDiags("requires x > 0");
  ASSERT_FALSE(ids.empty());
  EXPECT_EQ(ids[0], (unsigned)diag::err_ecsl_expected_semi_after_requires);
}

TEST_F(ECSLParserFixture, DiagMissingSemiAfterEnsures) {
  auto ids = ParseWithDiags("ensures \\result > 0");
  ASSERT_FALSE(ids.empty());
  EXPECT_EQ(ids[0], (unsigned)diag::err_ecsl_expected_semi_after_ensures);
}

TEST_F(ECSLParserFixture, DiagAssignsNonNothing) {
  auto ids = ParseWithDiags("assigns x;");
  ASSERT_FALSE(ids.empty());
  EXPECT_EQ(ids[0], (unsigned)diag::err_ecsl_assigns_not_nothing);
}

TEST_F(ECSLParserFixture, DiagMissingSemiAfterAssignsNothing) {
  auto ids = ParseWithDiags("assigns \\nothing");
  ASSERT_FALSE(ids.empty());
  EXPECT_EQ(ids[0], (unsigned)diag::err_ecsl_expected_semi_after_assigns);
}

TEST_F(ECSLParserFixture, DiagUnexpectedClauseToken) {
  auto ids = ParseWithDiags("unknown x > 0;");
  ASSERT_FALSE(ids.empty());
  EXPECT_EQ(ids[0], (unsigned)diag::err_ecsl_unexpected_clause_token);
}

} // namespace
