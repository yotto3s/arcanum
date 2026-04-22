//===- unittests/ECSL/ECSLAstTest.cpp - ECSLAst node construction tests ---===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
///
/// \file
/// Unit tests for ECSLAst.h data structures.
///
/// These tests verify that ECSL AST nodes can be constructed, moved, and
/// queried correctly. They do not require Clang compilation infrastructure.
///
//===----------------------------------------------------------------------===//

#include "clang/ECSL/ECSLAst.h"
#include "gtest/gtest.h"

using namespace clang;
using namespace clang::ecsl;

namespace {

// ---------------------------------------------------------------------------
// ECSLTerm
// ---------------------------------------------------------------------------

TEST(ECSLTermTest, MakeExprSetsKindAndExpr) {
  auto expr = ECSLExpr::MakeIntLit("7", SourceRange{});
  ECSLExpr *raw = expr.get();
  ECSLTerm t = ECSLTerm::MakeExpr(std::move(expr), SourceRange{});
  EXPECT_TRUE(std::holds_alternative<ECSLTerm::Expr>(t.m_val));
  EXPECT_EQ(std::get<ECSLTerm::Expr>(t.m_val).m_expr.get(), raw);
}

TEST(ECSLTermTest, MakeResultSetsKind) {
  ECSLTerm t = ECSLTerm::MakeResult(SourceRange{});
  EXPECT_TRUE(std::holds_alternative<ECSLTerm::Result>(t.m_val));
}

TEST(ECSLTermTest, MakeNothingSetsKind) {
  ECSLTerm t = ECSLTerm::MakeNothing(SourceRange{});
  EXPECT_TRUE(std::holds_alternative<ECSLTerm::Nothing>(t.m_val));
}

// ---------------------------------------------------------------------------
// ECSLPred — leaf nodes
// ---------------------------------------------------------------------------

TEST(ECSLPredTest, MakeTrue) {
  auto p = ECSLPred::MakeTrue(SourceRange{});
  ASSERT_NE(p, nullptr);
  EXPECT_TRUE(std::holds_alternative<ECSLPred::True>(p->m_val));
}

TEST(ECSLPredTest, MakeFalse) {
  auto p = ECSLPred::MakeFalse(SourceRange{});
  ASSERT_NE(p, nullptr);
  EXPECT_TRUE(std::holds_alternative<ECSLPred::False>(p->m_val));
}

// ---------------------------------------------------------------------------
// ECSLPred — relational
// ---------------------------------------------------------------------------

TEST(ECSLPredTest, MakeRelSetsFieldsCorrectly) {
  auto expr_a = ECSLExpr::MakeIntLit("10", SourceRange{});
  auto expr_b = ECSLExpr::MakeIntLit("32", SourceRange{});
  ECSLExpr *raw_a = expr_a.get();
  ECSLExpr *raw_b = expr_b.get();

  ECSLTerm lhs = ECSLTerm::MakeExpr(std::move(expr_a), SourceRange{});
  ECSLTerm rhs = ECSLTerm::MakeExpr(std::move(expr_b), SourceRange{});

  auto p = ECSLPred::MakeRel(std::move(lhs), RelOp::Gt, std::move(rhs),
                             SourceRange{});
  ASSERT_NE(p, nullptr);
  ASSERT_TRUE(std::holds_alternative<ECSLPred::Rel>(p->m_val));
  auto &rel = std::get<ECSLPred::Rel>(p->m_val);
  EXPECT_EQ(rel.m_op, RelOp::Gt);
  EXPECT_EQ(std::get<ECSLTerm::Expr>(rel.m_lhs.m_val).m_expr.get(), raw_a);
  EXPECT_EQ(std::get<ECSLTerm::Expr>(rel.m_rhs.m_val).m_expr.get(), raw_b);
}

TEST(ECSLPredTest, MakeRelAllOps) {
  auto make = [](RelOp op) {
    return ECSLPred::MakeRel(ECSLTerm::MakeExpr(nullptr, SourceRange{}), op,
                             ECSLTerm::MakeExpr(nullptr, SourceRange{}),
                             SourceRange{});
  };
  EXPECT_EQ(std::get<ECSLPred::Rel>(make(RelOp::Eq)->m_val).m_op, RelOp::Eq);
  EXPECT_EQ(std::get<ECSLPred::Rel>(make(RelOp::Ne)->m_val).m_op, RelOp::Ne);
  EXPECT_EQ(std::get<ECSLPred::Rel>(make(RelOp::Lt)->m_val).m_op, RelOp::Lt);
  EXPECT_EQ(std::get<ECSLPred::Rel>(make(RelOp::Le)->m_val).m_op, RelOp::Le);
  EXPECT_EQ(std::get<ECSLPred::Rel>(make(RelOp::Gt)->m_val).m_op, RelOp::Gt);
  EXPECT_EQ(std::get<ECSLPred::Rel>(make(RelOp::Ge)->m_val).m_op, RelOp::Ge);
}

// ---------------------------------------------------------------------------
// ECSLPred — connectives
// ---------------------------------------------------------------------------

TEST(ECSLPredTest, MakeAnd) {
  auto left = ECSLPred::MakeTrue(SourceRange{});
  auto right = ECSLPred::MakeFalse(SourceRange{});
  auto p = ECSLPred::MakeAnd(std::move(left), std::move(right), SourceRange{});
  ASSERT_NE(p, nullptr);
  ASSERT_TRUE(std::holds_alternative<ECSLPred::And>(p->m_val));
  auto &and_pred = std::get<ECSLPred::And>(p->m_val);
  ASSERT_NE(and_pred.m_left, nullptr);
  ASSERT_NE(and_pred.m_right, nullptr);
  EXPECT_TRUE(std::holds_alternative<ECSLPred::True>(and_pred.m_left->m_val));
  EXPECT_TRUE(std::holds_alternative<ECSLPred::False>(and_pred.m_right->m_val));
}

TEST(ECSLPredTest, MakeOr) {
  auto left = ECSLPred::MakeTrue(SourceRange{});
  auto right = ECSLPred::MakeTrue(SourceRange{});
  auto p = ECSLPred::MakeOr(std::move(left), std::move(right), SourceRange{});
  ASSERT_NE(p, nullptr);
  EXPECT_TRUE(std::holds_alternative<ECSLPred::Or>(p->m_val));
}

TEST(ECSLPredTest, MakeNot) {
  auto inner = ECSLPred::MakeTrue(SourceRange{});
  auto p = ECSLPred::MakeNot(std::move(inner), SourceRange{});
  ASSERT_NE(p, nullptr);
  ASSERT_TRUE(std::holds_alternative<ECSLPred::Not>(p->m_val));
  auto &not_pred = std::get<ECSLPred::Not>(p->m_val);
  ASSERT_NE(not_pred.m_operand, nullptr);
  EXPECT_TRUE(
      std::holds_alternative<ECSLPred::True>(not_pred.m_operand->m_val));
}

TEST(ECSLPredTest, NestedAndOrTree) {
  // Build: (True && False) || (!True)
  auto inner_and =
      ECSLPred::MakeAnd(ECSLPred::MakeTrue(SourceRange{}),
                        ECSLPred::MakeFalse(SourceRange{}), SourceRange{});
  auto inner_not =
      ECSLPred::MakeNot(ECSLPred::MakeTrue(SourceRange{}), SourceRange{});
  auto root = ECSLPred::MakeOr(std::move(inner_and), std::move(inner_not),
                               SourceRange{});

  ASSERT_NE(root, nullptr);
  ASSERT_TRUE(std::holds_alternative<ECSLPred::Or>(root->m_val));
  auto &or_pred = std::get<ECSLPred::Or>(root->m_val);
  EXPECT_TRUE(std::holds_alternative<ECSLPred::And>(or_pred.m_left->m_val));
  EXPECT_TRUE(std::holds_alternative<ECSLPred::Not>(or_pred.m_right->m_val));
}

// ---------------------------------------------------------------------------
// ECSLFunctionContract
// ---------------------------------------------------------------------------

TEST(ECSLFunctionContractTest, EmptyContractIsValid) {
  ECSLFunctionContract c;
  EXPECT_TRUE(c.m_requires.empty());
  EXPECT_TRUE(c.m_ensures.empty());
  EXPECT_FALSE(c.m_assigns_nothing.has_value());
}

TEST(ECSLFunctionContractTest, RequiresClauseStoresModAndPred) {
  ECSLFunctionContract::RequiresClause req;
  req.m_mod = ClauseModifier::None;
  req.m_pred = ECSLPred::MakeTrue(SourceRange{});

  EXPECT_EQ(req.m_mod, ClauseModifier::None);
  ASSERT_NE(req.m_pred, nullptr);
  EXPECT_TRUE(std::holds_alternative<ECSLPred::True>(req.m_pred->m_val));
}

TEST(ECSLFunctionContractTest, EnsuresClauseStoresResultTerm) {
  // ensures \result == 0
  ECSLTerm result_term = ECSLTerm::MakeResult(SourceRange{});
  ECSLTerm zero_term = ECSLTerm::MakeExpr(
      ECSLExpr::MakeIntLit("0", SourceRange{}), SourceRange{});
  auto pred = ECSLPred::MakeRel(std::move(result_term), RelOp::Eq,
                                std::move(zero_term), SourceRange{});

  ECSLFunctionContract::EnsuresClause ens;
  ens.m_pred = std::move(pred);

  ASSERT_NE(ens.m_pred, nullptr);
  ASSERT_TRUE(std::holds_alternative<ECSLPred::Rel>(ens.m_pred->m_val));
  auto &rel = std::get<ECSLPred::Rel>(ens.m_pred->m_val);
  EXPECT_TRUE(std::holds_alternative<ECSLTerm::Result>(rel.m_lhs.m_val));
  EXPECT_EQ(rel.m_op, RelOp::Eq);
}

TEST(ECSLFunctionContractTest, AssignsNothingClauseOptional) {
  ECSLFunctionContract c;
  EXPECT_FALSE(c.m_assigns_nothing.has_value());

  c.m_assigns_nothing = ECSLFunctionContract::AssignsNothingClause{};
  EXPECT_TRUE(c.m_assigns_nothing.has_value());
}

TEST(ECSLFunctionContractTest, ContractWithMultipleClauses) {
  ECSLFunctionContract c;

  // requires x > 0
  {
    ECSLFunctionContract::RequiresClause req;
    req.m_pred = ECSLPred::MakeRel(
        ECSLTerm::MakeExpr(ECSLExpr::MakeIdent("x", SourceRange{}),
                           SourceRange{}),
        RelOp::Gt,
        ECSLTerm::MakeExpr(ECSLExpr::MakeIntLit("0", SourceRange{}),
                           SourceRange{}),
        SourceRange{});
    c.m_requires.push_back(std::move(req));
  }

  // ensures \result > 0
  {
    ECSLFunctionContract::EnsuresClause ens;
    ens.m_pred = ECSLPred::MakeRel(
        ECSLTerm::MakeResult(SourceRange{}), RelOp::Gt,
        ECSLTerm::MakeExpr(ECSLExpr::MakeIntLit("0", SourceRange{}),
                           SourceRange{}),
        SourceRange{});
    c.m_ensures.push_back(std::move(ens));
  }

  // assigns \nothing
  c.m_assigns_nothing = ECSLFunctionContract::AssignsNothingClause{};

  EXPECT_EQ(c.m_requires.size(), 1u);
  EXPECT_EQ(c.m_ensures.size(), 1u);
  EXPECT_TRUE(c.m_assigns_nothing.has_value());
  EXPECT_TRUE(
      std::holds_alternative<ECSLPred::Rel>(c.m_requires[0].m_pred->m_val));
  EXPECT_TRUE(std::holds_alternative<ECSLTerm::Result>(
      std::get<ECSLPred::Rel>(c.m_ensures[0].m_pred->m_val).m_lhs.m_val));
}

// ---------------------------------------------------------------------------
// Move semantics
// ---------------------------------------------------------------------------

TEST(ECSLPredTest, MoveConstruction) {
  auto p1 = ECSLPred::MakeTrue(SourceRange{});
  ECSLPred *raw = p1.get();
  auto p2 = std::move(p1);
  EXPECT_EQ(p1, nullptr); // NOLINT: moved-from is null unique_ptr
  EXPECT_EQ(p2.get(), raw);
}

// ---------------------------------------------------------------------------
// ECSLExpr
// ---------------------------------------------------------------------------

TEST(ECSLExprTest, MakeIntLit) {
  auto e = ECSLExpr::MakeIntLit("42", SourceRange{});
  ASSERT_NE(e, nullptr);
  ASSERT_TRUE(std::holds_alternative<ECSLExpr::IntLit>(e->m_val));
  EXPECT_EQ(std::get<ECSLExpr::IntLit>(e->m_val).m_val, "42");
}

TEST(ECSLExprTest, MakeBoolLit) {
  auto t = ECSLExpr::MakeBoolLit(true, SourceRange{});
  auto f = ECSLExpr::MakeBoolLit(false, SourceRange{});
  EXPECT_TRUE(std::holds_alternative<ECSLExpr::BoolLit>(t->m_val));
  EXPECT_TRUE(std::get<ECSLExpr::BoolLit>(t->m_val).m_val);
  EXPECT_FALSE(std::get<ECSLExpr::BoolLit>(f->m_val).m_val);
}

TEST(ECSLExprTest, MakeIdent) {
  auto e = ECSLExpr::MakeIdent("x", SourceRange{});
  ASSERT_NE(e, nullptr);
  ASSERT_TRUE(std::holds_alternative<ECSLExpr::Ident>(e->m_val));
  EXPECT_EQ(std::get<ECSLExpr::Ident>(e->m_val).m_name, "x");
}

TEST(ECSLExprTest, MakeBinOp) {
  auto lhs = ECSLExpr::MakeIntLit("1", SourceRange{});
  auto rhs = ECSLExpr::MakeIntLit("2", SourceRange{});
  ECSLExpr *raw_lhs = lhs.get();
  ECSLExpr *raw_rhs = rhs.get();
  auto e = ECSLExpr::MakeBinOp(ExprOp::Add, std::move(lhs), std::move(rhs),
                               SourceRange{});
  ASSERT_NE(e, nullptr);
  ASSERT_TRUE(std::holds_alternative<ECSLExpr::BinOp>(e->m_val));
  auto &binop = std::get<ECSLExpr::BinOp>(e->m_val);
  EXPECT_EQ(binop.m_op, ExprOp::Add);
  EXPECT_EQ(binop.m_lhs.get(), raw_lhs);
  EXPECT_EQ(binop.m_rhs.get(), raw_rhs);
}

TEST(ECSLExprTest, MakeBinOpAllOps) {
  auto make = [](ExprOp op) {
    return ECSLExpr::MakeBinOp(op, ECSLExpr::MakeIntLit("0", SourceRange{}),
                               ECSLExpr::MakeIntLit("0", SourceRange{}),
                               SourceRange{});
  };
  EXPECT_EQ(std::get<ECSLExpr::BinOp>(make(ExprOp::Add)->m_val).m_op,
            ExprOp::Add);
  EXPECT_EQ(std::get<ECSLExpr::BinOp>(make(ExprOp::Sub)->m_val).m_op,
            ExprOp::Sub);
  EXPECT_EQ(std::get<ECSLExpr::BinOp>(make(ExprOp::Mul)->m_val).m_op,
            ExprOp::Mul);
  EXPECT_EQ(std::get<ECSLExpr::BinOp>(make(ExprOp::Div)->m_val).m_op,
            ExprOp::Div);
  EXPECT_EQ(std::get<ECSLExpr::BinOp>(make(ExprOp::Mod)->m_val).m_op,
            ExprOp::Mod);
}

TEST(ECSLExprTest, MakeUnary) {
  auto operand = ECSLExpr::MakeIntLit("5", SourceRange{});
  ECSLExpr *raw = operand.get();
  auto e = ECSLExpr::MakeUnary(ExprOp::Neg, std::move(operand), SourceRange{});
  ASSERT_NE(e, nullptr);
  ASSERT_TRUE(std::holds_alternative<ECSLExpr::UnaryOp>(e->m_val));
  auto &unary = std::get<ECSLExpr::UnaryOp>(e->m_val);
  EXPECT_EQ(unary.m_op, ExprOp::Neg);
  EXPECT_EQ(unary.m_operand.get(), raw);
}

TEST(ECSLExprTest, NestedBinOp) {
  // (1 + 2) * 3
  auto inner = ECSLExpr::MakeBinOp(
      ExprOp::Add, ECSLExpr::MakeIntLit("1", SourceRange{}),
      ECSLExpr::MakeIntLit("2", SourceRange{}), SourceRange{});
  auto outer = ECSLExpr::MakeBinOp(ExprOp::Mul, std::move(inner),
                                   ECSLExpr::MakeIntLit("3", SourceRange{}),
                                   SourceRange{});
  ASSERT_NE(outer, nullptr);
  ASSERT_TRUE(std::holds_alternative<ECSLExpr::BinOp>(outer->m_val));
  auto &outer_binop = std::get<ECSLExpr::BinOp>(outer->m_val);
  EXPECT_EQ(outer_binop.m_op, ExprOp::Mul);
  ASSERT_NE(outer_binop.m_lhs, nullptr);
  ASSERT_TRUE(
      std::holds_alternative<ECSLExpr::BinOp>(outer_binop.m_lhs->m_val));
  EXPECT_EQ(std::get<ECSLExpr::BinOp>(outer_binop.m_lhs->m_val).m_op,
            ExprOp::Add);
}

TEST(ECSLExprTest, MoveConstruction) {
  auto e1 = ECSLExpr::MakeIntLit("99", SourceRange{});
  ECSLExpr *raw = e1.get();
  auto e2 = std::move(e1);
  EXPECT_EQ(e1, nullptr); // NOLINT: moved-from
  EXPECT_EQ(e2.get(), raw);
}

} // namespace
