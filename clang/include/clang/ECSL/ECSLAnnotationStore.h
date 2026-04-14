//===- clang/ECSL/ECSLAnnotationStore.h - ECSL annotation store -----------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
///
/// \file
/// Declares ECSLAnnotationStore, a DenseMap-based store that associates
/// ECSL annotations with the Clang AST nodes they annotate.
///
/// The store is owned by SyntaxOnlyAction and registered on the Preprocessor
/// via PP.setECSLAnnotationStore() so that parser hooks can reach it through
/// PP.getECSLAnnotationStore().
///
//===----------------------------------------------------------------------===//

#ifndef LLVM_CLANG_ECSL_ECSLANNOTATIONSTORE_H
#define LLVM_CLANG_ECSL_ECSLANNOTATIONSTORE_H

#include "clang/ECSL/ECSLCommentHandler.h"
#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/DenseMap.h"
#include <vector>

namespace clang {
class CompilerInstance;
class Decl;
class Stmt;

namespace ecsl {

/// Associates ECSL annotation bodies with the Clang AST nodes they annotate.
///
/// Annotations are stored in two maps keyed by raw pointer identity:
///   - \c Decl* for function contracts, class invariants, etc.
///   - \c Stmt* for loop annotations and statement contracts.
///
/// The store is not populated by this class itself; parser hooks (PR 4/5)
/// drain the ECSLCommentHandler buffer at declaration and statement boundaries
/// and call addForDecl()/addForStmt().
///
/// Lifetime: owned by SyntaxOnlyAction for the duration of one source file.
class ECSLAnnotationStore {
public:
  ECSLAnnotationStore() = default;

  /// Append \p Annotations to the entry for \p D.
  void addForDecl(const Decl *D, std::vector<PendingAnnotation> Annotations);

  /// Append \p Annotations to the entry for \p S.
  void addForStmt(const Stmt *S, std::vector<PendingAnnotation> Annotations);

  /// Return the annotations associated with \p D, or an empty array.
  llvm::ArrayRef<PendingAnnotation> getForDecl(const Decl *D) const;

  /// Return the annotations associated with \p S, or an empty array.
  llvm::ArrayRef<PendingAnnotation> getForStmt(const Stmt *S) const;

  /// Retrieve the store registered on \p CI's preprocessor.
  /// Returns null when -fecsl is not active.
  static ECSLAnnotationStore *get(CompilerInstance &CI);

private:
  llvm::DenseMap<const Decl *, std::vector<PendingAnnotation>> DeclAnnotations;
  llvm::DenseMap<const Stmt *, std::vector<PendingAnnotation>> StmtAnnotations;
};

} // namespace ecsl
} // namespace clang

#endif // LLVM_CLANG_ECSL_ECSLANNOTATIONSTORE_H
