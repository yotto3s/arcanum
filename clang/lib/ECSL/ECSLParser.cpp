//===- clang/ECSL/ECSLParser.cpp - ECSL annotation parser ------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "clang/ECSL/ECSLParser.h"
#include "clang/Basic/Diagnostic.h"
#include "clang/Basic/DiagnosticECSL.h"
#include "clang/ECSL/ECSLLexer.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/StringRef.h"

using namespace clang;
using namespace clang::ecsl;

// ---------------------------------------------------------------------------
// Internal parser state — not exposed in the header.
// ---------------------------------------------------------------------------

namespace {

/// Returns true if \p k terminates a C-term scan at predicate level (depth 0).
static bool IsClauseBoundary(ECSLTokenKind k) {
  switch (k) {
  case ECSLTokenKind::EqEq:
  case ECSLTokenKind::BangEq:
  case ECSLTokenKind::Lt:
  case ECSLTokenKind::Le:
  case ECSLTokenKind::Gt:
  case ECSLTokenKind::Ge:
  case ECSLTokenKind::AmpAmp:
  case ECSLTokenKind::PipePipe:
  case ECSLTokenKind::Bang:
  case ECSLTokenKind::Semicolon:
  case ECSLTokenKind::Eof:
  case ECSLTokenKind::BslashResult:
  case ECSLTokenKind::BslashNothing:
    return true;
  default:
    return false;
  }
}

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

  std::unique_ptr<ECSLExpr> ParseExpr() { return ParseAddSub(); }

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
    return SourceRange(LocOf(tok.m_offset),
                       LocOf(tok.m_offset + tok.m_text.size()));
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
        break;
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
        break;
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

// ---------------------------------------------------------------------------
// Main contract parser state
// ---------------------------------------------------------------------------

/// State machine for parsing one ECSL function contract.
class ECSLParserImpl {
public:
  ECSLParserImpl(llvm::ArrayRef<ECSLToken> tokens, SourceLocation base_loc,
                 DiagnosticsEngine *diags)
      : m_tokens(tokens), m_base_loc(base_loc), m_diags(diags) {}

  std::optional<ECSLFunctionContract> ParseFunctionContract();

private:
  // ---- Token access -------------------------------------------------------

  const ECSLToken &Current() const {
    assert(m_pos < m_tokens.size());
    return m_tokens[m_pos];
  }

  ECSLToken Consume() {
    ECSLToken tok = m_tokens[m_pos];
    if (m_pos + 1 < m_tokens.size())
      ++m_pos;
    return tok;
  }

  bool AtEnd() const {
    return m_pos >= m_tokens.size() ||
           m_tokens[m_pos].m_kind == ECSLTokenKind::Eof;
  }

  /// Return the SourceLocation for the *start* of \p tok.
  SourceLocation LocStart(const ECSLToken &tok) const {
    if (!m_base_loc.isValid())
      return SourceLocation{};
    return m_base_loc.getLocWithOffset(static_cast<int>(tok.m_offset));
  }

  /// Return the SourceLocation for the *end* of \p tok (one past last char).
  SourceLocation LocEnd(const ECSLToken &tok) const {
    if (!m_base_loc.isValid())
      return SourceLocation{};
    return m_base_loc.getLocWithOffset(
        static_cast<int>(tok.m_offset + tok.m_text.size()));
  }

  SourceRange LocRange(const ECSLToken &tok) const {
    return SourceRange(LocStart(tok), LocEnd(tok));
  }

  // ---- Diagnostics --------------------------------------------------------

  void EmitError(const ECSLToken &tok, unsigned diag_id) {
    if (!m_diags)
      return;
    m_diags->Report(LocStart(tok), diag_id);
  }

  // ---- Error recovery -----------------------------------------------------

  /// Skip tokens until a ';' or Eof is consumed (inclusive of ';').
  void SkipToSemi() {
    while (!AtEnd()) {
      ECSLTokenKind k = Current().m_kind;
      if (k == ECSLTokenKind::Semicolon) {
        Consume();
        return;
      }
      if (k == ECSLTokenKind::Eof)
        return;
      Consume();
    }
  }

