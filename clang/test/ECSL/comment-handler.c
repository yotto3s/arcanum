// Smoke test: -fecsl is accepted and no crash or spurious diagnostic occurs
// when a source file contains both /*@ ... */ and //@ annotations alongside
// ordinary comments.

// RUN: %clang -cc1 -fecsl -fsyntax-only %s
// RUN: %clang -cc1 -fsyntax-only %s

/*@ requires x > 0;
    ensures \result > 0; */
int f(int x);

//@ assigns \nothing;
int g(void);

/* ordinary block comment — must not be captured */
// ordinary line comment — must not be captured

int h(int x) { return x + 1; }
