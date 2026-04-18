# Arcanum Architecture: C++ / Common Lisp Design

> **Date:** 2026-04-13
> **Status:** Draft
> **Related:** ecsl-cpp-design.md, arcanum-milestones.md

## Overview

Arcanum is a two-language system:

- **C++ (Clang fork)** — parses C/C++ source code and ECSL annotations, emits a serialized AST as S-expressions
- **Common Lisp** — reads the S-expression AST, runs verification (WP computation), emits WhyML for Why3, generates test code

The boundary is an S-expression stream. The C++ side knows about Clang's AST; the CL side knows about verification. Neither needs to know about the other's internals.

```
┌──────────────────────────────────┐
│  C++ (Clang fork — small patch)  │
│                                  │
│  1. Clang parses C/C++ source    │
│  2. ECSL annotations parsed      │
│     inline during Clang parsing  │
│     (full Sema/scope access)     │
│  3. AST emitter walks Clang AST  │
│     + ECSL annotations           │
│  4. Writes S-expressions → stdout│
└──────────┬───────────────────────┘
           │ S-expression stream
           │ (pipe or file)
┌──────────▼───────────────────────┐
│  Common Lisp                     │
│                                  │
│  1. read S-expressions           │
│  2. Deserialize into CL structs  │
│  3. WP engine (predicate         │
│     transformer on AST)          │
│  4. WhyML emitter → Why3         │
│  5. Test generator → Google Test │
│  6. Report results               │
└──────────────────────────────────┘
```

### Why two languages?

**C++ is required** for the Clang integration. Clang's APIs (`Sema`, `Parser`, `Preprocessor`, AST node classes) are C++ only. The ECSL parser calls `Parser::ParseExpression()` directly during Clang's parsing phase for C/C++ expression delegation. This requires a small fork of Clang — a plugin cannot access the parser at the right moment with the correct scope context.

**Common Lisp is natural** for the verification engine. WP computation is symbolic manipulation of logical predicates — substitution, rewriting, structural recursion over terms. This is what Lisp was designed for. Frama-C uses OCaml for the same reasons.

**The S-expression boundary** is clean. CL's `read` function natively parses S-expressions — no custom deserializer library needed. The C++ emitter is a straightforward AST visitor that prints parenthesized text. The two sides are independently testable.

### Why a Clang fork, not a plugin?

The ECSL parser must call `Parser::ParseExpression()` to delegate C/C++ expressions inside annotations to Clang. This requires access to the parser's current state — scope, token stream position, `Sema` context — at the exact point where the annotation appears in the source.

A Clang plugin only sees comments *after* parsing is complete. At that point, the parser state is gone. Workarounds (token injection, re-parsing) are fragile and lose source location accuracy.

A fork adds a small, localized patch:
- **Lexer** — recognize `/*@` as an ECSL annotation token (few lines)
- **Parser** — when encountering an ECSL annotation during parsing, call the ECSL parser with the current `Sema`/scope context (a hook)

These touch stable parts of Clang that rarely change between releases. Rebasing on new Clang versions is mechanical. The maintenance cost is comparable to tracking Clang AST API changes, which we must do regardless.

For a safety-critical verification tool targeting ISO 26262 ASIL-D, requiring a custom Clang build is expected and acceptable.

---

## S-expression Format

### Example

Given this C source:

```c
/*@ requires a > 0;
    ensures \result == a + b; */
int add(int a, int b) {
    return a + b;
}
```

The C++ side emits:

```lisp
(function-decl "add" "add"
  ((param-decl "a" (type "int"))
   (param-decl "b" (type "int")))
  (type "int")
  (compound-stmt
    ((return-stmt
       (binary-op :add
         (var-ref "a" (type "int"))
         (var-ref "b" (type "int"))
         (type "int")))))
  nil
  (ecsl-contract
    ((ecsl-clause :none
       (ecsl-comparison :gt
         (ecsl-c-expr (var-ref "a" (type "int")))
         (ecsl-c-expr (int-literal 0 (type "int"))))))
    ((ecsl-clause :none
       (ecsl-comparison :eq
         (ecsl-c-expr (ecsl-result))
         (ecsl-c-expr (binary-op :add
           (var-ref "a" (type "int"))
           (var-ref "b" (type "int"))
           (type "int"))))))
    ()
    ()
    nil
    nil))
```

### Design rules