  // ---- Lookahead helpers --------------------------------------------------

  /// Scan forward from position \p from to find the RParen matching an LParen
  /// at \p from. Returns the index of the matching RParen, or m_tokens.size()
  /// if not found.
  unsigned FindMatchingRParen(unsigned from) const {
    assert(m_tokens[from].m_kind == ECSLTokenKind::LParen);
    unsigned depth = 1;
    for (unsigned i = from + 1; i < m_tokens.size(); ++i) {
      ECSLTokenKind k = m_tokens[i].m_kind;
      if (k == ECSLTokenKind::LParen)
        ++depth;
      else if (k == ECSLTokenKind::RParen) {
        if (--depth == 0)
          return i;
      } else if (k == ECSLTokenKind::Eof)
        break;
    }
    return m_tokens.size();
  }

  /// Returns true if the token at \p idx is a relational operator.
  static bool IsRelOp(ECSLTokenKind k) {
    switch (k) {
    case ECSLTokenKind::EqEq:
    case ECSLTokenKind::BangEq:
    case ECSLTokenKind::Lt:
    case ECSLTokenKind::Le:
    case ECSLTokenKind::Gt:
    case ECSLTokenKind::Ge:
      return true;
    default:
      return false;
    }
  }

  // ---- Term parsing -------------------------------------------------------

  /// Parse a single ECSL term.
  ///
  /// A term is one of:
  ///   - \result  → ECSLTerm::Result
  ///   - \nothing → ECSLTerm::Nothing
  ///   - <C-expr> → ECSLTerm::Expr (sequence of distinct C tokens)
  ECSLTerm ParseCTerm() {
    // Backslash terms are single tokens.
    if (Current().m_kind == ECSLTokenKind::BslashResult) {
      ECSLToken tok = Consume();
      return ECSLTerm::MakeResult(LocRange(tok));
    }
    if (Current().m_kind == ECSLTokenKind::BslashNothing) {
      ECSLToken tok = Consume();
      return ECSLTerm::MakeNothing(LocRange(tok));
    }

    // Collect a C expression fragment.  Track parenthesis depth so function
    // call arguments like f(x > 0) are not split at the '>'.
    unsigned start = m_pos;
    unsigned depth = 0;

    while (!AtEnd()) {
      ECSLTokenKind k = Current().m_kind;

      if (k == ECSLTokenKind::LParen) {
        ++depth;
        ++m_pos;
        continue;
      }
      if (k == ECSLTokenKind::RParen) {
        if (depth == 0)
          break; // Outer predicate-grouping paren — stop.
        --depth;
        ++m_pos;
        continue;
      }

      // At depth 0, stop at ECSL predicate boundary tokens.
      if (depth == 0 && IsClauseBoundary(k))
        break;

      ++m_pos;
    }

    if (m_pos == start) {
      EmitError(Current(), diag::err_ecsl_expected_term);
      return ECSLTerm::MakeExpr(nullptr, SourceRange{});
    }

    llvm::ArrayRef<ECSLToken> span(m_tokens.data() + start, m_pos - start);
    std::unique_ptr<ECSLExpr> expr =
        ECSLExprParser(span, m_base_loc).ParseExpr();

    SourceRange range(LocStart(m_tokens[start]),
                      LocEnd(m_tokens[m_pos > start ? m_pos - 1 : start]));
    return ECSLTerm::MakeExpr(std::move(expr), range);
  }

  // ---- Predicate parsing --------------------------------------------------

  /// Parse an atom: '(' pred ')' or a comparison / bare C expression.
  ///
  /// When we see '(', we look ahead to its matching ')'. If the token after
  /// ')' is a relational operator, the parenthesised content is part of a
  /// C term — fall through to ParseComparison so the full `(expr) rel rhs`
  /// is handled correctly. Otherwise parse as a grouped predicate.
  std::unique_ptr<ECSLPred> ParseAtom() {
    if (Current().m_kind == ECSLTokenKind::LParen) {
      // Look ahead: is this a grouped predicate or a parenthesised C term?
      unsigned close = FindMatchingRParen(m_pos);
      unsigned after = (close + 1 < m_tokens.size()) ? close + 1 : close;
      bool term_paren = (close < m_tokens.size()) &&
                        (after < m_tokens.size()) &&
                        IsRelOp(m_tokens[after].m_kind);
      if (!term_paren) {
        Consume(); // '('
        auto pred = ParseOr();
        if (!AtEnd() && Current().m_kind == ECSLTokenKind::RParen)
          Consume();
        else
          EmitError(Current(), diag::err_ecsl_expected_rparen);
        return pred;
      }
    }
    return ParseComparison();
  }

