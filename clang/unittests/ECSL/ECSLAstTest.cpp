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
/// These tests verify that M1 ECSL AST nodes can be constructed, moved, and
/// queried correctly. They do not require Clang compilation infrastructure —
/// Clang Expr* fields are left null (representing the absence of a delegated
/// expression) throughout.
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

TEST(ECSLTermTest, MakeCExprSetsKindAndExpr) {
  auto *fake_expr = reinterpret_cast<clang::Expr *>(0x1);
  ECSLTerm t = ECSLTerm::MakeCExpr(fake_expr, SourceRange{});
  EXPECT_EQ(t.m_kind, ECSLTerm::Kind::CExpr);
  EXPECT_EQ(t.m_c_expr, fake_expr);
}

TEST(ECSLTermTest, MakeResultSetsKind) {
  ECSLTerm t = ECSLTerm::MakeResult(SourceRange{});
  EXPECT_EQ(t.m_kind, ECSLTerm::Kind::Result);
  EXPECT_EQ(t.m_c_expr, nullptr);
}

TEST(ECSLTermTest, MakeNothingSetsKind) {
  ECSLTerm t = ECSLTerm::MakeNothing(SourceRange{});
  EXPECT_EQ(t.m_kind, ECSLTerm::Kind::Nothing);
  EXPECT_EQ(t.m_c_expr, nullptr);
}

// ---------------------------------------------------------------------------
// ECSLPred — leaf nodes
// ---------------------------------------------------------------------------

TEST(ECSLPredTest, MakeTrue) {
  auto p = ECSLPred::MakeTrue(SourceRange{});
  ASSERT_NE(p, nullptr);
  EXPECT_EQ(p->m_kind, ECSLPred::Kind::True);
}

TEST(ECSLPredTest, MakeFalse) {
  auto p = ECSLPred::MakeFalse(SourceRange{});
  ASSERT_NE(p, nullptr);
  EXPECT_EQ(p->m_kind, ECSLPred::Kind::False);
}

// ---------------------------------------------------------------------------
// ECSLPred — relational
// ---------------------------------------------------------------------------

TEST(ECSLPredTest, MakeRelSetsFieldsCorrectly) {
  auto *fake_expr_a = reinterpret_cast<clang::Expr *>(0x10);
  auto *fake_expr_b = reinterpret_cast<clang::Expr *>(0x20);

  ECSLTerm lhs = ECSLTerm::MakeCExpr(fake_expr_a, SourceRange{});
  ECSLTerm rhs = ECSLTerm::MakeCExpr(fake_expr_b, SourceRange{});

  auto p = ECSLPred::MakeRel(std::move(lhs), RelOp::Gt, std::move(rhs),
                              SourceRange{});
  ASSERT_NE(p, nullptr);
  EXPECT_EQ(p->m_kind, ECSLPred::Kind::Rel);
  EXPECT_EQ(p->m_rel_op, RelOp::Gt);
  EXPECT_EQ(p->m_lhs.m_c_expr, fake_expr_a);
  EXPECT_EQ(p->m_rhs.m_c_expr, fake_expr_b);
}

TEST(ECSLPredTest, MakeRelAllOps) {
  auto make = [](RelOp op) {
    return ECSLPred::MakeRel(ECSLTerm::MakeCExpr(nullptr, SourceRange{}), op,
                             ECSLTerm::MakeCExpr(nullptr, SourceRange{}),
                             SourceRange{});
  };
  EXPECT_EQ(make(RelOp::Eq)->m_rel_op, RelOp::Eq);
  EXPECT_EQ(make(RelOp::Ne)->m_rel_op, RelOp::Ne);
  EXPECT_EQ(make(RelOp::Lt)->m_rel_op, RelOp::Lt);
  EXPECT_EQ(make(RelOp::Le)->m_rel_op, RelOp::Le);
  EXPECT_EQ(make(RelOp::Gt)->m_rel_op, RelOp::Gt);
  EXPECT_EQ(make(RelOp::Ge)->m_rel_op, RelOp::Ge);
}

// ---------------------------------------------------------------------------
// ECSLPred — connectives
// ---------------------------------------------------------------------------

