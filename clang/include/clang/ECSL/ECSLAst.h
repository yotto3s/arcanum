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
/// ECSLTerm, ECSLPred, and ECSLFunctionContract.
///
/// M1 scope covers: requires, ensures, assigns \nothing, \result, \nothing,
/// relational and logical predicate operators, and arithmetic terms delegated
/// to Clang's expression parser.
///
/// Node kinds are defined as enumerators inside each struct so the set can be
/// extended in later milestones (M2+) without breaking existing code.
///
//===----------------------------------------------------------------------===//

#ifndef LLVM_CLANG_ECSL_ECSLAST_H
#define LLVM_CLANG_ECSL_ECSLAST_H

#include "clang/Basic/SourceLocation.h"
#include <memory>
#include <optional>
#include <vector>

namespace clang {
class Expr; // Forward-declared; ECSLTerm stores Clang Expr* without pulling in
            // clangAST headers.
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

/// A term in an ECSL annotation.
///
/// In M1, terms appear as the operands of relational predicates. Arithmetic
/// subexpressions (+, -, *, /, %) are wholly inside m_c_expr and are parsed
/// by Clang's expression parser before the ECSLTerm is constructed.
struct ECSLTerm {
  enum class Kind {
    CExpr,   ///< Delegated C/C++ expression stored in m_c_expr.
    Result,  ///< \result — the return value of the annotated function.
    Nothing, ///< \nothing — the empty location set; used in assigns \nothing.
  };

  Kind m_kind = Kind::CExpr;
  clang::Expr *m_c_expr = nullptr; ///< Non-null iff m_kind == Kind::CExpr.
  SourceRange m_loc;

  static ECSLTerm MakeCExpr(clang::Expr *E, SourceRange Loc) {
    ECSLTerm T;
    T.m_kind = Kind::CExpr;
    T.m_c_expr = E;
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
    /// A delegated C/C++ expression used as a boolean predicate (e.g. a bare
    /// identifier or function call without an explicit relational operator).
    CExpr,
    Rel, ///< Relational comparison: m_lhs m_rel_op m_rhs.
    And, ///< Conjunction: m_left && m_right.
    Or,  ///< Disjunction: m_left || m_right.
    Not, ///< Negation: !m_operand.
  };

  Kind m_kind = Kind::True;
  SourceRange m_loc;

  /// Clang Expr* used as a boolean predicate. Valid when m_kind == Kind::CExpr.
  clang::Expr *m_c_pred_expr = nullptr;

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

  /// Create a CExpr predicate (a delegated C/C++ expression used as a boolean).
  static std::unique_ptr<ECSLPred> MakeCExprPred(clang::Expr *E,
                                                 SourceRange Loc) {
    auto P = std::make_unique<ECSLPred>();
    P->m_kind = Kind::CExpr;
    P->m_c_pred_expr = E;
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
