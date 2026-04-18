//===- ECSLStmtDispatchTest.cpp - Unit tests for PR5 Stmt dispatch --------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
///
/// \file
/// Tests for ECSL annotation dispatch at statement boundaries: annotation
/// comments buffered by ECSLCommentHandler are drained after ParseDeclaration
/// inside ParseStatementOrDeclarationAfterAttributes and stored in
/// ECSLAnnotationStore keyed by the inner Decl (e.g. VarDecl), not the
/// wrapping DeclStmt.
///
//===----------------------------------------------------------------------===//

#include "clang/AST/ASTConsumer.h"
#include "clang/AST/ASTContext.h"
#include "clang/AST/Decl.h"
#include "clang/AST/Stmt.h"
#include "clang/ECSL/ECSLAnnotationStore.h"
#include "clang/ECSL/ECSLCommentHandler.h"
#include "clang/Frontend/CompilerInstance.h"
#include "clang/Frontend/FrontendActions.h"
#include "clang/Lex/Preprocessor.h"
#include "clang/Tooling/Tooling.h"
#include "gtest/gtest.h"
#include <functional>
#include <string>
#include <vector>

using namespace clang;
using namespace clang::ecsl;
using namespace clang::tooling;

// ---------------------------------------------------------------------------
// Test infrastructure (mirrors ECSLDeclDispatchTest pattern)
// ---------------------------------------------------------------------------

using StoreInspector =
    std::function<void(ASTContext &, ecsl::ECSLAnnotationStore &)>;

class ECSLStmtConsumer : public ASTConsumer {
public:
  ECSLStmtConsumer(CompilerInstance &CI, StoreInspector Inspector)
      : m_ci(CI), m_inspector(std::move(Inspector)) {}

  void HandleTranslationUnit(ASTContext &Ctx) override {
    ecsl::ECSLAnnotationStore *Store =
        m_ci.getPreprocessor().getECSLAnnotationStore();
    ASSERT_NE(Store, nullptr)
        << "ECSLAnnotationStore should be registered when -fecsl is active";
    m_inspector(Ctx, *Store);
  }

private:
  CompilerInstance &m_ci;
  StoreInspector m_inspector;
};

class ECSLStmtAction : public SyntaxOnlyAction {
public:
  explicit ECSLStmtAction(StoreInspector Inspector)
      : m_inspector(std::move(Inspector)) {}

protected:
  std::unique_ptr<ASTConsumer> CreateASTConsumer(CompilerInstance &CI,
                                                 StringRef) override {
    return std::make_unique<ECSLStmtConsumer>(CI, m_inspector);
  }

private:
  StoreInspector m_inspector;
};

static bool runWithECSL(StringRef Code, StoreInspector Inspector) {
  return runToolOnCodeWithArgs(
      std::make_unique<ECSLStmtAction>(std::move(Inspector)), Code,
      {"-fecsl", "-x", "c"}, "input.c");
}

/// Walk the body of a named function and return the first VarDecl with the
/// given name, or nullptr if not found.
static const VarDecl *findLocalVar(ASTContext &Ctx, StringRef FuncName,
                                   StringRef VarName) {
  for (Decl *D : Ctx.getTranslationUnitDecl()->decls()) {
    auto *FD = dyn_cast<FunctionDecl>(D);
    if (!FD || FD->getNameAsString() != FuncName || !FD->hasBody())
      continue;
    auto *Body = dyn_cast<CompoundStmt>(FD->getBody());
    if (!Body)
      continue;
    for (Stmt *S : Body->body()) {
      auto *DS = dyn_cast<DeclStmt>(S);
      if (!DS)
        continue;
      for (auto *Decl : DS->decls()) {
        auto *VD = dyn_cast<VarDecl>(Decl);
        if (VD && VD->getNameAsString() == VarName)
          return VD;
      }
    }
  }
  return nullptr;
}

// ---------------------------------------------------------------------------
// Tests
// ---------------------------------------------------------------------------

TEST(ECSLStmtDispatch, BlockAnnotationAttachesToVarDecl) {
  bool InspectorRan = false;
  bool OK = runWithECSL("void f(void) { /*@ requires x > 0; */\nint x = 1; }",
                        [&](ASTContext &Ctx, ECSLAnnotationStore &Store) {
                          InspectorRan = true;
                          const VarDecl *VD = findLocalVar(Ctx, "f", "x");
                          ASSERT_NE(VD, nullptr)
                              << "VarDecl 'x' not found in 'f'";
                          auto Annots = Store.getForDecl(VD);
                          ASSERT_EQ(Annots.size(), 1u)
                              << "Expected one annotation on 'x'";
                          EXPECT_EQ(Annots[0].Body, " requires x > 0; ");
                        });
  EXPECT_TRUE(OK);
  EXPECT_TRUE(InspectorRan);
}

