//===- clang/ECSL/ECSLAst.h - M1 ECSL annotation AST nodes ---------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
///
/// \file
/// Declares the M1 ECSL annotation AST node types produced by the ECSL parser:
/// ECSLExpr, ECSLTerm, ECSLPred, and ECSLFunctionContract.
///
/// M1 scope covers: requires, ensures, assigns \nothing, \result, \nothing,
/// relational and logical predicate operators, and arithmetic terms parsed by
/// ECSL's own recursive-descent C expression parser (no Clang delegation).
///
/// Node kinds are defined as enumerators inside each struct so the set can be
/// extended in later milestones (M2+) without breaking existing code.
///
//===----------------------------------------------------------------------===//

#ifndef LLVM_CLANG_ECSL_ECSLAST_H
#define LLVM_CLANG_ECSL_ECSLAST_H

#include "clang/Basic/SourceLocation.h"
#include "llvm/ADT/StringRef.h"
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace clang {
namespace ecsl {

/// Clause modifier applied to requires/ensures clauses.
enum class ClauseModifier {
  None,  ///< No modifier (default).
  Check, ///< check: verify the clause, do not assume it (M3).
  Admit, ///< admit: assume without verification (M3).
};

/// Relational operator used in an ECSLPred::Rel comparison.
enum class RelOp {
  Eq, ///< ==
  Ne, ///< !=
  Lt, ///< <
  Le, ///< <=
  Gt, ///< >
  Ge, ///< >=
};

// Forward-declared so ECSLPred can contain std::unique_ptr<ECSLPred>.
struct ECSLPred;

/// Arithmetic/logical operator used in binary or unary expressions.
enum class ExprOp {
  Add, ///< +
  Sub, ///< -
  Mul, ///< *
  Div, ///< /
  Mod, ///< %
  Neg, ///< unary -
};

// Forward-declared so ECSLExpr can contain std::unique_ptr<ECSLExpr>.
struct ECSLExpr;

/// A C expression fragment parsed by the ECSL expression parser.
///
/// Replaces the old clang::Expr* delegation approach. All arithmetic and
/// identifier subexpressions inside ECSL contracts are represented with this
/// type rather than delegated to Clang's parser.
///
/// Move-only due to unique_ptr children.
struct ECSLExpr {
  enum class Kind {
    IntLit,  ///< Integer literal: m_int_val.
    BoolLit, ///< Boolean literal: m_bool_val.
    Ident,   ///< Variable reference: m_name.
    BinOp,   ///< Binary op: m_op, m_lhs, m_rhs.
    UnaryOp, ///< Unary op: m_op, m_lhs (m_rhs is null).
  };

  Kind m_kind;
  long long m_int_val = 0;         ///< Valid when m_kind == IntLit.
  bool m_bool_val = false;         ///< Valid when m_kind == BoolLit.
  std::string m_name;              ///< Valid when m_kind == Ident.
  ExprOp m_op = ExprOp::Add;       ///< Valid when m_kind == BinOp/UnaryOp.
  std::unique_ptr<ECSLExpr> m_lhs; ///< Valid when m_kind == BinOp/UnaryOp.
  std::unique_ptr<ECSLExpr>
      m_rhs; ///< Valid when m_kind == BinOp (null for UnaryOp).
  SourceRange m_loc;

  ECSLExpr() = default;
  ~ECSLExpr() = default;
  ECSLExpr(ECSLExpr &&) = default;
  ECSLExpr &operator=(ECSLExpr &&) = default;

  static std::unique_ptr<ECSLExpr> MakeIntLit(long long V, SourceRange Loc) {
    auto E = std::make_unique<ECSLExpr>();
    E->m_kind = Kind::IntLit;
    E->m_int_val = V;
    E->m_loc = Loc;
    return E;
  }

  static std::unique_ptr<ECSLExpr> MakeBoolLit(bool V, SourceRange Loc) {
    auto E = std::make_unique<ECSLExpr>();
    E->m_kind = Kind::BoolLit;
    E->m_bool_val = V;
    E->m_loc = Loc;
    return E;
  }

  static std::unique_ptr<ECSLExpr> MakeIdent(llvm::StringRef Name,
                                             SourceRange Loc) {
    auto E = std::make_unique<ECSLExpr>();
    E->m_kind = Kind::Ident;
    E->m_name = Name.str();
    E->m_loc = Loc;
    return E;
  }

  static std::unique_ptr<ECSLExpr> MakeBinOp(ExprOp Op,
                                             std::unique_ptr<ECSLExpr> L,
                                             std::unique_ptr<ECSLExpr> R,
                                             SourceRange Loc) {
    auto E = std::make_unique<ECSLExpr>();
    E->m_kind = Kind::BinOp;
    E->m_op = Op;
    E->m_lhs = std::move(L);
    E->m_rhs = std::move(R);
    E->m_loc = Loc;
    return E;
  }

  static std::unique_ptr<ECSLExpr>
  MakeUnary(ExprOp Op, std::unique_ptr<ECSLExpr> Operand, SourceRange Loc) {
    auto E = std::make_unique<ECSLExpr>();
    E->m_kind = Kind::UnaryOp;
    E->m_op = Op;
    E->m_lhs = std::move(Operand);
    E->m_loc = Loc;
    return E;
  }
};

/// A term in an ECSL annotation.
///
/// In M1, terms appear as the operands of relational predicates. Arithmetic
/// subexpressions (+, -, *, /, %) are represented as ECSLExpr trees parsed by
/// ECSL's own expression parser (no Clang delegation).
struct ECSLTerm {
  enum class Kind {
    CExpr,   ///< C expression stored in m_c_expr, parsed by ECSL expr parser.
    Result,  ///< \result — the return value of the annotated function.
    Nothing, ///< \nothing — the empty location set; used in assigns \nothing.
  };

  Kind m_kind = Kind::CExpr;
  std::unique_ptr<ECSLExpr> m_c_expr; ///< Non-null iff m_kind == Kind::CExpr.
  SourceRange m_loc;

  ECSLTerm() = default;
  ~ECSLTerm() = default;
  ECSLTerm(ECSLTerm &&) = default;
  ECSLTerm &operator=(ECSLTerm &&) = default;

  static ECSLTerm MakeCExpr(std::unique_ptr<ECSLExpr> E, SourceRange Loc) {
    ECSLTerm T;
    T.m_kind = Kind::CExpr;
    T.m_c_expr = std::move(E);
    T.m_loc = Loc;
    return T;
  }

  static ECSLTerm MakeResult(SourceRange Loc) {
    ECSLTerm T;
    T.m_kind = Kind::Result;
    T.m_loc = Loc;
    return T;
  }

  static ECSLTerm MakeNothing(SourceRange Loc) {
    ECSLTerm T;
    T.m_kind = Kind::Nothing;
    T.m_loc = Loc;
    return T;
  }
};

/// A predicate in an ECSL annotation — a propositional formula over ECSLTerms.
///
/// The tree is heap-allocated via unique_ptr to support arbitrary nesting
/// depth. Move-only due to unique_ptr members; never copy a predicate tree.
///
/// M1 kinds: True, False, CExpr, Rel, And, Or, Not.
struct ECSLPred {
  enum class Kind {
    True,  ///< Logical true (\true, or predicate that is trivially satisfied).
    False, ///< Logical false (\false).
    /// A C expression used as a boolean predicate (e.g. a bare identifier
    /// or function call without an explicit relational operator), parsed by
    /// ECSL's own expression parser.
    CExpr,
    Rel, ///< Relational comparison: m_lhs m_rel_op m_rhs.
    And, ///< Conjunction: m_left && m_right.
    Or,  ///< Disjunction: m_left || m_right.
    Not, ///< Negation: !m_operand.
  };

  Kind m_kind = Kind::True;
  SourceRange m_loc;

  /// C expression used as a boolean predicate. Valid when m_kind ==
  /// Kind::CExpr.
  std::unique_ptr<ECSLExpr> m_c_pred_expr;

  /// Relational operator. Valid when m_kind == Kind::Rel.
  RelOp m_rel_op = RelOp::Eq;
  /// Left operand. Valid when m_kind == Kind::Rel.
  ECSLTerm m_lhs;
  /// Right operand. Valid when m_kind == Kind::Rel.
  ECSLTerm m_rhs;

  /// Left sub-predicate. Valid when m_kind == Kind::And or Kind::Or.
  std::unique_ptr<ECSLPred> m_left;
  /// Right sub-predicate. Valid when m_kind == Kind::And or Kind::Or.
  std::unique_ptr<ECSLPred> m_right;

  /// Sub-predicate to negate. Valid when m_kind == Kind::Not.
  std::unique_ptr<ECSLPred> m_operand;

  ECSLPred() = default;
  ~ECSLPred() = default;
  ECSLPred(ECSLPred &&) = default;
  ECSLPred &operator=(ECSLPred &&) = default;

  static std::unique_ptr<ECSLPred> MakeTrue(SourceRange Loc) {
    auto P = std::make_unique<ECSLPred>();
    P->m_kind = Kind::True;
    P->m_loc = Loc;
    return P;
  }

  static std::unique_ptr<ECSLPred> MakeFalse(SourceRange Loc) {
    auto P = std::make_unique<ECSLPred>();
    P->m_kind = Kind::False;
    P->m_loc = Loc;
    return P;
  }

  static std::unique_ptr<ECSLPred> MakeRel(ECSLTerm Lhs, RelOp Op, ECSLTerm Rhs,
                                           SourceRange Loc) {
    auto P = std::make_unique<ECSLPred>();
    P->m_kind = Kind::Rel;
    P->m_rel_op = Op;
    P->m_lhs = std::move(Lhs);
    P->m_rhs = std::move(Rhs);
    P->m_loc = Loc;
    return P;
  }

  static std::unique_ptr<ECSLPred> MakeAnd(std::unique_ptr<ECSLPred> Left,
                                           std::unique_ptr<ECSLPred> Right,
                                           SourceRange Loc) {
    auto P = std::make_unique<ECSLPred>();
    P->m_kind = Kind::And;
    P->m_left = std::move(Left);
    P->m_right = std::move(Right);
    P->m_loc = Loc;
    return P;
  }

  static std::unique_ptr<ECSLPred> MakeOr(std::unique_ptr<ECSLPred> Left,
                                          std::unique_ptr<ECSLPred> Right,
                                          SourceRange Loc) {
    auto P = std::make_unique<ECSLPred>();
    P->m_kind = Kind::Or;
    P->m_left = std::move(Left);
    P->m_right = std::move(Right);
    P->m_loc = Loc;
    return P;
  }

  static std::unique_ptr<ECSLPred> MakeNot(std::unique_ptr<ECSLPred> Operand,
                                           SourceRange Loc) {
    auto P = std::make_unique<ECSLPred>();
    P->m_kind = Kind::Not;
    P->m_operand = std::move(Operand);
    P->m_loc = Loc;
    return P;
  }

  /// Create a CExpr predicate (a C expression used as a boolean predicate).
  static std::unique_ptr<ECSLPred> MakeCExprPred(std::unique_ptr<ECSLExpr> E,
                                                 SourceRange Loc) {
    auto P = std::make_unique<ECSLPred>();
    P->m_kind = Kind::CExpr;
    P->m_c_pred_expr = std::move(E);
    P->m_loc = Loc;
    return P;
  }
};

/// A parsed function contract for M1 ECSL annotations.
///
/// Represents the contract attached to a single FunctionDecl. A contract
/// consists of zero or more requires/ensures clauses and an optional
/// assigns \nothing clause.
///
/// Move-only: contains unique_ptr members transitively via RequiresClause and
/// EnsuresClause.
struct ECSLFunctionContract {
  /// A single requires clause: requires <pred>;
  struct RequiresClause {
    ClauseModifier m_mod = ClauseModifier::None;
    std::unique_ptr<ECSLPred> m_pred;
    SourceRange m_loc;
  };

  /// A single ensures clause: ensures <pred>;
  struct EnsuresClause {
    ClauseModifier m_mod = ClauseModifier::None;
    std::unique_ptr<ECSLPred> m_pred;
    SourceRange m_loc;
  };

  /// An assigns \nothing clause.  M1 supports only \nothing (no location
  /// list); the full location grammar is introduced in M2.
  struct AssignsNothingClause {
    SourceRange m_loc;
  };

  std::vector<RequiresClause> m_requires;
  std::vector<EnsuresClause> m_ensures;
  std::optional<AssignsNothingClause> m_assigns_nothing;
  SourceRange m_loc;
};

} // namespace ecsl
} // namespace clang

#endif // LLVM_CLANG_ECSL_ECSLAST_H
