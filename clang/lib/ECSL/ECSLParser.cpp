//===- clang/ECSL/ECSLParser.cpp - ECSL annotation parser (M1) ------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "clang/ECSL/ECSLParser.h"
#include "clang/ECSL/ECSLLexer.h"
#include "llvm/ADT/SmallVector.h"

using namespace clang;
using namespace clang::ecsl;

// ---------------------------------------------------------------------------
// Internal parser state — not exposed in the header.
// ---------------------------------------------------------------------------

namespace {

/// Returns true if \p k is a token kind that marks the start of an ECSL
/// clause, i.e. it is a boundary for C-term scanning.
static bool IsClauseBoundary(ECSLTokenKind k) {
  switch (k) {
  case ECSLTokenKind::EqEq:
  case ECSLTokenKind::BangEq:
  case ECSLTokenKind::Lt:
  case ECSLTokenKind::Le:
  case ECSLTokenKind::Gt:
  case ECSLTokenKind::Ge:
  case ECSLTokenKind::Amp2:
  case ECSLTokenKind::Pipe2:
  case ECSLTokenKind::Bang:
  case ECSLTokenKind::Semi:
  case ECSLTokenKind::Eof:
  case ECSLTokenKind::KwRequires:
  case ECSLTokenKind::KwEnsures:
  case ECSLTokenKind::KwAssigns:
  case ECSLTokenKind::BslashResult:
  case ECSLTokenKind::BslashNothing:
    return true;
  default:
    return false;
  }
}

/// State machine for parsing one ECSL function contract.
class ECSLParserImpl {
public:
  ECSLParserImpl(llvm::ArrayRef<ECSLToken> tokens, SourceLocation base_loc,
                 DiagnosticsEngine *diags, ECSLParser::ExprDelegate delegate)
      : m_tokens(tokens), m_base_loc(base_loc), m_diags(diags),
        m_delegate(std::move(delegate)) {}

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

  SourceLocation LocOf(const ECSLToken &tok) const {
    if (!m_base_loc.isValid())
      return SourceLocation{};
    return m_base_loc.getLocWithOffset(static_cast<int>(tok.m_offset));
  }

  // ---- Diagnostics --------------------------------------------------------

  void EmitError(const ECSLToken & /*tok*/, const char * /*msg*/) {
    // \todo Emit proper ECSL diagnostics once the diagnostic table is
    //        established.  For M1 errors are silently recovered.
    (void)m_diags;
  }

  // ---- Error recovery -----------------------------------------------------

  /// Skip tokens until a ';' or Eof is consumed (inclusive of ';').
  void SkipToSemi() {
    while (!AtEnd()) {
      ECSLTokenKind k = Current().m_kind;
      if (k == ECSLTokenKind::Semi) {
        Consume();
        return;
      }
      if (k == ECSLTokenKind::Eof)
        return;
      Consume();
    }
  }

  // ---- Term parsing -------------------------------------------------------

  /// Parse a single ECSL term.
  ///
  /// A term is one of:
  ///   - \result   → ECSLTerm::Result
  ///   - \nothing  → ECSLTerm::Nothing
  ///   - <C-expr>  → ECSLTerm::CExpr (sequence of CAtom / balanced parens)
  ECSLTerm ParseCTerm() {
    // Backslash terms are single tokens.
    if (Current().m_kind == ECSLTokenKind::BslashResult) {
      ECSLToken tok = Consume();
      return ECSLTerm::MakeResult(LocOf(tok));
    }
    if (Current().m_kind == ECSLTokenKind::BslashNothing) {
      ECSLToken tok = Consume();
      return ECSLTerm::MakeNothing(LocOf(tok));
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

      // At depth 0, stop at ECSL boundary tokens.
      if (depth == 0 && IsClauseBoundary(k))
        break;

      ++m_pos;
    }

    if (m_pos == start) {
      EmitError(Current(), "expected expression term");
      // Return a null CExpr node; the caller will continue error recovery.
      return ECSLTerm::MakeCExpr(nullptr, SourceRange{});
    }

    llvm::ArrayRef<ECSLToken> span(m_tokens.data() + start, m_pos - start);
    clang::Expr *expr = m_delegate ? m_delegate(span) : nullptr;

    SourceRange range(LocOf(m_tokens[start]),
                      LocOf(m_tokens[m_pos > start ? m_pos - 1 : start]));
    return ECSLTerm::MakeCExpr(expr, range);
  }

