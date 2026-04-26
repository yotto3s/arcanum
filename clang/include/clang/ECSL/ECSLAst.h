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

private:
  std::variant<IntLit, BoolLit, Ident, BinOp, UnaryOp> m_val;
  SourceRange m_loc;

public:
  ECSLExpr(std::variant<IntLit, BoolLit, Ident, BinOp, UnaryOp> Val,
           SourceRange Loc)
      : m_val(std::move(Val)), m_loc(Loc) {}
  ~ECSLExpr() = default;
  ECSLExpr(ECSLExpr &&) = default;
  ECSLExpr &operator=(ECSLExpr &&) = default;

  const std::variant<IntLit, BoolLit, Ident, BinOp, UnaryOp> &Val() const {
    return m_val;
  }
  SourceRange Loc() const { return m_loc; }

  static std::unique_ptr<ECSLExpr> MakeIntLit(std::string Val,
                                              SourceRange Loc) {
    return std::make_unique<ECSLExpr>(IntLit{std::move(Val)}, Loc);
  }

  static std::unique_ptr<ECSLExpr> MakeBoolLit(bool V, SourceRange Loc) {
    return std::make_unique<ECSLExpr>(BoolLit{V}, Loc);
  }

  static std::unique_ptr<ECSLExpr> MakeIdent(llvm::StringRef Name,
                                             SourceRange Loc) {
    return std::make_unique<ECSLExpr>(Ident{Name.str()}, Loc);
  }

  static std::unique_ptr<ECSLExpr> MakeBinOp(ExprOp Op,
                                             std::unique_ptr<ECSLExpr> L,
                                             std::unique_ptr<ECSLExpr> R,
                                             SourceRange Loc) {
    return std::make_unique<ECSLExpr>(BinOp{Op, std::move(L), std::move(R)},
                                      Loc);
  }

  static std::unique_ptr<ECSLExpr>
  MakeUnary(ExprOp Op, std::unique_ptr<ECSLExpr> Operand, SourceRange Loc) {
    return std::make_unique<ECSLExpr>(UnaryOp{Op, std::move(Operand)}, Loc);
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

private:
  std::variant<Expr, Result, Nothing> m_val;
  SourceRange m_loc;

public:
  ECSLTerm(std::variant<Expr, Result, Nothing> Val, SourceRange Loc)
      : m_val(std::move(Val)), m_loc(Loc) {}
  ~ECSLTerm() = default;
  ECSLTerm(ECSLTerm &&) = default;
  ECSLTerm &operator=(ECSLTerm &&) = default;

  const std::variant<Expr, Result, Nothing> &Val() const { return m_val; }
  SourceRange Loc() const { return m_loc; }

  static ECSLTerm MakeExpr(std::unique_ptr<ECSLExpr> E, SourceRange Loc) {
    return ECSLTerm{Expr{std::move(E)}, Loc};
  }

  static ECSLTerm MakeResult(SourceRange Loc) {
    return ECSLTerm{Result{}, Loc};
  }

  static ECSLTerm MakeNothing(SourceRange Loc) {
    return ECSLTerm{Nothing{}, Loc};
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
  /// A bare expression used as a boolean predicate, e.g. the inner
  /// predicate in `requires !flag;` where `flag` has no relational operator.
  struct BoolExpr {
    std::unique_ptr<ECSLExpr> m_expr;
  };

private:
  std::variant<True, False, Rel, And, Or, Not, BoolExpr> m_val;
  SourceRange m_loc;

public:
  ECSLPred(std::variant<True, False, Rel, And, Or, Not, BoolExpr> Val,
           SourceRange Loc)
      : m_val(std::move(Val)), m_loc(Loc) {}
  ~ECSLPred() = default;
  ECSLPred(ECSLPred &&) = default;
  ECSLPred &operator=(ECSLPred &&) = default;

  const std::variant<True, False, Rel, And, Or, Not, BoolExpr> &Val() const {
    return m_val;
  }
  SourceRange Loc() const { return m_loc; }

  static std::unique_ptr<ECSLPred> MakeTrue(SourceRange Loc) {
    return std::make_unique<ECSLPred>(True{}, Loc);
  }

  static std::unique_ptr<ECSLPred> MakeFalse(SourceRange Loc) {
    return std::make_unique<ECSLPred>(False{}, Loc);
  }

  static std::unique_ptr<ECSLPred> MakeRel(ECSLTerm Lhs, RelOp Op, ECSLTerm Rhs,
                                           SourceRange Loc) {
    return std::make_unique<ECSLPred>(Rel{Op, std::move(Lhs), std::move(Rhs)},
                                      Loc);
  }

  static std::unique_ptr<ECSLPred> MakeAnd(std::unique_ptr<ECSLPred> Left,
                                           std::unique_ptr<ECSLPred> Right,
                                           SourceRange Loc) {
    return std::make_unique<ECSLPred>(And{std::move(Left), std::move(Right)},
                                      Loc);
  }

  static std::unique_ptr<ECSLPred> MakeOr(std::unique_ptr<ECSLPred> Left,
                                          std::unique_ptr<ECSLPred> Right,
                                          SourceRange Loc) {
    return std::make_unique<ECSLPred>(Or{std::move(Left), std::move(Right)},
                                      Loc);
  }

  static std::unique_ptr<ECSLPred> MakeNot(std::unique_ptr<ECSLPred> Operand,
                                           SourceRange Loc) {
    return std::make_unique<ECSLPred>(Not{std::move(Operand)}, Loc);
  }

  static std::unique_ptr<ECSLPred> MakeBoolExpr(std::unique_ptr<ECSLExpr> E,
                                                SourceRange Loc) {
    return std::make_unique<ECSLPred>(BoolExpr{std::move(E)}, Loc);
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

  ECSLFunctionContract() = default;

  void AddRequires(RequiresClause Clause) {
    m_requires.push_back(std::move(Clause));
  }
  void AddEnsures(EnsuresClause Clause) {
    m_ensures.push_back(std::move(Clause));
  }
  void SetAssignsNothing(AssignsNothingClause Clause) {
    m_assigns_nothing = std::move(Clause);
  }

  size_t RequiresCount() const { return m_requires.size(); }
  size_t EnsuresCount() const { return m_ensures.size(); }
  bool HasAssignsNothing() const { return m_assigns_nothing.has_value(); }
  SourceRange Loc() const { return m_loc; }

  const RequiresClause &RequiresAt(size_t I) const { return m_requires[I]; }
  const EnsuresClause &EnsuresAt(size_t I) const { return m_ensures[I]; }

private:
  std::vector<RequiresClause> m_requires;
  std::vector<EnsuresClause> m_ensures;
  std::optional<AssignsNothingClause> m_assigns_nothing;
  SourceRange m_loc;
};

} // namespace ecsl
} // namespace clang

#endif // LLVM_CLANG_ECSL_ECSLAST_H
