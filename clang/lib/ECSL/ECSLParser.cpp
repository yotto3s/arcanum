//===- clang/ECSL/ECSLParser.cpp - ECSL annotation parser ------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "clang/ECSL/ECSLParser.h"
#include "clang/ECSL/ECSLLexer.h"
#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/SmallVector.h"

using namespace clang;
using namespace clang::ecsl;

// ---------------------------------------------------------------------------
// Internal parser state.
// ---------------------------------------------------------------------------

namespace {

// ---------------------------------------------------------------------------
// ECSL C/C++ expression sub-parser
// ---------------------------------------------------------------------------

/// Recursive-descent parser for C/C++ expression fragments inside ECSL
/// contracts.
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
    if (result && !AtEnd()) {
      return nullptr;
    }
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
    assert(m_base_loc.isValid());
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
    if (!lhs) {
      return nullptr;
    }
    while (!AtEnd()) {
      ECSLTokenKind k = Current().m_kind;
      ExprOp op;
      if (k == ECSLTokenKind::Plus) {
        op = ExprOp::Add;
      } else if (k == ECSLTokenKind::Minus) {
        op = ExprOp::Sub;
      } else {
        break;
      }
      Consume();
      auto rhs = ParseMulDiv();
      if (!rhs) {
        return nullptr;
      }
      SourceRange range(lhs->Loc().getBegin(), rhs->Loc().getEnd());
      lhs = ECSLExpr::MakeBinOp(op, std::move(lhs), std::move(rhs), range);
    }
    return lhs;
  }

  std::unique_ptr<ECSLExpr> ParseMulDiv() {
    auto lhs = ParseUnary();
    if (!lhs) {
      return nullptr;
    }
    while (!AtEnd()) {
      ECSLTokenKind k = Current().m_kind;
      ExprOp op;
      if (k == ECSLTokenKind::Star) {
        op = ExprOp::Mul;
      } else if (k == ECSLTokenKind::Slash) {
        op = ExprOp::Div;
      } else if (k == ECSLTokenKind::Percent) {
        op = ExprOp::Mod;
      } else {
        break;
      }
      Consume();
      auto rhs = ParseUnary();
      if (!rhs) {
        return nullptr;
      }
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
      if (!operand) {
        return nullptr;
      }
      SourceRange range(start, operand->Loc().getEnd());
      return ECSLExpr::MakeUnary(ExprOp::Neg, std::move(operand), range);
    }
    return ParsePrimary();
  }

  std::unique_ptr<ECSLExpr> ParsePrimary() {
    if (AtEnd()) {
      return nullptr;
    }

    const ECSLToken &tok = Current();

    if (tok.m_kind == ECSLTokenKind::BslashResult) {
      Consume();
      return ECSLExpr::MakeResult(LocRange(tok));
    }

    if (tok.m_kind == ECSLTokenKind::FloatLiteral) {
      return nullptr; // \todo float literals not yet supported
    }

    if (tok.m_kind == ECSLTokenKind::IntegerLiteral) {
      Consume();
      return ECSLExpr::MakeIntLit(tok.m_text.str(), LocRange(tok));
    }

    if (tok.m_kind == ECSLTokenKind::Identifier) {
      Consume();
      SourceRange loc = LocRange(tok);
      if (tok.m_text == "true") {
        return ECSLExpr::MakeBoolLit(true, loc);
      }
      if (tok.m_text == "false") {
        return ECSLExpr::MakeBoolLit(false, loc);
      }
      return ECSLExpr::MakeIdent(tok.m_text, loc);
    }

    if (tok.m_kind == ECSLTokenKind::LParen) {
      SourceLocation open = LocOf(tok.m_offset);
      Consume(); // '('
      auto e = ParseAddSub();
      if (!e) {
        return nullptr;
      }
      if (AtEnd() || Current().m_kind != ECSLTokenKind::RParen) {
        return nullptr; // missing ')'
      }
      ECSLToken close = Consume(); // ')'
      e->SetLoc(SourceRange(open, LocOf(close.m_offset)));
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

  /// Return the SourceLocation for the *last* character of \p tok (inclusive).
  /// Consistent with ECSLExprParser::LocRange which uses offset + size - 1.
  SourceLocation LocEnd(const ECSLToken &tok) const {
    if (!m_base_loc.isValid())
      return SourceLocation{};
    unsigned end_off =
        tok.m_text.empty()
            ? tok.m_offset
            : tok.m_offset + static_cast<unsigned>(tok.m_text.size()) - 1;
    return m_base_loc.getLocWithOffset(static_cast<int>(end_off));
  }

  SourceRange LocRange(const ECSLToken &tok) const {
    return SourceRange(LocStart(tok), LocEnd(tok));
  }

  // ---- Diagnostics --------------------------------------------------------

  void EmitError(const ECSLToken & /*tok*/, const char * /*msg*/) {
    // Intentionally a no-op for M1 — DiagnosticsEngine* is accepted and
    // stored but is not invoked in this PR.  Typed diag:: IDs are wired in
    // PR 4/4.  All error-recovery is structural (SkipToSemi / return nullopt)
    // so callers still behave correctly without diagnostic output.
    (void)m_diags;
  }

  // ---- Error recovery -----------------------------------------------------

  /// Skip tokens until a ';' or Eof is consumed (inclusive of ';').
  void SkipToSemi() {
    while (!AtEnd()) {
      switch (Current().m_kind) {
      case ECSLTokenKind::Semicolon:
        Consume();
        return;
      case ECSLTokenKind::Eof:
        return;
      default:
        Consume();
      }
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
      switch (m_tokens[i].m_kind) {
      case ECSLTokenKind::LParen:
        ++depth;
        break;
      case ECSLTokenKind::RParen:
        --depth;
        if (depth == 0) {
          return i;
        }
        break;
      case ECSLTokenKind::Eof:
        return m_tokens.size();
      default:
        break;
      }
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
  /// A term is a C expression fragment (sequence of C/C++ tokens), including
  /// the special `\result` token which maps to `ECSLExpr::Result`.
  ECSLTerm ParseTerm() {
    // Collect a C expression fragment.  Track parenthesis depth so that
    // parenthesised arithmetic sub-expressions like (x + y) are not split at
    // a relational operator that follows the closing paren.
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
      EmitError(Current(), "expected expression term");
      return ECSLTerm::MakeExpr(nullptr, SourceRange{});
    }

    llvm::ArrayRef<ECSLToken> span(m_tokens.data() + start, m_pos - start);
    std::unique_ptr<ECSLExpr> expr =
        ECSLExprParser(span, m_base_loc).ParseExpr();
    if (!expr)
      EmitError(m_tokens[start], "invalid C expression");

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
          EmitError(Current(), "expected ')' after predicate");
        return pred;
      }
    }
    return ParseComparison();
  }

  /// Parse a comparison: <term> rel-op <term>  OR  bare <term> (C predicate).
  std::unique_ptr<ECSLPred> ParseComparison() {
    SourceLocation start_loc = LocStart(Current());
    ECSLTerm lhs = ParseTerm();

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
      // No relational operator: treat as a bare boolean expression.
      // A null m_expr means ParseTerm already emitted an error.
      SourceRange range(start_loc, lhs.Loc().getEnd());
      std::unique_ptr<ECSLExpr> cexpr = lhs.TakeExpr();
      if (!cexpr)
        return nullptr;
      return ECSLPred::MakeBoolExpr(std::move(cexpr), range);
    }

    Consume(); // consume rel-op
    // A null m_expr means ParseTerm already emitted an error; propagate the
    // failure so the enclosing clause parser recovers.
    if (std::get<ECSLTerm::Expr>(lhs.Val()).m_expr == nullptr)
      return nullptr;
    ECSLTerm rhs = ParseTerm();
    if (std::get<ECSLTerm::Expr>(rhs.Val()).m_expr == nullptr)
      return nullptr;
    SourceRange range(start_loc, rhs.Loc().getEnd());
    return ECSLPred::MakeRel(std::move(lhs), op, std::move(rhs), range);
  }

  /// Parse a unary predicate: '!' <unary>  or  <atom>.
  std::unique_ptr<ECSLPred> ParseUnary() {
    if (Current().m_kind == ECSLTokenKind::Bang) {
      ECSLToken tok = Consume();
      auto operand = ParseUnary();
      if (!operand)
        return nullptr;
      SourceRange range(LocStart(tok), operand->Loc().getEnd());
      return ECSLPred::MakeNot(std::move(operand), range);
    }
    return ParseAtom();
  }

  /// Parse a conjunction: <unary> ('&&' <unary>)*.
  std::unique_ptr<ECSLPred> ParseAnd() {
    auto lhs = ParseUnary();
    if (!lhs)
      return nullptr;
    while (!AtEnd() && Current().m_kind == ECSLTokenKind::AmpAmp) {
      Consume(); // '&&'
      auto rhs = ParseUnary();
      if (!rhs)
        return nullptr;
      SourceRange range(lhs->Loc().getBegin(), rhs->Loc().getEnd());
      lhs = ECSLPred::MakeAnd(std::move(lhs), std::move(rhs), range);
    }
    return lhs;
  }

  /// Parse a disjunction: <and> ('||' <and>)*.
  std::unique_ptr<ECSLPred> ParseOr() {
    auto lhs = ParseAnd();
    if (!lhs)
      return nullptr;
    while (!AtEnd() && Current().m_kind == ECSLTokenKind::PipePipe) {
      Consume(); // '||'
      auto rhs = ParseAnd();
      if (!rhs)
        return nullptr;
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
    if (!pred) {
      SkipToSemi();
      return std::nullopt;
    }

    SourceRange range(start, LocStart(Current()));
    if (!AtEnd() && Current().m_kind == ECSLTokenKind::Semicolon) {
      Consume();
    } else {
      EmitError(Current(), "expected ';' after requires predicate");
      SkipToSemi();
      return std::nullopt;
    }

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
    if (!pred) {
      SkipToSemi();
      return std::nullopt;
    }

    SourceRange range(start, LocStart(Current()));
    if (!AtEnd() && Current().m_kind == ECSLTokenKind::Semicolon) {
      Consume();
    } else {
      EmitError(Current(), "expected ';' after ensures predicate");
      SkipToSemi();
      return std::nullopt;
    }

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
      EmitError(Current(),
                "expected '\\nothing' after 'assigns' (M1 supports only "
                "'assigns \\nothing')");
      SkipToSemi();
      return std::nullopt;
    }
    Consume(); // '\nothing'

    SourceRange range(start, LocStart(Current()));
    if (!AtEnd() && Current().m_kind == ECSLTokenKind::Semicolon) {
      Consume();
    } else {
      EmitError(Current(), "expected ';' after assigns \\nothing");
      SkipToSemi();
      return std::nullopt;
    }

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
        }
        // ParseRequiresClause calls SkipToSemi() internally on nullopt.
        continue;
      }

      if (text == "ensures") {
        if (auto clause = ParseEnsuresClause()) {
          contract.AddEnsures(std::move(*clause));
          saw_any_clause = true;
        }
        // ParseEnsuresClause calls SkipToSemi() internally on nullopt.
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
                                  DiagnosticsEngine *Diags) {
  assert(Loc.isValid() &&
         "ParseFunctionContract requires a valid SourceLocation"
         "; use getFromRawEncoding(1) when location info is "
         "not needed");
  llvm::SmallVector<ECSLToken> tokens;
  ECSLLexer lexer(Text, Loc);
  lexer.Lex(tokens);

  ECSLParserImpl impl(tokens, Loc, Diags);
  return impl.ParseFunctionContract();
}

std::unique_ptr<ECSLExpr> ECSLParser::ParseExpr(llvm::StringRef Text,
                                                SourceLocation Loc) {
  llvm::SmallVector<ECSLToken> tokens;
  ECSLLexer lexer(Text, Loc);
  lexer.Lex(tokens);
  ECSLExprParser parser(tokens, Loc);
  return parser.ParseExpr();
}

// ---------------------------------------------------------------------------
// Backward-compatibility entry point used by the clangParse dispatch hooks
// ---------------------------------------------------------------------------

PendingAnnotation ecsl::parseECSLAnnotation(PendingAnnotation PA) {
  ECSLParser parser;
  SourceLocation body_loc = PA.Loc;
  if (body_loc.isValid()) {
    body_loc = body_loc.getLocWithOffset(3);
  }
  parser.ParseFunctionContract(PA.Body, body_loc);
  return PA;
}