  // ---- Predicate parsing --------------------------------------------------

  /// Parse an atom: '(' pred ')' or a comparison / bare C expression.
  std::unique_ptr<ECSLPred> ParseAtom() {
    if (Current().m_kind == ECSLTokenKind::LParen) {
      Consume(); // '('
      auto pred = ParseOr();
      if (Current().m_kind == ECSLTokenKind::RParen)
        Consume();
      else
        EmitError(Current(), "expected ')' after predicate");
      return pred;
    }
    return ParseComparison();
  }

  /// Parse a comparison: <term> rel-op <term>  OR  bare <term> (C predicate).
  std::unique_ptr<ECSLPred> ParseComparison() {
    SourceLocation start_loc = LocOf(Current());
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

    if (!has_rel)
      return ECSLPred::MakeCExprPred(lhs.m_c_expr, lhs.m_loc);

    Consume(); // consume rel-op
    ECSLTerm rhs = ParseCTerm();
    SourceRange range(start_loc, rhs.m_loc.getEnd());
    return ECSLPred::MakeRel(std::move(lhs), op, std::move(rhs), range);
  }

  /// Parse a unary predicate: '!' <unary>  or  <atom>.
  std::unique_ptr<ECSLPred> ParseUnary() {
    if (Current().m_kind == ECSLTokenKind::Bang) {
      ECSLToken tok = Consume();
      auto operand = ParseUnary();
      return ECSLPred::MakeNot(std::move(operand), LocOf(tok));
    }
    return ParseAtom();
  }

  /// Parse a conjunction: <unary> ('&&' <unary>)*.
  std::unique_ptr<ECSLPred> ParseAnd() {
    auto lhs = ParseUnary();
    while (Current().m_kind == ECSLTokenKind::Amp2) {
      Consume(); // '&&'
      auto rhs = ParseUnary();
      SourceRange range(lhs->m_loc.getBegin(), rhs->m_loc.getEnd());
      lhs = ECSLPred::MakeAnd(std::move(lhs), std::move(rhs), range);
    }
    return lhs;
  }

  /// Parse a disjunction: <and> ('||' <and>)*.
  std::unique_ptr<ECSLPred> ParseOr() {
    auto lhs = ParseAnd();
    while (Current().m_kind == ECSLTokenKind::Pipe2) {
      Consume(); // '||'
      auto rhs = ParseAnd();
      SourceRange range(lhs->m_loc.getBegin(), rhs->m_loc.getEnd());
      lhs = ECSLPred::MakeOr(std::move(lhs), std::move(rhs), range);
    }
    return lhs;
  }

  /// Parse a full predicate (top level of the predicate grammar).
  std::unique_ptr<ECSLPred> ParsePred() { return ParseOr(); }

  // ---- Clause parsing -----------------------------------------------------

  /// Parse a requires clause: 'requires' <pred> ';'
  std::optional<ECSLFunctionContract::RequiresClause> ParseRequiresClause() {
    assert(Current().m_kind == ECSLTokenKind::KwRequires);
    SourceLocation start = LocOf(Consume());

    auto pred = ParsePred();

    SourceRange range(start, LocOf(Current()));
    if (Current().m_kind == ECSLTokenKind::Semi)
      Consume();
    else
      EmitError(Current(), "expected ';' after requires predicate");

    ECSLFunctionContract::RequiresClause clause;
    clause.m_pred = std::move(pred);
    clause.m_loc = range;
    return clause;
  }

