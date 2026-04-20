//===- ECSLStoreTypedTest.cpp - Integration tests for typed contract store
//-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
///
/// \file
/// Integration tests for PR 4/4: verifies that the ECSL dispatch pipeline
/// produces typed ECSLFunctionContract entries in ECSLAnnotationStore for
/// annotated FunctionDecls, and that non-function decls and plain comments do
/// not produce typed contracts.
///
/// Test infrastructure mirrors ECSLDeclDispatchTest (SyntaxOnlyAction subclass
/// + custom ASTConsumer that fires an inspector inside HandleTranslationUnit).
///
//===----------------------------------------------------------------------===//

#include "clang/AST/ASTConsumer.h"
#include "clang/AST/ASTContext.h"
#include "clang/AST/Decl.h"
#include "clang/ECSL/ECSLAnnotationStore.h"
#include "clang/ECSL/ECSLAst.h"
#include "clang/Frontend/CompilerInstance.h"
#include "clang/Frontend/FrontendActions.h"
#include "clang/Lex/Preprocessor.h"
#include "clang/Tooling/Tooling.h"
#include "gtest/gtest.h"
#include <functional>
#include <string>

using namespace clang;
using namespace clang::ecsl;
using namespace clang::tooling;

// ---------------------------------------------------------------------------
// Test infrastructure (same pattern as ECSLDeclDispatchTest)
// ---------------------------------------------------------------------------

using StoreInspector =
    std::function<void(ASTContext &, ecsl::ECSLAnnotationStore &)>;

class ECSLTypedConsumer : public ASTConsumer {
public:
  ECSLTypedConsumer(CompilerInstance &CI, StoreInspector Inspector)
      : m_ci(CI), m_inspector(std::move(Inspector)) {}

  void HandleTranslationUnit(ASTContext &Ctx) override {
    ecsl::ECSLAnnotationStore *Store =
        m_ci.getPreprocessor().getECSLAnnotationStore();
    ASSERT_NE(Store, nullptr);
    m_inspector(Ctx, *Store);
  }

private:
  CompilerInstance &m_ci;
  StoreInspector m_inspector;
};

class ECSLTypedAction : public SyntaxOnlyAction {
public:
  explicit ECSLTypedAction(StoreInspector Inspector)
      : m_inspector(std::move(Inspector)) {}

protected:
  std::unique_ptr<ASTConsumer> CreateASTConsumer(CompilerInstance &CI,
                                                 StringRef) override {
    return std::make_unique<ECSLTypedConsumer>(CI, m_inspector);
  }

private:
  StoreInspector m_inspector;
};

static bool runWithECSL(StringRef Code, StoreInspector Inspector) {
  return runToolOnCodeWithArgs(
      std::make_unique<ECSLTypedAction>(std::move(Inspector)), Code,
      {"-fecsl", "-x", "c"}, "input.c");
}

static const FunctionDecl *findFunc(ASTContext &Ctx, StringRef Name) {
  for (Decl *D : Ctx.getTranslationUnitDecl()->decls())
    if (auto *FD = dyn_cast<FunctionDecl>(D); FD && FD->getName() == Name)
      return FD;
  return nullptr;
}

// ---------------------------------------------------------------------------
// Tests
// ---------------------------------------------------------------------------

TEST(ECSLStoreTyped, RequiresClauseProducesTypedContract) {
  bool ran = false;
  bool ok = runWithECSL("/*@ requires x > 0; */\nint f(int x);",
                        [&](ASTContext &Ctx, ECSLAnnotationStore &Store) {
                          ran = true;
                          const FunctionDecl *FD = findFunc(Ctx, "f");
                          ASSERT_NE(FD, nullptr);
                          const ECSLFunctionContract *C = Store.getContract(FD);
                          ASSERT_NE(C, nullptr)
                              << "Expected typed contract for 'f'";
                          EXPECT_EQ(C->m_requires.size(), 1u);
                          EXPECT_TRUE(C->m_ensures.empty());
                          EXPECT_FALSE(C->m_assigns_nothing.has_value());
                        });
  EXPECT_TRUE(ok);
  EXPECT_TRUE(ran);
}

TEST(ECSLStoreTyped, EnsuresResultProducesTypedContract) {
  bool ran = false;
  bool ok = runWithECSL("/*@ ensures \\result > 0; */\nint g(int x);",
                        [&](ASTContext &Ctx, ECSLAnnotationStore &Store) {
                          ran = true;
                          const FunctionDecl *FD = findFunc(Ctx, "g");
                          ASSERT_NE(FD, nullptr);
                          const ECSLFunctionContract *C = Store.getContract(FD);
                          ASSERT_NE(C, nullptr)
                              << "Expected typed contract for 'g'";
                          EXPECT_TRUE(C->m_requires.empty());
                          EXPECT_EQ(C->m_ensures.size(), 1u);
                        });
  EXPECT_TRUE(ok);
  EXPECT_TRUE(ran);
}