TEST(ECSLPredTest, MakeAnd) {
  auto left = ECSLPred::MakeTrue(SourceRange{});
  auto right = ECSLPred::MakeFalse(SourceRange{});
  auto p =
      ECSLPred::MakeAnd(std::move(left), std::move(right), SourceRange{});
  ASSERT_NE(p, nullptr);
  EXPECT_EQ(p->m_kind, ECSLPred::Kind::And);
  ASSERT_NE(p->m_left, nullptr);
  ASSERT_NE(p->m_right, nullptr);
  EXPECT_EQ(p->m_left->m_kind, ECSLPred::Kind::True);
  EXPECT_EQ(p->m_right->m_kind, ECSLPred::Kind::False);
}

TEST(ECSLPredTest, MakeOr) {
  auto left = ECSLPred::MakeTrue(SourceRange{});
  auto right = ECSLPred::MakeTrue(SourceRange{});
  auto p = ECSLPred::MakeOr(std::move(left), std::move(right), SourceRange{});
  ASSERT_NE(p, nullptr);
  EXPECT_EQ(p->m_kind, ECSLPred::Kind::Or);
}

TEST(ECSLPredTest, MakeNot) {
  auto inner = ECSLPred::MakeTrue(SourceRange{});
  auto p = ECSLPred::MakeNot(std::move(inner), SourceRange{});
  ASSERT_NE(p, nullptr);
  EXPECT_EQ(p->m_kind, ECSLPred::Kind::Not);
  ASSERT_NE(p->m_operand, nullptr);
  EXPECT_EQ(p->m_operand->m_kind, ECSLPred::Kind::True);
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
  EXPECT_EQ(root->m_kind, ECSLPred::Kind::Or);
  EXPECT_EQ(root->m_left->m_kind, ECSLPred::Kind::And);
  EXPECT_EQ(root->m_right->m_kind, ECSLPred::Kind::Not);
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
  EXPECT_EQ(req.m_pred->m_kind, ECSLPred::Kind::True);
}

TEST(ECSLFunctionContractTest, EnsuresClauseStoresResultTerm) {
  // ensures \result == 0
  ECSLTerm result_term = ECSLTerm::MakeResult(SourceRange{});
  ECSLTerm zero_term = ECSLTerm::MakeCExpr(nullptr, SourceRange{});
  auto pred = ECSLPred::MakeRel(std::move(result_term), RelOp::Eq,
                                 std::move(zero_term), SourceRange{});

  ECSLFunctionContract::EnsuresClause ens;
  ens.m_pred = std::move(pred);

  ASSERT_NE(ens.m_pred, nullptr);
  EXPECT_EQ(ens.m_pred->m_kind, ECSLPred::Kind::Rel);
  EXPECT_EQ(ens.m_pred->m_lhs.m_kind, ECSLTerm::Kind::Result);
  EXPECT_EQ(ens.m_pred->m_rel_op, RelOp::Eq);
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
    req.m_pred =
        ECSLPred::MakeRel(ECSLTerm::MakeCExpr(nullptr, SourceRange{}),
                          RelOp::Gt,
                          ECSLTerm::MakeCExpr(nullptr, SourceRange{}),
                          SourceRange{});
    c.m_requires.push_back(std::move(req));
  }

  // ensures \result > 0
  {
    ECSLFunctionContract::EnsuresClause ens;
    ens.m_pred =
        ECSLPred::MakeRel(ECSLTerm::MakeResult(SourceRange{}), RelOp::Gt,
                          ECSLTerm::MakeCExpr(nullptr, SourceRange{}),
                          SourceRange{});
    c.m_ensures.push_back(std::move(ens));
  }

  // assigns \nothing
  c.m_assigns_nothing = ECSLFunctionContract::AssignsNothingClause{};

  EXPECT_EQ(c.m_requires.size(), 1u);
  EXPECT_EQ(c.m_ensures.size(), 1u);
  EXPECT_TRUE(c.m_assigns_nothing.has_value());
  EXPECT_EQ(c.m_requires[0].m_pred->m_kind, ECSLPred::Kind::Rel);
  EXPECT_EQ(c.m_ensures[0].m_pred->m_lhs.m_kind, ECSLTerm::Kind::Result);
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

} // namespace
