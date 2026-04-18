// Test: ECSL annotations before block-level declarations inside a function
// body are buffered and dispatched without error or crash.  The annotation
// contents are not yet validated; this test only checks that the dispatch
// machinery is wired correctly.

// RUN: %clang -cc1 -fecsl -fsyntax-only %s
// RUN: %clang -cc1 -fsyntax-only %s

void f(void) {
  /*@ requires x > 0; */
  int x = 1;

  //@ loop invariant x >= 0;
  int y = x + 1;

  // Multiple annotations before one local decl.
  /*@ assigns nothing; */
  /*@ ensures x == 1; */
  int z = x;

  // No annotation — must not crash.
  int bare = 42;

  (void)y;
  (void)z;
  (void)bare;
}