1. **Each node is a list.** Car is the tag (symbol), cdr is the fields in schema-defined order.
2. **Leaf nodes with no fields** are bare symbols: `break-stmt`, `ecsl-true`, `ecsl-nothing`.
3. **nil** represents absence (`:maybe` field with no value, empty body, etc.).
4. **Lists of children** are nested lists: `((child1) (child2) (child3))`.
5. **Types** are currently `(type "int")` — a simplified string representation. Will be structured later.
6. **Source locations** are `(loc "file.c" 10 5)` — file, line, column. Omitted in examples for brevity.
7. **Keywords** use CL keyword syntax: `:add`, `:sub`, `:gt`, `:struct`, etc.
8. **Strings** are double-quoted: `"add"`, `"int"`.

---

## AST Schema

A single Lisp file (`arcanum-ast-schema.lisp`) defines every AST node type. From this schema, the `define-ast-schema` macro generates:

1. **CL defstructs** — one per node type
2. **CL deserializer** — `deserialize-node` function: S-expression → struct
3. **C++ emitter** — via `generate-cpp-emitter-file`: writes a `.cpp` that walks Clang's AST and prints S-expressions

### Schema format

```lisp
(:category node-name
  (field-name type [:clang accessor]))
```

Categories: `:stmt`, `:expr`, `:decl`, `:type` (Clang AST nodes), `:ecsl`, `:ecsl-pred`, `:ecsl-term` (ECSL annotations).

### Accessor convention

The `:clang` value specifies how to extract the field from a Clang AST node:

- **Symbol** — auto-converted from kebab-case to camelCase with `()` appended:
  - `get-lhs` → `getLhs()`
  - `get-sub-expr` → `getSubExpr()`
  - `is-arrow` → `isArrow()`
  - `children` → `children()`

- **String** — used verbatim for complex expressions:
  - `"getDecl()->getName()"` → `getDecl()->getName()`
  - `"getOpcodeStr(getOpcode())"` → `getOpcodeStr(getOpcode())`

- **Omitted** — ECSL-only nodes have no C++ counterpart

### Example schema entry

```lisp
(:expr binary-op
  (opcode :keyword :clang "getOpcodeStr(getOpcode())")
  (lhs :expr :clang get-lhs)
  (rhs :expr :clang get-rhs)
  (type :c-type :clang get-type))
```

Generates:

**CL:**
```lisp
(defstruct binary-op opcode lhs rhs type)
```

**C++ (in emitStmt):**
```cpp
case Stmt::BinaryOperatorClass: {
    auto *E = cast<BinaryOperator>(Node);
    OS << "(binary-op ";
    OS << ":" << E->getOpcodeStr(E->getOpcode());
    OS << " "; emitExpr(E->getLHS(), OS);
    OS << " "; emitExpr(E->getRHS(), OS);
    OS << " "; emitType(E->getType(), OS);
    OS << ")";
    break;
}
```

### Adding a new AST node

To support a new Clang AST node:

1. Add one entry to `arcanum-ast-schema.lisp`
2. Run `(generate-cpp-emitter-file)` to regenerate the C++ emitter
3. Rebuild arcanum-emit

The CL defstructs and deserializer are regenerated automatically at compile time. No other changes needed.

---

## C++ Side: Clang Fork

### Components

| Component | File | Role |
|---|---|---|
| Lexer patch | `Lexer.cpp` (Clang fork) | Recognize `/*@` as ECSL annotation token |
| Parser hook | `Parser.cpp` (Clang fork) | When annotation token encountered, call ECSL parser with current scope |
| ECSL parser | `ECSLParser.cpp` | Recursive descent for ECSL grammar, calls `Parser::ParseExpression()` directly for C/C++ expression delegation |
| Annotation store | `ECSLAnnotationStore.cpp` | Stores parsed annotations keyed by `Decl*`/`Stmt*` |
| AST emitter | `ast_emitter_gen.cpp` | **Auto-generated** from schema. Walks Clang AST, prints S-expressions |
| Annotation emitter | `ECSLEmitter.cpp` | Emits parsed ECSL annotations as S-expressions |
| Main driver | `ASTEmitterConsumer.cpp` | `ASTConsumer` that coordinates: walk AST + annotations → emit S-expressions |

The Clang fork patch is minimal — approximately 50-100 lines across `Lexer.cpp` and `Parser.cpp`. All Arcanum-specific logic lives in separate files that don't touch Clang internals.

### ECSL parser architecture

The ECSL parser is invoked *during* Clang's parsing phase, with full access to the current `Sema` and scope:

```
Clang parsing int f(int x) {          ← Parser has scope with 'x' in it
  encounters /*@ requires x > 0; */   ← Lexer recognizes ECSL annotation
  Parser hook calls ECSLParser         ← ECSL parser runs with this scope
    ECSL parser:
      sees "requires" keyword
      delegates "x > 0" to Parser::ParseExpression()
        Clang resolves 'x' as ParmVarDecl  ← correct because scope is active
        returns BinaryOperator(DeclRefExpr(x), IntegerLiteral(0), BO_GT)
      stores: ECSLRequires with Clang Expr*
  Clang continues parsing function body
```

Because the ECSL parser runs inline during Clang's parse, expression delegation is clean — no token injection, no re-parsing, no scope reconstruction. The parser stores results in `ECSLAnnotationStore` (side map from `clang::Decl*`/`clang::Stmt*` → annotation structs). The emitter reads from this store when walking the AST.

For the full ECSL grammar (BNF), token definitions, annotation data structures, error handling, and parser entry points, see `ecsl-parser-design.md`.

### Build

Arcanum's C++ components are built as part of the Clang fork:

```cmake
# In the Clang fork's CMakeLists.txt, add Arcanum sources
add_clang_library(clangArcanum
    ECSLParser.cpp
    ECSLAnnotationStore.cpp
    ECSLEmitter.cpp
    ASTEmitterConsumer.cpp
    ${CMAKE_CURRENT_BINARY_DIR}/ast_emitter_gen.cpp  # auto-generated
    LINK_LIBS
    clangAST clangSema clangParse clangLex
)

# arcanum-emit tool — a standalone Clang tool that emits S-expressions
add_clang_executable(arcanum-emit
    ArcanuEmitMain.cpp
    LINK_LIBS
    clangArcanum clangTooling
)
```

This builds `arcanum-emit` as a Clang tool (like `clang-format` or `clang-tidy`) — a standalone binary that parses C/C++ with ECSL annotations and emits S-expressions.

### Schema-driven code generation

The auto-generated `ast_emitter_gen.cpp` is produced by running CL on the schema:

```cmake
add_custom_command(
    OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/ast_emitter_gen.cpp
    COMMAND sbcl --noinform --non-interactive
            --load ${SCHEMA_DIR}/define-ast-schema.lisp
            --load ${SCHEMA_DIR}/arcanum-ast-schema.lisp
            --eval "(generate-cpp-emitter-file \"${CMAKE_CURRENT_BINARY_DIR}/ast_emitter_gen.cpp\")"
            --eval "(quit)"
    DEPENDS ${SCHEMA_DIR}/define-ast-schema.lisp
            ${SCHEMA_DIR}/arcanum-ast-schema.lisp
    COMMENT "Generating AST emitter from schema"
)

This runs at build time — SBCL must be installed on the build machine. The generated `.cpp` file is committed to the repo as well, so builds without SBCL are possible (just won't regenerate if schema changes).
```

---

## Common Lisp Side: Verification Engine

### Components

| Component | File | Role |
|---|---|---|
| Schema | `define-ast-schema.lisp` | Macro + generators |
| AST definitions | `arcanum-ast-schema.lisp` | Node definitions → defstructs + deserializer |
| Reader | `reader.lisp` | Read S-expressions from stream, build struct tree |
| WP engine | `wp.lisp` | Weakest precondition computation |
| WhyML emitter | `whyml.lisp` | Translate predicates/exprs to Why3 input |
| Test generator | `testgen.lisp` | Boundary-value test generation → Google Test |
| Driver | `main.lisp` | CLI: read stdin, run analysis, write results |

### WP engine sketch

```lisp
(defun wp (stmt postcond)
  "Compute weakest precondition of STMT w.r.t. POSTCOND."
  (etypecase stmt
    (return-stmt
     (subst-term postcond 'result (return-stmt-value stmt)))

    (compound-stmt
     (reduce #'wp (compound-stmt-stmts stmt)
             :from-end t :initial-value postcond))

    (if-stmt
     (let ((c (if-stmt-cond stmt))
           (wp-then (wp (if-stmt-then stmt) postcond))
           (wp-else (if (if-stmt-else-branch stmt)
                        (wp (if-stmt-else-branch stmt) postcond)
                        postcond)))
       (make-ecsl-and
        :lhs (make-ecsl-implies :lhs c :rhs wp-then)
        :rhs (make-ecsl-implies
              :lhs (make-ecsl-not :operand c)
              :rhs wp-else))))

    (expr-stmt
     ;; Assignment: x = e  →  P[x/e]
     (let ((expr (expr-stmt-expr stmt)))
       (when (and (binary-op-p expr)
                  (eq (binary-op-opcode expr) :assign))
         (subst-term postcond
                     (binary-op-lhs expr)
                     (binary-op-rhs expr)))))

    (null-stmt postcond)))
```

