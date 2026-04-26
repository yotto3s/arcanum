// Test: M1 ECSL annotations are parsed and accepted without error or crash.
// Checks that requires/ensures/assigns \nothing clauses, relational operators,
// logical connectives, negation, \result, and multi-clause contracts all
// survive the full pipeline (-fecsl, -fsyntax-only) without triggering an
// assertion, crash, or unexpected diagnostic.

// RUN: %clang -cc1 -fecsl -fsyntax-only %s
// RUN: %clang -cc1 -fsyntax-only %s

// Basic requires clause.
/*@ requires x > 0; */
int positive(int x);

// Basic ensures clause with \result.
/*@ ensures \result > 0; */
int positive_result(int x);

// assigns \nothing clause.
/*@ assigns \nothing; */
int pure_fn(int x);

// Multi-clause contract.
/*@ requires x > 0;
    ensures \result > 0;
    assigns \nothing; */
int multi(int x);

// Relational operators: == != < <= > >=
/*@ requires x == 0; */
int eq_zero(int x);

/*@ requires x != 0; */
int ne_zero(int x);

/*@ requires x <= 10; */
int le_ten(int x);

/*@ requires x >= 0; */
int ge_zero(int x);

// Logical connectives.
/*@ requires x > 0 && y > 0; */
int both_pos(int x, int y);

/*@ requires x > 0 || y > 0; */
int either_pos(int x, int y);

// Negation.
/*@ requires !flag; */
int negated(int flag);

// Arithmetic in a term.
/*@ ensures \result == x + y; */
int sum(int x, int y);

// Line-comment annotation syntax.
//@ requires n > 0;
int line_annot(int n);

// No annotation — must not crash.
int no_annot(void);