TEST(ECSLStoreTyped, AssignsNothingProducesTypedContract) {
  bool ran = false;
  bool ok = runWithECSL("/*@ assigns \\nothing; */\nint h(void);",
                        [&](ASTContext &Ctx, ECSLAnnotationStore &Store) {
                          ran = true;
                          const FunctionDecl *FD = findFunc(Ctx, "h");
                          ASSERT_NE(FD, nullptr);
                          const ECSLFunctionContract *C = Store.getContract(FD);
                          ASSERT_NE(C, nullptr);
                          EXPECT_TRUE(C->m_assigns_nothing.has_value());
                        });
  EXPECT_TRUE(ok);
  EXPECT_TRUE(ran);
}

TEST(ECSLStoreTyped, MultiClauseContractMergesFromMultipleAnnotations) {
  bool ran = false;
  bool ok = runWithECSL(
      "/*@ requires x > 0; */\n/*@ ensures \\result > 0; */\nint multi(int x);",
      [&](ASTContext &Ctx, ECSLAnnotationStore &Store) {
        ran = true;
        const FunctionDecl *FD = findFunc(Ctx, "multi");
        ASSERT_NE(FD, nullptr);
        const ECSLFunctionContract *C = Store.getContract(FD);
        ASSERT_NE(C, nullptr);
        EXPECT_EQ(C->m_requires.size(), 1u);
        EXPECT_EQ(C->m_ensures.size(), 1u);
      });
  EXPECT_TRUE(ok);
  EXPECT_TRUE(ran);
}

TEST(ECSLStoreTyped, FullContractInOneAnnotation) {
  bool ran = false;
  bool ok = runWithECSL("/*@ requires x > 0;\n    ensures \\result > 0;\n"
                        "    assigns \\nothing; */\nint full(int x);",
                        [&](ASTContext &Ctx, ECSLAnnotationStore &Store) {
                          ran = true;
                          const FunctionDecl *FD = findFunc(Ctx, "full");
                          ASSERT_NE(FD, nullptr);
                          const ECSLFunctionContract *C = Store.getContract(FD);
                          ASSERT_NE(C, nullptr);
                          EXPECT_EQ(C->m_requires.size(), 1u);
                          EXPECT_EQ(C->m_ensures.size(), 1u);
                          EXPECT_TRUE(C->m_assigns_nothing.has_value());
                        });
  EXPECT_TRUE(ok);
  EXPECT_TRUE(ran);
}

TEST(ECSLStoreTyped, PlainCommentProducesNoTypedContract) {
  bool ran = false;
  bool ok = runWithECSL("/* plain comment */\nint plain(void);",
                        [&](ASTContext &Ctx, ECSLAnnotationStore &Store) {
                          ran = true;
                          const FunctionDecl *FD = findFunc(Ctx, "plain");
                          ASSERT_NE(FD, nullptr);
                          EXPECT_EQ(Store.getContract(FD), nullptr);
                        });
  EXPECT_TRUE(ok);
  EXPECT_TRUE(ran);
}

TEST(ECSLStoreTyped, TypedefHasNoTypedContract) {
  bool ran = false;
  bool ok =
      runWithECSL("/*@ some note; */\ntypedef int MyInt;",
                  [&](ASTContext &Ctx, ECSLAnnotationStore &Store) {
                    ran = true;
                    // Typedef should have raw annotation but no typed contract.
                    for (Decl *D : Ctx.getTranslationUnitDecl()->decls()) {
                      if (auto *FD = dyn_cast<FunctionDecl>(D))
                        EXPECT_EQ(Store.getContract(FD), nullptr);
                    }
                  });
  EXPECT_TRUE(ok);
  EXPECT_TRUE(ran);
}

TEST(ECSLStoreTyped, RawAnnotationStillPopulatedForFunctionDecl) {
  bool ran = false;
  bool ok = runWithECSL("/*@ requires n > 0; */\nint p(int n);",
                        [&](ASTContext &Ctx, ECSLAnnotationStore &Store) {
                          ran = true;
                          const FunctionDecl *FD = findFunc(Ctx, "p");
                          ASSERT_NE(FD, nullptr);
                          // Raw store entry must still be present alongside the
                          // typed contract.
                          auto Raw = Store.getForDecl(FD);
                          EXPECT_EQ(Raw.size(), 1u)
                              << "Raw annotation should still be in store";
                          // Typed contract must also be present.
                          EXPECT_NE(Store.getContract(FD), nullptr);
                        });
  EXPECT_TRUE(ok);
  EXPECT_TRUE(ran);
}