### ASDF system definition

```lisp
(defsystem :arcanum
  :description "Arcanum formal verification engine"
  :depends-on (:fiveam :trivia)
  :serial t
  :components
  ((:file "packages")
   (:file "define-ast-schema")
   (:file "arcanum-ast-schema")
   (:file "reader")
   (:file "wp")
   (:file "subst")
   (:file "whyml")
   (:file "testgen")
   (:file "main")))

(defsystem :arcanum/test
  :depends-on (:arcanum :fiveam)
  :components
  ((:file "test/wp-test")
   (:file "test/whyml-test")
   (:file "test/deser-test")))
```

---

## Testing

### C++ side: lit + FileCheck

Test that arcanum-emit produces correct S-expressions:

```c
// test/emitter/basic_func.c
// RUN: arcanum-emit %s | FileCheck %s

// CHECK: (function-decl "add"
// CHECK:   (param-decl "a" (type "int"))
// CHECK:   (param-decl "b" (type "int"))
// CHECK:   (return-stmt
// CHECK:     (binary-op :add
int add(int a, int b) {
    return a + b;
}
```

### CL side: FiveAM

Test the WP engine, deserializer, and WhyML emitter:

```lisp
(def-suite wp-tests)

(in-suite wp-tests)

(test wp-return
  "WP of return a+b with postcondition result == a+b is true."
  (let* ((stmt (make-return-stmt
                :value (make-binary-op :opcode :add
                         :lhs (make-var-ref :name "a")
                         :rhs (make-var-ref :name "b"))))
         (post (make-ecsl-comparison :op :eq
                 :lhs (make-ecsl-c-expr :expr (make-ecsl-result))
                 :rhs (make-ecsl-c-expr
                        :expr (make-binary-op :opcode :add
                                :lhs (make-var-ref :name "a")
                                :rhs (make-var-ref :name "b"))))))
    ;; WP substitutes \result with a+b, yielding a+b == a+b, which is true
    (let ((result (wp stmt post)))
      (is (trivially-true-p result)))))

(test deserialize-round-trip
  "Deserialize a binary-op S-expression."
  (let ((node (deserialize-node
                '(binary-op :add
                   (var-ref "x" (type "int"))
                   (int-literal 1 (type "int"))
                   (type "int")))))
    (is (binary-op-p node))
    (is (eq :add (binary-op-opcode node)))
    (is (var-ref-p (binary-op-lhs node)))
    (is (int-literal-p (binary-op-rhs node)))))
```

### End-to-end test

```bash
#!/bin/bash
# test/e2e/test_pure_func.sh

# 1. arcanum-emit produces S-expressions
arcanum-emit test_input.c > /tmp/ast.sexp

# 2. CL reads S-expressions, runs WP, emits WhyML
sbcl --load arcanum --eval '(arcanum:verify "/tmp/ast.sexp")' > /tmp/result.why

# 3. Why3 checks the proof obligations
why3 prove /tmp/result.why
```

---

## Project Structure

