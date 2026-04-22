//===- clang/ECSL/ECSLAst.h - ECSL annotation AST nodes -------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
///
/// \file
/// Declares the ECSL annotation AST node types produced by the ECSL parser:
/// ECSLExpr, ECSLTerm, ECSLPred, and ECSLFunctionContract.
///
//===----------------------------------------------------------------------===//

#ifndef LLVM_CLANG_ECSL_ECSLAST_H
#define LLVM_CLANG_ECSL_ECSLAST_H

#include "clang/Basic/SourceLocation.h"
#include "llvm/ADT/StringRef.h"
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <variant>
#include <vector>

namespace clang {
namespace ecsl {

/// Clause modifier applied to requires/ensures clauses.
enum class ClauseModifier {
  None,  ///< No modifier (default).
  Check, ///< check: verify the clause, do not assume it.
  Admit, ///< admit: assume without verification.
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

/// An expression fragment parsed by the ECSL expression parser.
///
/// Move-only due to unique_ptr children.
struct ECSLExpr {
  struct IntLit {
    std::string m_val;
  };
  struct BoolLit {
    bool m_val = false;
  };
  struct Ident {
    std::string m_name;
  };
  struct BinOp {
    ExprOp m_op = ExprOp::Add;
    std::unique_ptr<ECSLExpr> m_lhs;
    std::unique_ptr<ECSLExpr> m_rhs;
  };
  struct UnaryOp {
    ExprOp m_op = ExprOp::Neg;
    std::unique_ptr<ECSLExpr> m_operand;
  };

  std::variant<IntLit, BoolLit, Ident, BinOp, UnaryOp> m_val;
  SourceRange m_loc;

  ECSLExpr() = default;
  ~ECSLExpr() = default;
  ECSLExpr(ECSLExpr &&) = default;
  ECSLExpr &operator=(ECSLExpr &&) = default;

  static std::unique_ptr<ECSLExpr> MakeIntLit(std::string Val,
                                              SourceRange Loc) {
    auto E = std::make_unique<ECSLExpr>();
    E->m_val = IntLit{std::move(Val)};
    E->m_loc = Loc;
    return E;
  }

  static std::unique_ptr<ECSLExpr> MakeBoolLit(bool V, SourceRange Loc) {
    auto E = std::make_unique<ECSLExpr>();
    E->m_val = BoolLit{V};
    E->m_loc = Loc;
    return E;
  }

  static std::unique_ptr<ECSLExpr> MakeIdent(llvm::StringRef Name,
                                             SourceRange Loc) {
    auto E = std::make_unique<ECSLExpr>();
    E->m_val = Ident{Name.str()};
    E->m_loc = Loc;
    return E;
  }

  static std::unique_ptr<ECSLExpr> MakeBinOp(ExprOp Op,
                                             std::unique_ptr<ECSLExpr> L,
                                             std::unique_ptr<ECSLExpr> R,
                                             SourceRange Loc) {
    auto E = std::make_unique<ECSLExpr>();
    E->m_val = BinOp{Op, std::move(L), std::move(R)};
    E->m_loc = Loc;
    return E;
  }

  static std::unique_ptr<ECSLExpr>
  MakeUnary(ExprOp Op, std::unique_ptr<ECSLExpr> Operand, SourceRange Loc) {
    auto E = std::make_unique<ECSLExpr>();
    E->m_val = UnaryOp{Op, std::move(Operand)};
    E->m_loc = Loc;
    return E;
  }
};

/// A term in an ECSL annotation.
///
/// Terms appear as the operands of relational predicates.
struct ECSLTerm {
  struct Expr {
    std::unique_ptr<ECSLExpr> m_expr;
  };
  struct Result {};
  struct Nothing {};

  std::variant<Expr, Result, Nothing> m_val;
  SourceRange m_loc;

  ECSLTerm() = default;
  ~ECSLTerm() = default;
  ECSLTerm(ECSLTerm &&) = default;
  ECSLTerm &operator=(ECSLTerm &&) = default;

  static ECSLTerm MakeExpr(std::unique_ptr<ECSLExpr> E, SourceRange Loc) {
    ECSLTerm T;
    T.m_val = Expr{std::move(E)};
    T.m_loc = Loc;
    return T;
  }

  static ECSLTerm MakeResult(SourceRange Loc) {
    ECSLTerm T;
    T.m_val = Result{};
    T.m_loc = Loc;
    return T;
  }

  static ECSLTerm MakeNothing(SourceRange Loc) {
    ECSLTerm T;
    T.m_val = Nothing{};
    T.m_loc = Loc;
    return T;
  }
};

/// A predicate in an ECSL annotation — a propositional formula over ECSLTerms.
///
/// The tree is heap-allocated via unique_ptr to support arbitrary nesting
/// depth. Move-only due to unique_ptr members; never copy a predicate tree.
struct ECSLPred {
  struct True {};
  struct False {};
  struct Rel {
    RelOp m_op = RelOp::Eq;
    ECSLTerm m_lhs;
    ECSLTerm m_rhs;
  };
  struct And {
    std::unique_ptr<ECSLPred> m_left;
    std::unique_ptr<ECSLPred> m_right;
  };
  struct Or {
    std::unique_ptr<ECSLPred> m_left;
    std::unique_ptr<ECSLPred> m_right;
  };
  struct Not {
    std::unique_ptr<ECSLPred> m_operand;
  };

  std::variant<True, False, Rel, And, Or, Not> m_val;
  SourceRange m_loc;

  ECSLPred() = default;
  ~ECSLPred() = default;
  ECSLPred(ECSLPred &&) = default;
  ECSLPred &operator=(ECSLPred &&) = default;

  static std::unique_ptr<ECSLPred> MakeTrue(SourceRange Loc) {
    auto P = std::make_unique<ECSLPred>();
    P->m_val = True{};
    P->m_loc = Loc;
    return P;
  }

  static std::unique_ptr<ECSLPred> MakeFalse(SourceRange Loc) {
    auto P = std::make_unique<ECSLPred>();
    P->m_val = False{};
    P->m_loc = Loc;
    return P;
  }

  static std::unique_ptr<ECSLPred> MakeRel(ECSLTerm Lhs, RelOp Op, ECSLTerm Rhs,
                                           SourceRange Loc) {
    auto P = std::make_unique<ECSLPred>();
    P->m_val = Rel{Op, std::move(Lhs), std::move(Rhs)};
    P->m_loc = Loc;
    return P;
  }

  static std::unique_ptr<ECSLPred> MakeAnd(std::unique_ptr<ECSLPred> Left,
                                           std::unique_ptr<ECSLPred> Right,
                                           SourceRange Loc) {
    auto P = std::make_unique<ECSLPred>();
    P->m_val = And{std::move(Left), std::move(Right)};
    P->m_loc = Loc;
    return P;
  }

  static std::unique_ptr<ECSLPred> MakeOr(std::unique_ptr<ECSLPred> Left,
                                          std::unique_ptr<ECSLPred> Right,
                                          SourceRange Loc) {
    auto P = std::make_unique<ECSLPred>();
    P->m_val = Or{std::move(Left), std::move(Right)};
    P->m_loc = Loc;
    return P;
  }

  static std::unique_ptr<ECSLPred> MakeNot(std::unique_ptr<ECSLPred> Operand,
                                           SourceRange Loc) {
    auto P = std::make_unique<ECSLPred>();
    P->m_val = Not{std::move(Operand)};
    P->m_loc = Loc;
    return P;
  }
};

/// A parsed function contract for ECSL annotations.
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

  /// An assigns \nothing clause.
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
