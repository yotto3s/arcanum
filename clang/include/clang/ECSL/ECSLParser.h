//===- clang/ECSL/ECSLParser.h - ECSL annotation parser stub --------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
///
/// \file
/// Declares parseECSLAnnotation(), the entry point for parsing a single ECSL
/// annotation body into a processed form.
///
/// In this PR the function is a stub that returns the input unchanged.
/// Subsequent PRs will replace it with a real ECSL grammar parser that
/// recognises \c requires, \c ensures, \c assigns, etc.
///
//===----------------------------------------------------------------------===//

#ifndef LLVM_CLANG_ECSL_ECSLPARSER_H
#define LLVM_CLANG_ECSL_ECSLPARSER_H

#include "clang/ECSL/ECSLCommentHandler.h"

namespace clang {
namespace ecsl {

/// Parse one ECSL annotation.
///
/// Currently a pass-through stub: returns \p PA unchanged.  Future versions
/// will tokenise and parse the body into an ECSL AST fragment.
PendingAnnotation parseECSLAnnotation(PendingAnnotation PA);

} // namespace ecsl
} // namespace clang

#endif // LLVM_CLANG_ECSL_ECSLPARSER_H
