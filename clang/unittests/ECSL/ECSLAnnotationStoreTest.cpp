//===- ECSLAnnotationStoreTest.cpp - Unit tests for ECSLAnnotationStore ---===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "clang/ECSL/ECSLAnnotationStore.h"
#include "clang/AST/Decl.h"
#include "clang/AST/Stmt.h"
#include "gtest/gtest.h"
#include <cstdint>
#include <string>
#include <vector>

using namespace clang;
using namespace clang::ecsl;

// ---------------------------------------------------------------------------
// Helpers: manufacture fake opaque keys without instantiating a
// CompilerInstance. DenseMap uses pointer identity only; no Decl/Stmt methods
// are called.
// ---------------------------------------------------------------------------

static const Decl *fakeDecl(uintptr_t Id) {
  return reinterpret_cast<const Decl *>(Id);
}

static const Stmt *fakeStmt(uintptr_t Id) {
  return reinterpret_cast<const Stmt *>(Id);
}

static PendingAnnotation makeAnnotation(std::string Body) {
  return PendingAnnotation{std::move(Body), SourceLocation()};
}

// ---------------------------------------------------------------------------
// Tests
// ---------------------------------------------------------------------------

TEST(ECSLAnnotationStore, EmptyStoreReturnsEmptyArrayForDecl) {
  ECSLAnnotationStore Store;
  EXPECT_TRUE(Store.getForDecl(fakeDecl(1)).empty());
}

TEST(ECSLAnnotationStore, EmptyStoreReturnsEmptyArrayForStmt) {
  ECSLAnnotationStore Store;
  EXPECT_TRUE(Store.getForStmt(fakeStmt(1)).empty());
}

TEST(ECSLAnnotationStore, AddForDeclRoundTrips) {
  ECSLAnnotationStore Store;
  const Decl *Key = fakeDecl(0x10);

  std::vector<PendingAnnotation> Annots;
  Annots.push_back(makeAnnotation("requires x > 0;"));
  Store.addForDecl(Key, std::move(Annots));

  auto Result = Store.getForDecl(Key);
  ASSERT_EQ(Result.size(), 1u);
  EXPECT_EQ(Result[0].Body, "requires x > 0;");
}

TEST(ECSLAnnotationStore, AddForStmtRoundTrips) {
  ECSLAnnotationStore Store;
  const Stmt *Key = fakeStmt(0x20);

  std::vector<PendingAnnotation> Annots;
  Annots.push_back(makeAnnotation("loop invariant i >= 0;"));
  Store.addForStmt(Key, std::move(Annots));

  auto Result = Store.getForStmt(Key);
  ASSERT_EQ(Result.size(), 1u);
  EXPECT_EQ(Result[0].Body, "loop invariant i >= 0;");
}

TEST(ECSLAnnotationStore, MultipleAddForDeclAccumulates) {
  ECSLAnnotationStore Store;
  const Decl *Key = fakeDecl(0x30);

  std::vector<PendingAnnotation> First;
  First.push_back(makeAnnotation("requires a > 0;"));
  Store.addForDecl(Key, std::move(First));

  std::vector<PendingAnnotation> Second;
  Second.push_back(makeAnnotation("ensures \\result >= 0;"));
  Store.addForDecl(Key, std::move(Second));

  auto Result = Store.getForDecl(Key);
  ASSERT_EQ(Result.size(), 2u);
  EXPECT_EQ(Result[0].Body, "requires a > 0;");
  EXPECT_EQ(Result[1].Body, "ensures \\result >= 0;");
}

TEST(ECSLAnnotationStore, DifferentKeysDontInterfere) {
  ECSLAnnotationStore Store;
  const Decl *KeyA = fakeDecl(0x40);
  const Decl *KeyB = fakeDecl(0x50);

  std::vector<PendingAnnotation> AAnnots;
  AAnnots.push_back(makeAnnotation("requires a;"));
  Store.addForDecl(KeyA, std::move(AAnnots));

  EXPECT_TRUE(Store.getForDecl(KeyB).empty());
  ASSERT_EQ(Store.getForDecl(KeyA).size(), 1u);
  EXPECT_EQ(Store.getForDecl(KeyA)[0].Body, "requires a;");
}
