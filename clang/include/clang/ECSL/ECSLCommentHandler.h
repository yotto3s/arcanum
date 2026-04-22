//===- clang/ECSL/ECSLCommentHandler.h - ECSL annotation comment handler --===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
///
/// \file
/// Declares ECSLCommentHandler, a clang::CommentHandler that captures
/// "/*@ ... */" and "//@ ..." annotation comments into a pending buffer.
/// The handler is registered on the Preprocessor only when -fecsl is active
/// and is unregistered (and its buffer drained) at declaration/statement
/// boundaries in later passes.
///
//===----------------------------------------------------------------------===//

#ifndef LLVM_CLANG_ECSL_ECSLCOMMENTHANDLER_H
#define LLVM_CLANG_ECSL_ECSLCOMMENTHANDLER_H

#include "clang/Basic/SourceLocation.h"
#include "clang/Lex/Preprocessor.h"
#include "llvm/ADT/StringRef.h"
#include <string>
#include <vector>

namespace clang {
namespace ecsl {

/// A single captured ECSL annotation body with its source location.
///
/// \c Body contains the raw annotation text with comment delimiters stripped:
///   - For "/*@ foo */": body is " foo "
///   - For "//@ foo":    body is " foo"
struct PendingAnnotation {
  std::string Body;
  SourceLocation Loc;
};

/// Captures ECSL annotation comments for later parsing.
///
/// Subclasses clang::CommentHandler and fires on every comment token the
/// preprocessor sees.  Only "/*@" and "//@" prefixes are retained; all other
/// comments are ignored.  The handler always returns false so it never
/// suppresses the comment token.
///
/// Lifetime: owned by the FrontendAction that creates it.  Call
/// registerWith() during BeginSourceFileAction and unregisterFrom() during
/// EndSourceFileAction.
class ECSLCommentHandler : public clang::CommentHandler {
public:
  ECSLCommentHandler() = default;
  ~ECSLCommentHandler() override = default;

  /// CommentHandler interface.  Filters and buffers ECSL annotation comments.
  bool HandleComment(Preprocessor &PP, SourceRange Range) override;

  /// Register this handler with \p PP.
  void registerWith(Preprocessor &PP);

  /// Unregister this handler from \p PP.
  void unregisterFrom(Preprocessor &PP);

  /// Return all pending annotations and clear the internal buffer.
  std::vector<PendingAnnotation> takePending();

  /// Return a read-only view of the pending annotations.
  llvm::ArrayRef<PendingAnnotation> getPending() const { return m_pending; }

private:
  std::vector<PendingAnnotation> m_pending;
};

} // namespace ecsl
} // namespace clang

#endif // LLVM_CLANG_ECSL_ECSLCOMMENTHANDLER_H
