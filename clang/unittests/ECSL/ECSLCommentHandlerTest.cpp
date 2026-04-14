//===- unittests/ECSL/ECSLCommentHandlerTest.cpp - ECSLCommentHandler tests ===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "clang/ECSL/ECSLCommentHandler.h"
#include "clang/Basic/Diagnostic.h"
#include "clang/Frontend/CompilerInstance.h"
#include "clang/Frontend/CompilerInvocation.h"
#include "clang/Frontend/FrontendActions.h"
#include "clang/Frontend/FrontendOptions.h"
#include "clang/Lex/Preprocessor.h"
#include "clang/Lex/PreprocessorOptions.h"
#include "llvm/Support/MemoryBuffer.h"
#include "llvm/Support/VirtualFileSystem.h"
#include "gtest/gtest.h"
#include <memory>
#include <string>
#include <vector>

using namespace clang;
using namespace clang::ecsl;

namespace {

// ---------------------------------------------------------------------------
// Direct unit tests for ECSLCommentHandler
// ---------------------------------------------------------------------------

/// Build a minimal CompilerInstance, register the handler, lex a buffer, and
/// return what was buffered.
///
/// Because we want to test the handler class directly (not through the full
/// driver pipeline), we construct a small Preprocessor and feed it a buffer.
///
/// We use a simple approach: register the handler through a CompilerInstance
/// driven by a custom ASTFrontendAction, then drain the handler's buffer in
/// EndSourceFileAction and expose the captured annotations for assertions.

class HandlerCapturingAction : public ASTFrontendAction {
public:
  std::vector<PendingAnnotation> Captured;
  std::vector<PendingAnnotation> CapturedSecondDrain;

protected:
  std::unique_ptr<ASTConsumer>
  CreateASTConsumer(CompilerInstance &, StringRef) override {
    return std::make_unique<ASTConsumer>();
  }

  bool BeginSourceFileAction(CompilerInstance &CI) override {
    m_handler = std::make_unique<ECSLCommentHandler>();
    m_handler->registerWith(CI.getPreprocessor());
    return ASTFrontendAction::BeginSourceFileAction(CI);
  }

  void EndSourceFileAction() override {
    Captured = m_handler->takePending();
    CapturedSecondDrain = m_handler->takePending();
    m_handler->unregisterFrom(getCompilerInstance().getPreprocessor());
    m_handler.reset();
    ASTFrontendAction::EndSourceFileAction();
  }

private:
  std::unique_ptr<ECSLCommentHandler> m_handler;
};

/// Run \p Source through the given action and populate Captured.
static bool runAction(HandlerCapturingAction &Action, const char *Source) {
  auto Invocation = std::make_shared<CompilerInvocation>();
  Invocation->getPreprocessorOpts().addRemappedFile(
      "input.c",
      llvm::MemoryBuffer::getMemBuffer(Source, "input.c").release());
  Invocation->getFrontendOpts().Inputs.push_back(
      FrontendInputFile("input.c", Language::C));
  Invocation->getFrontendOpts().ProgramAction = frontend::ParseSyntaxOnly;
  Invocation->getTargetOpts().Triple = "x86_64-unknown-linux-gnu";

  CompilerInstance CI(std::move(Invocation));
  CI.setVirtualFileSystem(llvm::vfs::getRealFileSystem());
  CI.createDiagnostics(nullptr, /*ShouldOwnClient=*/false);
  return CI.ExecuteAction(Action);
}

// ---------------------------------------------------------------------------
// Tests
// ---------------------------------------------------------------------------

TEST(ECSLCommentHandler, BlockAnnotationIsCaptured) {
  const char *Source = "/*@ requires x > 0; */ int f(int x) { return x; }\n";
  HandlerCapturingAction Action;
  ASSERT_TRUE(runAction(Action, Source));
  ASSERT_EQ(Action.Captured.size(), 1u);
  EXPECT_NE(Action.Captured[0].Body.find("requires x > 0;"), std::string::npos);
}

TEST(ECSLCommentHandler, LineAnnotationIsCaptured) {
  const char *Source = "//@ assigns \\nothing;\nint g(void) { return 0; }\n";
  HandlerCapturingAction Action;
  ASSERT_TRUE(runAction(Action, Source));
  ASSERT_EQ(Action.Captured.size(), 1u);
  EXPECT_NE(Action.Captured[0].Body.find("assigns"), std::string::npos);
}

TEST(ECSLCommentHandler, PlainCommentIsNotCaptured) {
  const char *Source = "/* normal comment */ int h(void) { return 0; }\n"
                       "// another plain comment\n";
  HandlerCapturingAction Action;
  ASSERT_TRUE(runAction(Action, Source));
  EXPECT_EQ(Action.Captured.size(), 0u);
}

TEST(ECSLCommentHandler, MixedCommentsOnlyCapturesAnnotations) {
  const char *Source = "/* not ecsl */\n"
                       "/*@ ensures \\result == 1; */\n"
                       "// plain\n"
                       "//@ assigns \\nothing;\n"
                       "int j(void) { return 1; }\n";
  HandlerCapturingAction Action;
  ASSERT_TRUE(runAction(Action, Source));
  ASSERT_EQ(Action.Captured.size(), 2u);
  EXPECT_NE(Action.Captured[0].Body.find("ensures"), std::string::npos);
  EXPECT_NE(Action.Captured[1].Body.find("assigns"), std::string::npos);
}

TEST(ECSLCommentHandler, SourceLocationIsRecorded) {
  const char *Source = "/*@ requires x > 0; */ int f(int x) { return x; }\n";
  HandlerCapturingAction Action;
  ASSERT_TRUE(runAction(Action, Source));
  ASSERT_EQ(Action.Captured.size(), 1u);
  EXPECT_TRUE(Action.Captured[0].Loc.isValid());
}

TEST(ECSLCommentHandler, TakePendingClearsBuffer) {
  const char *Source = "/*@ requires x > 0; */ int f(int x) { return x; }\n";
  HandlerCapturingAction Action;
  ASSERT_TRUE(runAction(Action, Source));
  // First drain should have captured the annotation.
  EXPECT_EQ(Action.Captured.size(), 1u);
  // Second drain (immediately after the first) must return empty.
  EXPECT_TRUE(Action.CapturedSecondDrain.empty());
}

} // namespace
