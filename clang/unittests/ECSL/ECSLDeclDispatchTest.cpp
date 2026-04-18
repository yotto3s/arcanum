//===- ECSLDeclDispatchTest.cpp - Unit tests for PR4 Decl dispatch --------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
///
/// \file
/// Tests for ECSL annotation dispatch at Decl boundaries: annotation comments
/// buffered by ECSLCommentHandler are drained after ParseExternalDeclaration
/// and stored in ECSLAnnotationStore keyed by the parsed Decl.
///
//===----------------------------------------------------------------------===//

#include "clang/AST/ASTConsumer.h"
#include "clang/AST/ASTContext.h"
#include "clang/AST/Decl.h"
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
// Test infrastructure
// ---------------------------------------------------------------------------

/// Callback invoked from inside HandleTranslationUnit so the annotation store
/// is still alive (EndSourceFileAction tears it down afterwards).
using StoreInspector =
    std::function<void(ASTContext &, ecsl::ECSLAnnotationStore &)>;

/// Custom consumer that captures the CompilerInstance and calls an inspector
/// in HandleTranslationUnit before the action tears down.
class ECSLDispatchConsumer : public ASTConsumer {
public:
  ECSLDispatchConsumer(CompilerInstance &CI, StoreInspector Inspector)
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

/// SyntaxOnlyAction subclass that injects ECSLDispatchConsumer instead of
/// the default no-op consumer.  The parent's BeginSourceFileAction /
/// EndSourceFileAction handle ECSL setup and teardown unchanged.
class ECSLDispatchAction : public SyntaxOnlyAction {
public:
  explicit ECSLDispatchAction(StoreInspector Inspector)
      : m_inspector(std::move(Inspector)) {}

protected:
  std::unique_ptr<ASTConsumer> CreateASTConsumer(CompilerInstance &CI,
                                                 StringRef) override {
    return std::make_unique<ECSLDispatchConsumer>(CI, m_inspector);
  }

private:
  StoreInspector m_inspector;
};

/// Run \p Code through -fecsl -fsyntax-only and invoke \p Inspector inside
/// HandleTranslationUnit. Returns true if the action succeeded.
static bool runWithECSL(StringRef Code, StoreInspector Inspector) {
  return runToolOnCodeWithArgs(
      std::make_unique<ECSLDispatchAction>(std::move(Inspector)), Code,
      {"-fecsl", "-x", "c"}, "input.c");
}

// ---------------------------------------------------------------------------
// Tests
// ---------------------------------------------------------------------------

TEST(ECSLDeclDispatch, SingleAnnotationAttachesToFunctionDecl) {
  bool InspectorRan = false;
  bool OK =
      runWithECSL("/*@ requires x > 0; */\nint f(int x);",
                  [&](ASTContext &Ctx, ECSLAnnotationStore &Store) {
                    InspectorRan = true;
                    // Find the FunctionDecl for 'f'.
                    const FunctionDecl *FD = nullptr;
                    for (Decl *D : Ctx.getTranslationUnitDecl()->decls()) {
                      if (auto *Fn = dyn_cast<FunctionDecl>(D);
                          Fn && Fn->getNameAsString() == "f") {
                        FD = Fn;
                        break;
                      }
                    }
                    ASSERT_NE(FD, nullptr) << "FunctionDecl 'f' not found";
                    auto Annots = Store.getForDecl(FD);
                    ASSERT_EQ(Annots.size(), 1u)
                        << "Expected one annotation on 'f'";
                    EXPECT_EQ(Annots[0].Body, " requires x > 0; ");
                  });
  EXPECT_TRUE(OK);
  EXPECT_TRUE(InspectorRan);
}

TEST(ECSLDeclDispatch, MultipleAnnotationsAllAttach) {
  bool InspectorRan = false;
  bool OK = runWithECSL(
      "/*@ requires x >= 0; */\n/*@ ensures \\result >= 0; */\nint h(int x);",
      [&](ASTContext &Ctx, ECSLAnnotationStore &Store) {
        InspectorRan = true;
        const FunctionDecl *FD = nullptr;
        for (Decl *D : Ctx.getTranslationUnitDecl()->decls()) {
          if (auto *Fn = dyn_cast<FunctionDecl>(D);
              Fn && Fn->getNameAsString() == "h") {
            FD = Fn;
            break;
          }
        }
        ASSERT_NE(FD, nullptr);
        auto Annots = Store.getForDecl(FD);
        ASSERT_EQ(Annots.size(), 2u) << "Expected two annotations on 'h'";
        EXPECT_EQ(Annots[0].Body, " requires x >= 0; ");
        EXPECT_EQ(Annots[1].Body, " ensures \\result >= 0; ");
      });
  EXPECT_TRUE(OK);
  EXPECT_TRUE(InspectorRan);
}

TEST(ECSLDeclDispatch, PlainCommentProducesEmptyStore) {
  bool InspectorRan = false;
  bool OK =
      runWithECSL("/* plain comment */\nint p(void);",
                  [&](ASTContext &Ctx, ECSLAnnotationStore &Store) {
                    InspectorRan = true;
                    for (Decl *D : Ctx.getTranslationUnitDecl()->decls()) {
                      EXPECT_TRUE(Store.getForDecl(D).empty())
                          << "No annotations expected for plain comment";
                    }
                  });
  EXPECT_TRUE(OK);
  EXPECT_TRUE(InspectorRan);
}

TEST(ECSLDeclDispatch, AnnotationBeforeTypedefAttaches) {
  bool InspectorRan = false;
  bool OK = runWithECSL(
      "/*@ some note; */\ntypedef int MyInt;",
      [&](ASTContext &Ctx, ECSLAnnotationStore &Store) {
        InspectorRan = true;
        // Search by name to avoid picking up implicit builtin typedefs.
        const TypedefDecl *TD = nullptr;
        for (Decl *D : Ctx.getTranslationUnitDecl()->decls()) {
          if (auto *TDCandidate = dyn_cast<TypedefDecl>(D);
              TDCandidate && TDCandidate->getNameAsString() == "MyInt") {
            TD = TDCandidate;
            break;
          }
        }
        ASSERT_NE(TD, nullptr) << "TypedefDecl 'MyInt' not found";
        auto Annots = Store.getForDecl(TD);
        ASSERT_EQ(Annots.size(), 1u) << "Expected one annotation on typedef";
        EXPECT_EQ(Annots[0].Body, " some note; ");
      });
  EXPECT_TRUE(OK);
  EXPECT_TRUE(InspectorRan);
}

TEST(ECSLDeclDispatch, NoCrashWhenNoAnnotations) {
  bool InspectorRan = false;
  bool OK = runWithECSL("int bare(void);\nint also_bare(int x);",
                        [&](ASTContext &Ctx, ECSLAnnotationStore &Store) {
                          InspectorRan = true;
                          for (Decl *D : Ctx.getTranslationUnitDecl()->decls())
                            EXPECT_TRUE(Store.getForDecl(D).empty());
                        });
  EXPECT_TRUE(OK);
  EXPECT_TRUE(InspectorRan);
}

TEST(ECSLDeclDispatch, LineAnnotationAttachesToFunctionDecl) {
  bool InspectorRan = false;
  bool OK =
      runWithECSL("//@ requires x > 0;\nint g(int x);",
                  [&](ASTContext &Ctx, ECSLAnnotationStore &Store) {
                    InspectorRan = true;
                    const FunctionDecl *FD = nullptr;
                    for (Decl *D : Ctx.getTranslationUnitDecl()->decls()) {
                      if (auto *Fn = dyn_cast<FunctionDecl>(D);
                          Fn && Fn->getNameAsString() == "g") {
                        FD = Fn;
                        break;
                      }
                    }
                    ASSERT_NE(FD, nullptr) << "FunctionDecl 'g' not found";
                    auto Annots = Store.getForDecl(FD);
                    ASSERT_EQ(Annots.size(), 1u)
                        << "Expected one line annotation on 'g'";
                    EXPECT_EQ(Annots[0].Body, " requires x > 0;");
                  });
  EXPECT_TRUE(OK);
  EXPECT_TRUE(InspectorRan);
}

TEST(ECSLDeclDispatch, MixedBlockAndLineAnnotationsAllAttach) {
  bool InspectorRan = false;
  bool OK =
      runWithECSL("/*@ requires x >= 0; */\n//@ ensures \\result >= 0;\nint "
                  "m(int x);",
                  [&](ASTContext &Ctx, ECSLAnnotationStore &Store) {
                    InspectorRan = true;
                    const FunctionDecl *FD = nullptr;
                    for (Decl *D : Ctx.getTranslationUnitDecl()->decls()) {
                      if (auto *Fn = dyn_cast<FunctionDecl>(D);
                          Fn && Fn->getNameAsString() == "m") {
                        FD = Fn;
                        break;
                      }
                    }
                    ASSERT_NE(FD, nullptr);
                    auto Annots = Store.getForDecl(FD);
                    ASSERT_EQ(Annots.size(), 2u)
                        << "Expected two annotations on 'm'";
                    EXPECT_EQ(Annots[0].Body, " requires x >= 0; ");
                    EXPECT_EQ(Annots[1].Body, " ensures \\result >= 0;");
                  });
  EXPECT_TRUE(OK);
  EXPECT_TRUE(InspectorRan);
}