  /// Parse a comparison: <term> rel-op <term>  OR  bare <term> (C predicate).
  std::unique_ptr<ECSLPred> ParseComparison() {
    SourceLocation start_loc = LocStart(Current());
    ECSLTerm lhs = ParseCTerm();

    RelOp op;
    bool has_rel = true;
    switch (Current().m_kind) {
    case ECSLTokenKind::EqEq:
      op = RelOp::Eq;
      break;
    case ECSLTokenKind::BangEq:
      op = RelOp::Ne;
      break;
    case ECSLTokenKind::Lt:
      op = RelOp::Lt;
      break;
    case ECSLTokenKind::Le:
      op = RelOp::Le;
      break;
    case ECSLTokenKind::Gt:
      op = RelOp::Gt;
      break;
    case ECSLTokenKind::Ge:
      op = RelOp::Ge;
      break;
    default:
      has_rel = false;
      break;
    }

    if (!has_rel) {
      // Bare term used as a boolean predicate (e.g. a plain identifier).
      SourceRange range(start_loc, lhs.Loc().getEnd());
      std::unique_ptr<ECSLExpr> cexpr;
      if (std::holds_alternative<ECSLTerm::Expr>(lhs.Val()))
        cexpr = lhs.TakeExpr();
      return ECSLPred::MakeBoolExpr(std::move(cexpr), range);
    }

    Consume(); // consume rel-op
    ECSLTerm rhs = ParseCTerm();
    SourceRange range(start_loc, rhs.Loc().getEnd());
    return ECSLPred::MakeRel(std::move(lhs), op, std::move(rhs), range);
  }

  /// Parse a unary predicate: '!' <unary>  or  <atom>.
  std::unique_ptr<ECSLPred> ParseUnary() {
    if (Current().m_kind == ECSLTokenKind::Bang) {
      ECSLToken tok = Consume();
      auto operand = ParseUnary();
      return ECSLPred::MakeNot(std::move(operand), LocStart(tok));
    }
    return ParseAtom();
  }

  /// Parse a conjunction: <unary> ('&&' <unary>)*.
  std::unique_ptr<ECSLPred> ParseAnd() {
    auto lhs = ParseUnary();
    while (!AtEnd() && Current().m_kind == ECSLTokenKind::AmpAmp) {
      Consume(); // '&&'
      auto rhs = ParseUnary();
      SourceRange range(lhs->Loc().getBegin(), rhs->Loc().getEnd());
      lhs = ECSLPred::MakeAnd(std::move(lhs), std::move(rhs), range);
    }
    return lhs;
  }

  /// Parse a disjunction: <and> ('||' <and>)*.
  std::unique_ptr<ECSLPred> ParseOr() {
    auto lhs = ParseAnd();
    while (!AtEnd() && Current().m_kind == ECSLTokenKind::PipePipe) {
      Consume(); // '||'
      auto rhs = ParseAnd();
      SourceRange range(lhs->Loc().getBegin(), rhs->Loc().getEnd());
      lhs = ECSLPred::MakeOr(std::move(lhs), std::move(rhs), range);
    }
    return lhs;
  }

  std::unique_ptr<ECSLPred> ParsePred() { return ParseOr(); }

  // ---- Clause parsing -----------------------------------------------------

  /// Parse a requires clause: 'requires' <pred> ';'
  std::optional<ECSLFunctionContract::RequiresClause> ParseRequiresClause() {
    assert(Current().m_text == "requires");
    SourceLocation start = LocStart(Consume());

    auto pred = ParsePred();

    SourceRange range(start, LocStart(Current()));
    if (!AtEnd() && Current().m_kind == ECSLTokenKind::Semicolon)
      Consume();
    else
      EmitError(Current(), diag::err_ecsl_expected_semi_after_requires);

    ECSLFunctionContract::RequiresClause clause;
    clause.m_pred = std::move(pred);
    clause.m_loc = range;
    return clause;
  }