  /// Parse an ensures clause: 'ensures' <pred> ';'
  std::optional<ECSLFunctionContract::EnsuresClause> ParseEnsuresClause() {
    assert(Current().m_kind == ECSLTokenKind::KwEnsures);
    SourceLocation start = LocOf(Consume());

    auto pred = ParsePred();

    SourceRange range(start, LocOf(Current()));
    if (Current().m_kind == ECSLTokenKind::Semi)
      Consume();
    else
      EmitError(Current(), "expected ';' after ensures predicate");

    ECSLFunctionContract::EnsuresClause clause;
    clause.m_pred = std::move(pred);
    clause.m_loc = range;
    return clause;
  }

  /// Parse an assigns clause: 'assigns' '\nothing' ';'  (M1 only)
  std::optional<ECSLFunctionContract::AssignsNothingClause>
  ParseAssignsClause() {
    assert(Current().m_kind == ECSLTokenKind::KwAssigns);
    SourceLocation start = LocOf(Consume());

    if (Current().m_kind != ECSLTokenKind::BslashNothing) {
      EmitError(Current(),
                "expected '\\nothing' after 'assigns' (M1 supports only "
                "'assigns \\nothing')");
      SkipToSemi();
      return std::nullopt;
    }
    Consume(); // '\nothing'

    SourceRange range(start, LocOf(Current()));
    if (Current().m_kind == ECSLTokenKind::Semi)
      Consume();
    else
      EmitError(Current(), "expected ';' after assigns \\nothing");

    ECSLFunctionContract::AssignsNothingClause clause;
    clause.m_loc = range;
    return clause;
  }

  // ---- Data members -------------------------------------------------------
  llvm::ArrayRef<ECSLToken> m_tokens;
  unsigned m_pos = 0;
  SourceLocation m_base_loc;
  DiagnosticsEngine *m_diags;
  ECSLParser::ExprDelegate m_delegate;
};

// ---------------------------------------------------------------------------
// ParseFunctionContract
// ---------------------------------------------------------------------------

std::optional<ECSLFunctionContract> ECSLParserImpl::ParseFunctionContract() {
  ECSLFunctionContract contract;
  bool saw_any_clause = false;

  while (!AtEnd()) {
    ECSLTokenKind k = Current().m_kind;

    if (k == ECSLTokenKind::KwRequires) {
      if (auto clause = ParseRequiresClause()) {
        contract.m_requires.push_back(std::move(*clause));
        saw_any_clause = true;
      } else
        SkipToSemi();
      continue;
    }

    if (k == ECSLTokenKind::KwEnsures) {
      if (auto clause = ParseEnsuresClause()) {
        contract.m_ensures.push_back(std::move(*clause));
        saw_any_clause = true;
      } else
        SkipToSemi();
      continue;
    }

    if (k == ECSLTokenKind::KwAssigns) {
      if (auto clause = ParseAssignsClause()) {
        contract.m_assigns_nothing = std::move(*clause);
        saw_any_clause = true;
      }
      continue;
    }

    if (k == ECSLTokenKind::Eof)
      break;

    // Unexpected token at clause position — emit a diagnostic and skip.
    EmitError(Current(),
              "expected 'requires', 'ensures', or 'assigns' at clause start");
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
                                  DiagnosticsEngine *Diags,
                                  ExprDelegate Delegate) {
  llvm::SmallVector<ECSLToken> tokens;
  ECSLLexer lexer(Text, Loc);
  lexer.Lex(tokens);

  ECSLParserImpl impl(tokens, Loc, Diags, std::move(Delegate));
  return impl.ParseFunctionContract();
}

// ---------------------------------------------------------------------------
// Backward-compatibility entry point used by the clangParse dispatch hooks
// ---------------------------------------------------------------------------

PendingAnnotation ecsl::parseECSLAnnotation(PendingAnnotation PA) {
  ECSLParser parser;
  // Parse for diagnostics; discard the typed result until PR 4/4 wires typed
  // contract storage.
  parser.ParseFunctionContract(PA.Body, PA.Loc);
  return PA;
}