TEST(ECSLStmtDispatch, LineAnnotationAttachesToVarDecl) {
  bool InspectorRan = false;
  bool OK =
      runWithECSL("void f(void) { //@ loop invariant x >= 0;\nint x = 1; }",
                  [&](ASTContext &Ctx, ECSLAnnotationStore &Store) {
                    InspectorRan = true;
                    const VarDecl *VD = findLocalVar(Ctx, "f", "x");
                    ASSERT_NE(VD, nullptr) << "VarDecl 'x' not found in 'f'";
                    auto Annots = Store.getForDecl(VD);
                    ASSERT_EQ(Annots.size(), 1u)
                        << "Expected one line annotation on 'x'";
                    EXPECT_EQ(Annots[0].Body, " loop invariant x >= 0;");
                  });
  EXPECT_TRUE(OK);
  EXPECT_TRUE(InspectorRan);
}

TEST(ECSLStmtDispatch, MultipleAnnotationsAttachToVarDecl) {
  bool InspectorRan = false;
  bool OK = runWithECSL(
      "void f(void) { /*@ assigns nothing; */\n/*@ ensures x == 1; */\nint x "
      "= 1; }",
      [&](ASTContext &Ctx, ECSLAnnotationStore &Store) {
        InspectorRan = true;
        const VarDecl *VD = findLocalVar(Ctx, "f", "x");
        ASSERT_NE(VD, nullptr);
        auto Annots = Store.getForDecl(VD);
        ASSERT_EQ(Annots.size(), 2u) << "Expected two annotations on local 'x'";
        EXPECT_EQ(Annots[0].Body, " assigns nothing; ");
        EXPECT_EQ(Annots[1].Body, " ensures x == 1; ");
      });
  EXPECT_TRUE(OK);
  EXPECT_TRUE(InspectorRan);
}

TEST(ECSLStmtDispatch, NoCrashWhenNoAnnotations) {
  bool InspectorRan = false;
  bool OK = runWithECSL("void f(void) { int x = 1; int y = 2; }",
                        [&](ASTContext &Ctx, ECSLAnnotationStore &Store) {
                          InspectorRan = true;
                          const VarDecl *VDx = findLocalVar(Ctx, "f", "x");
                          const VarDecl *VDy = findLocalVar(Ctx, "f", "y");
                          ASSERT_NE(VDx, nullptr);
                          ASSERT_NE(VDy, nullptr);
                          EXPECT_TRUE(Store.getForDecl(VDx).empty());
                          EXPECT_TRUE(Store.getForDecl(VDy).empty());
                        });
  EXPECT_TRUE(OK);
  EXPECT_TRUE(InspectorRan);
}

TEST(ECSLStmtDispatch, AnnotationNotAttachedToDeclStmt) {
  // Verify the annotation ends up on the VarDecl, not the DeclStmt.
  bool InspectorRan = false;
  bool OK =
      runWithECSL("void f(void) { /*@ note; */\nint x = 1; }",
                  [&](ASTContext &Ctx, ECSLAnnotationStore &Store) {
                    InspectorRan = true;
                    // Find the DeclStmt containing 'x'.
                    for (Decl *D : Ctx.getTranslationUnitDecl()->decls()) {
                      auto *FD = dyn_cast<FunctionDecl>(D);
                      if (!FD || FD->getNameAsString() != "f" || !FD->hasBody())
                        continue;
                      auto *Body = dyn_cast<CompoundStmt>(FD->getBody());
                      if (!Body)
                        continue;
                      for (Stmt *S : Body->body()) {
                        auto *DS = dyn_cast<DeclStmt>(S);
                        if (!DS)
                          continue;
                        EXPECT_TRUE(Store.getForStmt(DS).empty())
                            << "Annotation should be on VarDecl, not DeclStmt";
                      }
                    }
                    // Verify it IS on the VarDecl.
                    const VarDecl *VD = findLocalVar(Ctx, "f", "x");
                    ASSERT_NE(VD, nullptr);
                    EXPECT_EQ(Store.getForDecl(VD).size(), 1u);
                  });
  EXPECT_TRUE(OK);
  EXPECT_TRUE(InspectorRan);
}