  /// Parse an ensures clause: 'ensures' <pred> ';'
  std::optional<ECSLFunctionContract::EnsuresClause> ParseEnsuresClause() {
    assert(Current().m_text == "ensures");
    SourceLocation start = LocStart(Consume());

    auto pred = ParsePred();

    SourceRange range(start, LocStart(Current()));
    if (!AtEnd() && Current().m_kind == ECSLTokenKind::Semicolon)
      Consume();
    else
      EmitError(Current(), diag::err_ecsl_expected_semi_after_ensures);

    ECSLFunctionContract::EnsuresClause clause;
    clause.m_pred = std::move(pred);
    clause.m_loc = range;
    return clause;
  }

  /// Parse an assigns clause: 'assigns' '\nothing' ';'  (M1 only)
  std::optional<ECSLFunctionContract::AssignsNothingClause>
  ParseAssignsClause() {
    assert(Current().m_text == "assigns");
    SourceLocation start = LocStart(Consume());

    if (Current().m_kind != ECSLTokenKind::BslashNothing) {
      EmitError(Current(), diag::err_ecsl_assigns_not_nothing);
      SkipToSemi();
      return std::nullopt;
    }
    Consume(); // '\nothing'

    SourceRange range(start, LocStart(Current()));
    if (!AtEnd() && Current().m_kind == ECSLTokenKind::Semicolon)
      Consume();
    else
      EmitError(Current(), diag::err_ecsl_expected_semi_after_assigns);

    ECSLFunctionContract::AssignsNothingClause clause;
    clause.m_loc = range;
    return clause;
  }

  // ---- Data members -------------------------------------------------------
  llvm::ArrayRef<ECSLToken> m_tokens;
  unsigned m_pos = 0;
  SourceLocation m_base_loc;
  DiagnosticsEngine *m_diags;
};

// ---------------------------------------------------------------------------
// ParseFunctionContract
// ---------------------------------------------------------------------------

std::optional<ECSLFunctionContract> ECSLParserImpl::ParseFunctionContract() {
  ECSLFunctionContract contract;
  bool saw_any_clause = false;

  while (!AtEnd()) {
    // Clause keywords are plain Identifier tokens whose text is the keyword.
    if (Current().m_kind == ECSLTokenKind::Identifier) {
      llvm::StringRef text = Current().m_text;

      if (text == "requires") {
        if (auto clause = ParseRequiresClause()) {
          contract.AddRequires(std::move(*clause));
          saw_any_clause = true;
        } else
          SkipToSemi();
        continue;
      }

      if (text == "ensures") {
        if (auto clause = ParseEnsuresClause()) {
          contract.AddEnsures(std::move(*clause));
          saw_any_clause = true;
        } else
          SkipToSemi();
        continue;
      }

      if (text == "assigns") {
        if (auto clause = ParseAssignsClause()) {
          contract.SetAssignsNothing(std::move(*clause));
          saw_any_clause = true;
        }
        continue;
      }
    }

    if (Current().m_kind == ECSLTokenKind::Eof)
      break;

    // Unexpected token at clause position — emit a diagnostic and skip.
    EmitError(Current(), diag::err_ecsl_unexpected_clause_token);
    SkipToSemi();
  }

  if (!saw_any_clause)
    return std::nullopt;

  return contract;
}

} // anonymous namespace

// ---------------------------------------------------------------------------
// ECSLParser public API
// ---------------------------------------------------------------------------

std::optional<ECSLFunctionContract>
ECSLParser::ParseFunctionContract(llvm::StringRef Text, SourceLocation Loc,
                                  DiagnosticsEngine *Diags) {
  llvm::SmallVector<ECSLToken> tokens;
  ECSLLexer lexer(Text, Loc);
  lexer.Lex(tokens);

  ECSLParserImpl impl(tokens, Loc, Diags);
  return impl.ParseFunctionContract();
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
