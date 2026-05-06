//===- unittests/ECSL/ECSLExprParserTest.cpp - ECSLExprParser tests -------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
///
/// \file
/// Unit tests for ECSLExprParser via ECSLParser::ParseExpr.
///
/// Tests cover: integer/bool literals, identifiers, operator precedence,
/// left-associativity, unary negation, parenthesised expressions, and error
/// cases (partial expressions, trailing tokens, missing closing paren).
///
//===----------------------------------------------------------------------===//

#include "clang/ECSL/ECSLParser.h"
#include "gtest/gtest.h"
#include <memory>

using namespace clang;
using namespace clang::ecsl;

namespace {

/// Parse \p text and return the resulting ECSLExpr (or nullptr on error).
static std::unique_ptr<ECSLExpr> Parse(llvm::StringRef text) {
  ECSLParser parser;
  return parser.ParseExpr(text, SourceLocation::getFromRawEncoding(1));
}

// ---------------------------------------------------------------------------
// Literals
// ---------------------------------------------------------------------------

TEST(ECSLExprParser, IntegerLiteral) {
  auto e = Parse("42");
  ASSERT_NE(e, nullptr);
  const auto *lit = std::get_if<ECSLExpr::IntLit>(&e->Val());
  ASSERT_NE(lit, nullptr);
  EXPECT_EQ(lit->m_val, "42");
}

TEST(ECSLExprParser, BoolLiteralTrue) {
  auto e = Parse("true");
  ASSERT_NE(e, nullptr);
  const auto *lit = std::get_if<ECSLExpr::BoolLit>(&e->Val());
  ASSERT_NE(lit, nullptr);
  EXPECT_TRUE(lit->m_val);
}

TEST(ECSLExprParser, BoolLiteralFalse) {
  auto e = Parse("false");
  ASSERT_NE(e, nullptr);
  const auto *lit = std::get_if<ECSLExpr::BoolLit>(&e->Val());
  ASSERT_NE(lit, nullptr);
  EXPECT_FALSE(lit->m_val);
}

TEST(ECSLExprParser, Identifier) {
  auto e = Parse("x");
  ASSERT_NE(e, nullptr);
  const auto *ident = std::get_if<ECSLExpr::Ident>(&e->Val());
  ASSERT_NE(ident, nullptr);
  EXPECT_EQ(ident->m_name, "x");
}

// ---------------------------------------------------------------------------
// Operator precedence: * binds tighter than +
// ---------------------------------------------------------------------------

TEST(ECSLExprParser, PrecedenceMulOverAdd) {
  // 1+2*3 should parse as Add(1, Mul(2, 3)), not Mul(Add(1,2), 3)
  auto e = Parse("1+2*3");
  ASSERT_NE(e, nullptr);
  const auto *add = std::get_if<ECSLExpr::BinOp>(&e->Val());
  ASSERT_NE(add, nullptr);
  EXPECT_EQ(add->m_op, ExprOp::Add);
  ASSERT_NE(add->m_lhs, nullptr);
  EXPECT_NE(std::get_if<ECSLExpr::IntLit>(&add->m_lhs->Val()), nullptr);
  const auto *mul = std::get_if<ECSLExpr::BinOp>(&add->m_rhs->Val());
  ASSERT_NE(mul, nullptr);
  EXPECT_EQ(mul->m_op, ExprOp::Mul);
}

TEST(ECSLExprParser, PrecedenceDivOverSub) {
  // 6-4/2 should parse as Sub(6, Div(4, 2))
  auto e = Parse("6-4/2");
  ASSERT_NE(e, nullptr);
  const auto *sub = std::get_if<ECSLExpr::BinOp>(&e->Val());
  ASSERT_NE(sub, nullptr);
  EXPECT_EQ(sub->m_op, ExprOp::Sub);
  const auto *div = std::get_if<ECSLExpr::BinOp>(&sub->m_rhs->Val());
  ASSERT_NE(div, nullptr);
  EXPECT_EQ(div->m_op, ExprOp::Div);
}

// ---------------------------------------------------------------------------
// Left-associativity
// ---------------------------------------------------------------------------

TEST(ECSLExprParser, LeftAssocAdd) {
  // 1-2-3 => Sub(Sub(1,2),3)
  auto e = Parse("1-2-3");
  ASSERT_NE(e, nullptr);
  const auto *outer = std::get_if<ECSLExpr::BinOp>(&e->Val());
  ASSERT_NE(outer, nullptr);
  EXPECT_EQ(outer->m_op, ExprOp::Sub);
  const auto *inner = std::get_if<ECSLExpr::BinOp>(&outer->m_lhs->Val());
  ASSERT_NE(inner, nullptr);
  EXPECT_EQ(inner->m_op, ExprOp::Sub);
}

TEST(ECSLExprParser, LeftAssocMul) {
  // 8/4/2 => Div(Div(8,4),2)
  auto e = Parse("8/4/2");
  ASSERT_NE(e, nullptr);
  const auto *outer = std::get_if<ECSLExpr::BinOp>(&e->Val());
  ASSERT_NE(outer, nullptr);
  EXPECT_EQ(outer->m_op, ExprOp::Div);
  const auto *inner = std::get_if<ECSLExpr::BinOp>(&outer->m_lhs->Val());
  ASSERT_NE(inner, nullptr);
  EXPECT_EQ(inner->m_op, ExprOp::Div);
}

// ---------------------------------------------------------------------------
// Unary negation
// ---------------------------------------------------------------------------

TEST(ECSLExprParser, UnaryNeg) {
  auto e = Parse("-x");
  ASSERT_NE(e, nullptr);
  const auto *u = std::get_if<ECSLExpr::UnaryOp>(&e->Val());
  ASSERT_NE(u, nullptr);
  EXPECT_EQ(u->m_op, ExprOp::Neg);
  EXPECT_NE(std::get_if<ECSLExpr::Ident>(&u->m_operand->Val()), nullptr);
}

TEST(ECSLExprParser, DoubleUnaryNeg) {
  auto e = Parse("--x");
  ASSERT_NE(e, nullptr);
  const auto *outer = std::get_if<ECSLExpr::UnaryOp>(&e->Val());
  ASSERT_NE(outer, nullptr);
  EXPECT_EQ(outer->m_op, ExprOp::Neg);
  const auto *inner = std::get_if<ECSLExpr::UnaryOp>(&outer->m_operand->Val());
  ASSERT_NE(inner, nullptr);
  EXPECT_EQ(inner->m_op, ExprOp::Neg);
}

// ---------------------------------------------------------------------------
// Parentheses
// ---------------------------------------------------------------------------

TEST(ECSLExprParser, Parens) {
  // (1+2)*3 => Mul(Add(1,2), 3)
  auto e = Parse("(1+2)*3");
  ASSERT_NE(e, nullptr);
  const auto *mul = std::get_if<ECSLExpr::BinOp>(&e->Val());
  ASSERT_NE(mul, nullptr);
  EXPECT_EQ(mul->m_op, ExprOp::Mul);
  const auto *add = std::get_if<ECSLExpr::BinOp>(&mul->m_lhs->Val());
  ASSERT_NE(add, nullptr);
  EXPECT_EQ(add->m_op, ExprOp::Add);
}

TEST(ECSLExprParser, NestedParens) {
  auto e = Parse("((x))");
  ASSERT_NE(e, nullptr);
  EXPECT_NE(std::get_if<ECSLExpr::Ident>(&e->Val()), nullptr);
}

TEST(ECSLExprParser, ParensWithUnary) {
  auto e = Parse("-(1+2)");
  ASSERT_NE(e, nullptr);
  const auto *u = std::get_if<ECSLExpr::UnaryOp>(&e->Val());
  ASSERT_NE(u, nullptr);
  EXPECT_EQ(u->m_op, ExprOp::Neg);
  EXPECT_NE(std::get_if<ECSLExpr::BinOp>(&u->m_operand->Val()), nullptr);
}

// ---------------------------------------------------------------------------
// Error cases
// ---------------------------------------------------------------------------

TEST(ECSLExprParser, PartialExprTrailingOp) {
  // "1+" is malformed — missing rhs
  EXPECT_EQ(Parse("1+"), nullptr);
}

TEST(ECSLExprParser, TrailingTokens) {
  // "a b" — extra token after valid expression
  EXPECT_EQ(Parse("a b"), nullptr);
}

TEST(ECSLExprParser, EmptyInput) { EXPECT_EQ(Parse(""), nullptr); }

TEST(ECSLExprParser, MissingClosingParen) {
  // "(1+2" — missing ')'
  EXPECT_EQ(Parse("(1+2"), nullptr);
}

TEST(ECSLExprParser, FloatLiteralUnsupported) {
  // Float literals are explicitly unsupported for now
  EXPECT_EQ(Parse("3.14"), nullptr);
}

} // anonymous namespace