```
arcanum/
├── CMakeLists.txt
│
├── schema/                         # Single source of truth
│   ├── define-ast-schema.lisp      # Macro + generators
│   └── arcanum-ast-schema.lisp     # Node definitions
│
├── clang-patch/                    # Clang fork patches
│   ├── lexer.patch                 # /*@ annotation token recognition
│   └── parser.patch                # ECSL parser hook
│
├── emitter/                        # C++ (built into Clang fork)
│   ├── CMakeLists.txt
│   ├── ArcanuEmitMain.cpp          # arcanum-emit tool entry point
│   ├── ASTEmitterConsumer.cpp      # AST walk + emit orchestration
│   ├── ECSLParser.h / .cpp         # ECSL recursive descent parser
│   ├── ECSLAnnotationStore.h / .cpp # Annotation storage
│   ├── ECSLEmitter.h / .cpp        # Annotation → S-expr emitter
│   └── ASTEmitter.h                # Header for generated emitter
│
├── core/                           # Common Lisp verification engine
│   ├── arcanum.asd                 # ASDF system definition
│   ├── packages.lisp
│   ├── reader.lisp                 # S-expr → struct reader
│   ├── wp.lisp                     # WP computation
│   ├── subst.lisp                  # Term substitution
│   ├── whyml.lisp                  # WhyML emitter
│   ├── testgen.lisp                # Google Test generator
│   └── main.lisp                   # CLI driver
│
├── test/
│   ├── lit.cfg.py                  # lit configuration
│   ├── emitter/                    # C++ tests (lit + FileCheck)
│   │   ├── basic_func.c
│   │   ├── if_else.c
│   │   ├── loop.c
│   │   └── ecsl_contract.c
│   ├── core/                       # CL tests (FiveAM)
│   │   ├── wp-test.lisp
│   │   ├── whyml-test.lisp
│   │   └── deser-test.lisp
│   └── e2e/                        # End-to-end tests
│       ├── test_pure_func.sh
│       └── inputs/
│
└── tools/
    └── arcanum                     # CLI entry point (shell script)
        # arcanum verify input.c
        # arcanum testgen input.c
```

---

## CLI Interface

```bash
# Verify a C/C++ file with ECSL annotations
arcanum verify input.c
# Pipeline: arcanum-emit → S-expr → CL WP engine → WhyML → Why3

# Generate tests from ECSL contracts
arcanum testgen input.c -o test_input.cpp
# Pipeline: arcanum-emit → S-expr → CL test generator → Google Test .cpp

# Emit S-expressions only (for debugging)
arcanum dump input.c
# Pipeline: arcanum-emit → S-expr → stdout

# Verify with specific Why3 prover
arcanum verify input.c --prover alt-ergo
```

The `arcanum` script orchestrates the pipeline:

```bash
#!/bin/bash
EMIT="$(dirname $0)/../bin/arcanum-emit"
CORE_PATH="$(dirname $0)/../lib/arcanum-core.fasl"

case "$1" in
    verify)
        shift
        "$EMIT" "$@" \
            | sbcl --noinform --non-interactive \
                   --load "$CORE_PATH" \
                   --eval "(arcanum:verify-stdin)"
        ;;
    testgen)
        shift
        "$EMIT" "$@" \
            | sbcl --noinform --non-interactive \
                   --load "$CORE_PATH" \
                   --eval "(arcanum:testgen-stdin)"
        ;;
    dump)
        shift
        "$EMIT" "$@"
        ;;
esac
```

---

## Milestone Implementation Map

| Milestone | C++ work | CL work |
|---|---|---|
| **M1: Pure functions** | Clang fork setup, ECSL parser (requires/ensures/assigns), AST emitter for M1 nodes (~30 nodes), schema | WP engine (return, compound, if/else, assignment), WhyML emitter, test generator, Why3 integration |
| **M2: Full C** | ECSL parser (loop invariant/variant, \valid, \old, assert, assigns), AST emitter for M2 nodes (~44 more nodes) | WP for loops (induction), pointer memory model, float support, WhyML extensions |
| **M3: WP equivalent** | ECSL parser (behaviors, quantifiers, logic defs, ghost code, all remaining ACSL) | Full WP engine (quantifiers, logic definitions, ghost variables, modular verification) |
| **M4: Basic C++** | AST emitter for C++ nodes (classes, methods, constructors), ECSL parser (class invariant) | Proof obligation generation (class invariant checking, Liskov substitution) |
| **M5: C++ Refs/Casts** | AST emitter for references, casts, new/delete | Reference-as-pointer modeling in WP |
| **M6: Advanced C++** | AST emitter for lambdas, ECSL parser (throws clause, if exceptions decided) | Lambda capture modeling, exception control flow in WP (if decided) |

---

## References

- **Frama-C** — same two-language pattern: C (CIL parser) + OCaml (WP engine, plugins). Production-grade for 15+ years.
- **ACSL v1.23** — base specification grammar
- **ACSL++ v0.1.1** — C++ extensions we adopt
- **Clang Internals Manual** — https://clang.llvm.org/docs/InternalsManual.html
- **Clang internals** — `Parser::ParseExpression()`, `Preprocessor::EnterTokenStream()` for ECSL expression delegation
- **Why3** — https://why3.lri.fr/ — target prover platform
- **FiveAM** — CL test framework
- **LLVM lit + FileCheck** — C++ test infrastructure
