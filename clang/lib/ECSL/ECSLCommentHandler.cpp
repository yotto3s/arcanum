//===- clang/ECSL/ECSLCommentHandler.cpp - ECSL annotation comment handler ===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "clang/ECSL/ECSLCommentHandler.h"
#include "clang/Basic/SourceManager.h"
#include "clang/Lex/Preprocessor.h"
#include "llvm/ADT/StringRef.h"

using namespace clang;
using namespace clang::ecsl;

/// Extract the raw comment text from the source buffer.
static std::string extractCommentText(Preprocessor &PP, SourceRange Range) {
  SourceManager &SM = PP.getSourceManager();
  const char *Start = SM.getCharacterData(Range.getBegin());
  const char *End = SM.getCharacterData(Range.getEnd());
  if (!Start || !End || End <= Start)
    return {};
  return std::string(Start, End);
}

/// Strip the "/*@" prefix and " */" suffix from a block ECSL annotation.
static std::string stripBlockAnnotation(llvm::StringRef Text) {
  // Drop "/*@" prefix.
  if (Text.starts_with("/*@"))
    Text = Text.drop_front(3);
  // Drop " */" suffix.
  if (Text.ends_with("*/"))
    Text = Text.drop_back(2);
  return Text.str();
}

/// Strip the "//@" prefix from a line ECSL annotation.
static std::string stripLineAnnotation(llvm::StringRef Text) {
  // Drop "//@" prefix.
  if (Text.starts_with("//@"))
    Text = Text.drop_front(3);
  // Strip a trailing newline if present.
  Text = Text.rtrim("\r\n");
  return Text.str();
}

bool ECSLCommentHandler::HandleComment(Preprocessor &PP, SourceRange Range) {
  std::string Raw = extractCommentText(PP, Range);
  llvm::StringRef Text(Raw);

  if (Text.starts_with("/*@")) {
    m_pending.push_back({stripBlockAnnotation(Text), Range.getBegin()});
  } else if (Text.starts_with("//@")) {
    m_pending.push_back({stripLineAnnotation(Text), Range.getBegin()});
  }

  return false; // never suppress the comment token
}

void ECSLCommentHandler::registerWith(Preprocessor &PP) {
  PP.addCommentHandler(this);
}

void ECSLCommentHandler::unregisterFrom(Preprocessor &PP) {
  PP.removeCommentHandler(this);
}

std::vector<PendingAnnotation> ECSLCommentHandler::takePending() {
  std::vector<PendingAnnotation> Result;
  std::swap(Result, m_pending);
  return Result;
}
