//===- clang/ECSL/ECSLParser.cpp - ECSL annotation parser ------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "clang/ECSL/ECSLParser.h"
#include "clang/ECSL/ECSLLexer.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/StringRef.h"

using namespace clang;
using namespace clang::ecsl;

// ---------------------------------------------------------------------------
// Internal parser state — not exposed in the header.
// ---------------------------------------------------------------------------

namespace {

// ---------------------------------------------------------------------------
// ECSL C expression sub-parser
// ---------------------------------------------------------------------------

/// Recursive-descent parser for C expression fragments inside ECSL contracts.
///
/// Operates on the sub-span of Identifier / IntegerLiteral / FloatLiteral /
/// arithmetic-op / paren tokens collected by ECSLParserImpl::ParseCTerm, and
/// produces an ECSLExpr tree.
///
/// Grammar (subset of C):
///   expr     := add-sub
///   add-sub  := mul-div (('+' | '-') mul-div)*
///   mul-div  := unary   (('*' | '/' | '%') unary)*
///   unary    := '-' unary | primary
///   primary  := int-literal | 'true' | 'false' | identifier | '(' expr ')'
class ECSLExprParser {
public:
  explicit ECSLExprParser(llvm::ArrayRef<ECSLToken> tokens,
                          SourceLocation base_loc)
      : m_tokens(tokens), m_base_loc(base_loc) {}

  std::unique_ptr<ECSLExpr> ParseExpr() {
    auto result = ParseAddSub();
    if (result && !AtEnd())
      return nullptr;
    return result;
  }

private:
  bool AtEnd() const {
    return m_pos >= m_tokens.size() ||
           m_tokens[m_pos].m_kind == ECSLTokenKind::Eof;
  }

  const ECSLToken &Current() const {
    assert(m_pos < m_tokens.size());
    return m_tokens[m_pos];
  }

  ECSLToken Consume() {
    assert(!AtEnd());
    ECSLToken tok = m_tokens[m_pos];
    ++m_pos;
    return tok;
  }

  SourceLocation LocOf(unsigned offset) const {
    if (!m_base_loc.isValid())
      return SourceLocation{};
    return m_base_loc.getLocWithOffset(static_cast<int>(offset));
  }

  SourceRange LocRange(const ECSLToken &tok) const {
    unsigned end_off = tok.m_text.empty()
                           ? tok.m_offset
                           : tok.m_offset + tok.m_text.size() - 1;
    return SourceRange(LocOf(tok.m_offset), LocOf(end_off));
  }

  std::unique_ptr<ECSLExpr> ParseAddSub() {
    auto lhs = ParseMulDiv();
    if (!lhs)
      return nullptr;
    while (!AtEnd()) {
      ECSLTokenKind k = Current().m_kind;
      ExprOp op;
      if (k == ECSLTokenKind::Plus)
        op = ExprOp::Add;
      else if (k == ECSLTokenKind::Minus)
        op = ExprOp::Sub;
      else
        break;
      Consume();
      auto rhs = ParseMulDiv();
      if (!rhs)
        return nullptr;
      SourceRange range(lhs->Loc().getBegin(), rhs->Loc().getEnd());
      lhs = ECSLExpr::MakeBinOp(op, std::move(lhs), std::move(rhs), range);
    }
    return lhs;
  }

  std::unique_ptr<ECSLExpr> ParseMulDiv() {
    auto lhs = ParseUnary();
    if (!lhs)
      return nullptr;
    while (!AtEnd()) {
      ECSLTokenKind k = Current().m_kind;
      ExprOp op;
      if (k == ECSLTokenKind::Star)
        op = ExprOp::Mul;
      else if (k == ECSLTokenKind::Slash)
        op = ExprOp::Div;
      else if (k == ECSLTokenKind::Percent)
        op = ExprOp::Mod;
      else
        break;
      Consume();
      auto rhs = ParseUnary();
      if (!rhs)
        return nullptr;
      SourceRange range(lhs->Loc().getBegin(), rhs->Loc().getEnd());
      lhs = ECSLExpr::MakeBinOp(op, std::move(lhs), std::move(rhs), range);
    }
    return lhs;
  }

  std::unique_ptr<ECSLExpr> ParseUnary() {
    if (!AtEnd() && Current().m_kind == ECSLTokenKind::Minus) {
      ECSLToken tok = Consume();
      SourceLocation start = LocOf(tok.m_offset);
      auto operand = ParseUnary();
      if (!operand)
        return nullptr;
      SourceRange range(start, operand->Loc().getEnd());
      return ECSLExpr::MakeUnary(ExprOp::Neg, std::move(operand), range);
    }
    return ParsePrimary();
  }

  std::unique_ptr<ECSLExpr> ParsePrimary() {
    if (AtEnd())
      return nullptr;

    const ECSLToken &tok = Current();

    if (tok.m_kind == ECSLTokenKind::FloatLiteral)
      return nullptr; // \todo float literals not yet supported

    if (tok.m_kind == ECSLTokenKind::IntegerLiteral) {
      Consume();
      return ECSLExpr::MakeIntLit(tok.m_text.str(), LocRange(tok));
    }

    if (tok.m_kind == ECSLTokenKind::Identifier) {
      Consume();
      SourceRange loc = LocRange(tok);
      if (tok.m_text == "true")
        return ECSLExpr::MakeBoolLit(true, loc);
      if (tok.m_text == "false")
        return ECSLExpr::MakeBoolLit(false, loc);
      return ECSLExpr::MakeIdent(tok.m_text, loc);
    }

    if (tok.m_kind == ECSLTokenKind::LParen) {
      Consume(); // '('
      auto e = ParseExpr();
      if (!AtEnd() && Current().m_kind == ECSLTokenKind::RParen)
        Consume(); // ')'
      return e;
    }

    return nullptr;
  }

  llvm::ArrayRef<ECSLToken> m_tokens;
  unsigned m_pos = 0;
  SourceLocation m_base_loc;
};

} // anonymous namespace

// ---------------------------------------------------------------------------
// ECSLParser public API
// ---------------------------------------------------------------------------

std::optional<ECSLFunctionContract>
ECSLParser::ParseFunctionContract(llvm::StringRef Text, SourceLocation Loc,
                                  DiagnosticsEngine *Diags) {
  // ECSLParserImpl (contract clause grammar) is added in the next PR.
  // Returning nullopt here keeps the compat shim and all callers compilable.
  return std::nullopt;
}

// ---------------------------------------------------------------------------
// Backward-compatibility entry point used by the clangParse dispatch hooks
// ---------------------------------------------------------------------------

PendingAnnotation ecsl::parseECSLAnnotation(PendingAnnotation PA) {
  ECSLParser parser;
  SourceLocation body_loc = PA.Loc;
  if (body_loc.isValid())
    body_loc = body_loc.getLocWithOffset(3);
  parser.ParseFunctionContract(PA.Body, body_loc);
  return PA;
}
