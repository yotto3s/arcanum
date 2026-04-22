//===- clang/ECSL/ECSLAnnotationStore.cpp - ECSL annotation store ---------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "clang/ECSL/ECSLAnnotationStore.h"
#include "clang/Frontend/CompilerInstance.h"
#include "clang/Lex/Preprocessor.h"
#include <iterator>

using namespace clang;
using namespace clang::ecsl;

void ECSLAnnotationStore::addForDecl(
    const Decl *D, std::vector<PendingAnnotation> Annotations) {
  auto &Slot = DeclAnnotations[D];
  Slot.insert(Slot.end(), std::make_move_iterator(Annotations.begin()),
              std::make_move_iterator(Annotations.end()));
}

void ECSLAnnotationStore::addForStmt(
    const Stmt *S, std::vector<PendingAnnotation> Annotations) {
  auto &Slot = StmtAnnotations[S];
  Slot.insert(Slot.end(), std::make_move_iterator(Annotations.begin()),
              std::make_move_iterator(Annotations.end()));
}

llvm::ArrayRef<PendingAnnotation>
ECSLAnnotationStore::getForDecl(const Decl *D) const {
  auto It = DeclAnnotations.find(D);
  if (It == DeclAnnotations.end())
    return {};
  return It->second;
}

llvm::ArrayRef<PendingAnnotation>
ECSLAnnotationStore::getForStmt(const Stmt *S) const {
  auto It = StmtAnnotations.find(S);
  if (It == StmtAnnotations.end())
    return {};
  return It->second;
}

ECSLAnnotationStore *ECSLAnnotationStore::get(CompilerInstance &CI) {
  return CI.getPreprocessor().getECSLAnnotationStore();
}
