// Test: ECSL annotations before top-level declarations are buffered and
// dispatched without error or crash.  The annotation contents are not yet
// validated (grammar parsing is a later PR); this test only checks that the
// dispatch machinery is wired correctly.

// RUN: %clang -cc1 -fecsl -fsyntax-only %s
// RUN: %clang -cc1 -fsyntax-only %s

/*@ requires x > 0;
    ensures \result > 0; */
int f(int x);

/*@ ensures \result == 0; */
int g(void);

// Multiple annotations before one decl.
/*@ requires x >= 0; */
/*@ ensures \result >= 0; */
int h(int x);

// Annotation before a typedef — must not crash.
/*@ some annotation; */
typedef int MyInt;

// No annotation before this decl — must not crash (empty drain).
int no_annot(void);

// Plain comment must not be dispatched (no crash, no false positive).
/* plain block comment */
// plain line comment
int plain(void);
