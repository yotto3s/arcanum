# ECSL Parser Implementation Design

> **Date:** 2026-04-12
> **Status:** Draft
> **Related:** arcanum-architecture.md, ecsl-cpp-design.md
> **References:** ACSL v1.23 §A, ACSL++ v0.1.1 §2

## Overview

The ECSL parser is a hand-written recursive descent parser that handles specification-level grammar and delegates C/C++ expression fragments to Clang's `Parser::ParseExpression()` with full `Sema` context. The parser produces annotation data structures that attach to Clang AST nodes. Verification passes walk the Clang AST + annotations directly — no intermediate IR. This document provides the implementation-level details needed to build it.

This document is a companion to `arcanum-architecture.md`, which describes the overall two-language (C++ / Common Lisp) architecture, and `ecsl-cpp-design.md`, which describes how each C++ language feature is handled in ECSL. This document focuses specifically on the ECSL parser internals: grammar, tokens, data structures, error handling, and entry points.

---

## 1. Comment Extraction

### Source annotations

ECSL annotations are embedded in C/C++ comments:

```cpp
/*@ requires x > 0;
    ensures \result > 0; */
int f(int x);

//@ assigns \nothing;
```

### Clang integration

Register a `CommentHandler` via Clang's `Preprocessor`:

```cpp
class ECSLCommentHandler : public clang::CommentHandler {
public:
    bool HandleComment(clang::Preprocessor &PP,
                       clang::SourceRange Range) override {
        StringRef Text = getCommentText(PP, Range);
        if (Text.starts_with("/*@") || Text.starts_with("//@")) {
            // Strip comment delimiters and leading @ on each line
            StringRef Body = stripAnnotationDelimiters(Text);
            SourceLocation Loc = Range.getBegin();
            PendingAnnotations.push_back({Body, Loc});
        }
        return false; // don't consume the comment
    }

    std::vector<PendingAnnotation> PendingAnnotations;
};
```

Register during `ASTConsumer` setup:

```cpp
PP.addCommentHandler(&CommentHandler);
```

### Association with AST nodes

After Clang finishes parsing, walk the AST and match annotations to declarations/statements by source location:

```cpp
// For each pending annotation, find the next declaration or statement
// that follows the annotation's source location.
// Function contracts: annotation immediately before FunctionDecl
// Loop annotations: annotation immediately before loop statement
// Statement contracts: annotation immediately before any statement
// Class invariants: annotation inside RecordDecl scope
```

The matching uses `SourceManager::isBeforeInTranslationUnit()` to find the nearest AST node following each annotation.

---

## 2. ECSL Token Definitions

### ECSL-specific tokens

The ECSL lexer produces these token kinds:

```
// Keywords (clause-level)
TOK_REQUIRES        "requires"
TOK_ENSURES         "ensures"
TOK_ASSIGNS         "assigns"
TOK_FREES           "frees"
TOK_ALLOCATES       "allocates"
TOK_BEHAVIOR        "behavior"
TOK_ASSUMES         "assumes"
TOK_COMPLETE        "complete"
TOK_DISJOINT        "disjoint"
TOK_BEHAVIORS       "behaviors"
TOK_CHECK           "check"
TOK_ADMIT           "admit"
TOK_TERMINATES      "terminates"
TOK_DECREASES       "decreases"
TOK_LOOP            "loop"
TOK_INVARIANT       "invariant"
TOK_VARIANT         "variant"
TOK_ASSERT          "assert"
TOK_CLASS           "class"
TOK_THROWS          "throws"

// Keywords (abrupt termination)
TOK_EXITS           "exits"
TOK_BREAKS          "breaks"
TOK_CONTINUES       "continues"
TOK_RETURNS         "returns"

// Logic keywords (start with \)
TOK_RESULT          "\result"
TOK_OLD             "\old"
TOK_AT              "\at"
TOK_NOTHING         "\nothing"
TOK_EVERYTHING      "\everything"
TOK_VALID           "\valid"
TOK_VALID_READ      "\valid_read"
TOK_SEPARATED       "\separated"
TOK_FREEABLE        "\freeable"
TOK_ALLOCABLE       "\allocable"
TOK_INITIALIZED     "\initialized"
TOK_DANGLING        "\dangling"
TOK_BASE_ADDR       "\base_addr"
TOK_OFFSET          "\offset"
TOK_BLOCK_LENGTH    "\block_length"
TOK_FORALL          "\forall"
TOK_EXISTS          "\exists"
TOK_LET             "\let"
TOK_LAMBDA          "\lambda"
TOK_TRUE            "\true"
TOK_FALSE           "\false"
TOK_EXCEPTION       "\exception"
TOK_IN              "\in"
TOK_SUBSET          "\subset"
TOK_EMPTY           "\empty"
TOK_UNION           "\union"
TOK_INTER           "\inter"

// Logic operators
TOK_IMPLIES         "==>"
TOK_EQUIV           "<==>"
TOK_XOR             "^^"
TOK_BIMPLIES        "-->"
TOK_BEQUIV          "<-->"
TOK_RANGE           ".."

// Punctuation
TOK_SEMICOLON       ";"
TOK_COMMA           ","
TOK_COLON           ":"
TOK_LPAREN          "("
TOK_RPAREN          ")"
TOK_LBRACE          "{"
TOK_RBRACE          "}"
TOK_LBRACKET        "["
TOK_RBRACKET        "]"

// Other
TOK_IDENT           // C/C++ identifier (not an ECSL keyword)
TOK_C_EXPR_START    // marks start of delegation to Clang
TOK_EOF
```

### Lexer rules

1. If a token starts with `\`, it's an ECSL logic keyword. Look up in the keyword table. If not found, it's an error ("unknown ECSL keyword").
2. If a token matches `==>`, `<==>`, `^^`, `-->`, `<-->`, or `..`, it's an ECSL operator.
3. If a token is a bare identifier that matches an ECSL keyword (`requires`, `ensures`, etc.), it's that keyword. Note: these are context-sensitive — `requires` is only a keyword at clause position, not inside expressions.
4. Everything else is passed through for Clang delegation.

### Context-sensitive keywords

ECSL keywords like `requires`, `ensures`, `assigns` are not reserved in C/C++. A variable named `requires` is legal. The ECSL lexer resolves this by position:

- At **clause position** (start of a new clause in a contract): `requires` is a keyword.
- Inside an **expression**: `requires` is a C identifier, delegated to Clang.

Clause position is: start of annotation, or after a `;` in the annotation.

---

## 3. ECSL Grammar (BNF)

### Annotation dispatch

```
annotation          ::= function-contract
                      | loop-annotation
                      | statement-contract
                      | assertion
                      | class-invariant-decl
                      | logic-definition
                      | type-invariant-decl
                      | global-invariant-decl
```

Dispatch is determined by the first token and the AST node the annotation is attached to.

### Function contracts

```
function-contract   ::= requires-clause* terminates-clause? decreases-clause?
                        simple-clause* named-behavior* completeness-clause*

requires-clause     ::= clause-modifier? 'requires' pred ';'
terminates-clause   ::= 'terminates' pred ';'
decreases-clause    ::= 'decreases' term ('for' IDENT)? ';'

simple-clause       ::= assigns-clause | ensures-clause
                      | allocation-clause | abrupt-clause

assigns-clause      ::= 'assigns' locations ';'
ensures-clause      ::= clause-modifier? 'ensures' pred ';'

allocation-clause   ::= 'frees' locations ';'
                      | 'allocates' locations ';'

abrupt-clause       ::= 'exits' pred ';'
                      | 'breaks' pred ';'
                      | 'continues' pred ';'
                      | 'returns' pred ';'
                      | 'throws' pred ';'

locations           ::= '\nothing' | location-list
location-list       ::= location (',' location)*
location            ::= tset

named-behavior      ::= 'behavior' IDENT ':' behavior-body
behavior-body       ::= assumes-clause* requires-clause* simple-clause*
assumes-clause      ::= 'assumes' pred ';'

completeness-clause ::= 'complete' 'behaviors' (IDENT (',' IDENT)*)? ';'
                      | 'disjoint' 'behaviors' (IDENT (',' IDENT)*)? ';'

clause-modifier     ::= 'check' | 'admit'
```

### Loop annotations

```
loop-annotation     ::= loop-clause+
loop-clause         ::= loop-behavior? loop-annot

loop-behavior       ::= 'for' IDENT (',' IDENT)* ':'
loop-annot          ::= 'loop' 'invariant' pred ';'
                      | 'loop' 'assigns' locations ';'
                      | 'loop' 'variant' term ';'
                      | 'loop' 'allocates' locations ';'
                      | 'loop' 'frees' locations ';'
```

### Statement contracts

```
statement-contract  ::= requires-clause* simple-clause* named-behavior*
```

### Assertions

```
assertion           ::= clause-modifier? 'assert' pred ';'
```

### Class invariant (ECSL extension)

```
class-invariant-decl ::= 'class' 'invariant' IDENT ':' pred ';'
```

### Type and global invariants

```
type-invariant-decl    ::= 'type' 'invariant' IDENT '(' type-expr IDENT ')' '=' pred ';'
global-invariant-decl  ::= 'global' 'invariant' IDENT ':' pred ';'
```

### Logic definitions

```
logic-definition    ::= logic-function-def
                      | logic-predicate-def
                      | logic-lemma
                      | logic-axiomatic
                      | logic-type-def
                      | logic-inductive-def

logic-function-def  ::= 'logic' type-expr IDENT '(' params? ')' '=' term ';'
logic-predicate-def ::= 'predicate' IDENT '(' params? ')' '=' pred ';'
logic-lemma         ::= 'lemma' IDENT ':' pred ';'
logic-axiomatic     ::= 'axiomatic' IDENT '{' logic-definition* '}'
logic-type-def      ::= 'type' IDENT type-params? '=' type-expr ';'
logic-inductive-def ::= 'inductive' IDENT '(' params? ')' '{' ind-case* '}'
ind-case            ::= 'case' IDENT ':' pred ';'

params              ::= param (',' param)*
param               ::= type-expr IDENT
type-params         ::= '<' IDENT (',' IDENT)* '>'
```

### Predicates and terms

These are where Clang delegation happens. The ECSL parser handles the top-level structure:

```
pred                ::= '\true' | '\false'
                      | term (rel-op term)+             → delegate terms to Clang
                      | IDENT '(' term (',' term)* ')'  → predicate application
                      | '(' pred ')'
                      | pred '&&' pred
                      | pred '||' pred
                      | pred '==>' pred
                      | pred '<==>' pred
                      | '!' pred
                      | pred '^^' pred
                      | term '?' pred ':' pred
                      | pred '?' pred ':' pred
                      | '\let' IDENT '=' term ';' pred
                      | '\let' IDENT '=' pred ';' pred
                      | '\forall' binders ';' pred
                      | '\exists' binders ';' pred
                      | IDENT ':' pred                    → syntactic naming
                      | ecsl-predicate

ecsl-predicate      ::= '\valid' '(' term ')'
                      | '\valid_read' '(' term ')'
                      | '\valid' '(' term '+' '(' term '..' term ')' ')'
                      | '\separated' '(' term (',' term)+ ')'
                      | '\freeable' '(' term ')'
                      | '\allocable' '(' term ')'
                      | '\initialized' '(' term ')'
                      | '\dangling' '(' term ')'

term                ::= C-EXPR                            → delegate to Clang
                      | '\old' '(' term ')'
                      | '\at' '(' term ',' IDENT ')'
                      | '\result'
                      | '\exception'
                      | '\base_addr' '(' term ')'
                      | '\offset' '(' term ')'
                      | '\block_length' '(' term ')'
                      | '\let' IDENT '=' term ';' term
                      | '\lambda' binders ';' term
                      | '{' term '\with' '.' IDENT '=' term '}'
                      | '{' term '\with' '[' term ']' '=' term '}'
                      | IDENT '(' term (',' term)* ')'    → logic function application
                      | ecsl-set-term

ecsl-set-term       ::= '\nothing' | '\everything' | '\empty'
                      | term '..' term                     → range
                      | '\union' '(' term (',' term)+ ')'
                      | '\inter' '(' term (',' term)+ ')'
                      | '{' term '|' binders ';' pred '}'  → set comprehension

binders             ::= binder (',' binder)*
binder              ::= type-expr IDENT (',' IDENT)*

rel-op              ::= '==' | '!=' | '<' | '<=' | '>' | '>='
```

---

## 4. Clang Expression Delegation

### When to delegate

The ECSL parser delegates to Clang at **term** position when the next token is not an ECSL keyword (doesn't start with `\`) and is not an ECSL operator.

### How to delegate

```cpp
/// Parse a C/C++ expression using Clang's parser.
/// Called from the ECSL parser at term positions.
///
/// @param AnnotationTokens  Token buffer from the ECSL annotation
/// @param StartIdx          Index of the first token to delegate
/// @param PP                Clang Preprocessor (for creating token buffers)
/// @param ParserActions     Clang Sema instance
/// @param CurScope          Current Clang scope (function, class, namespace)
/// @return                  Parsed Clang Expr* and number of tokens consumed
clang::ExprResult delegateToCLang(
    ArrayRef<Token> AnnotationTokens,
    unsigned StartIdx,
    clang::Preprocessor &PP,
    clang::Sema &S,
    clang::Scope *CurScope);
```

### Implementation strategy

1. **Collect tokens** from the ECSL annotation text until an ECSL-specific token is encountered (`\valid`, `==>`, `;`, etc.). These are the C/C++ expression tokens.

2. **Inject tokens** into Clang's token stream:
   ```cpp
   // Create a temporary token buffer
   SmallVector<Token, 16> ExprTokens;
   // ... copy tokens from annotation
   // Append an EOF sentinel
   ExprTokens.push_back(makeEofToken());

   // Push onto Clang's preprocessor
   PP.EnterTokenStream(ExprTokens, /*DisableMacroExpansion=*/true);
   ```

3. **Call Clang's parser**:
   ```cpp
   // Create a temporary Parser or reuse the existing one
   // with the current Sema context
   Parser::ExprResult Result = TheParser->ParseExpression();
   ```

4. **Pop the token stream** and resume ECSL parsing.

### Scope context

For expression delegation to work, Clang needs the correct scope:

| Annotation location | DeclContext for delegation |
|---|---|
| Function contract | The FunctionDecl's parameter scope |
| Loop annotation | The enclosing function body scope |
| Class invariant | The RecordDecl scope (access to private members) |
| Statement contract | The enclosing block scope |
| Global logic def | Translation unit scope |

The ECSL parser stores the current `DeclContext` based on which AST node the annotation is attached to. This is passed to `Sema` before delegating.

### Handling `this`

In member function contracts, `this` must be resolvable. Clang's `Sema` already handles `this` when the current `DeclContext` is a `CXXMethodDecl`. No special handling needed — delegation just works.

### Handling template parameters

In template class/function contracts, template parameters like `T` must be resolvable. Since Arcanum only verifies instantiated code, the contracts are parsed in the context of a concrete instantiation where `T` is a known type. Clang's `Sema` resolves it normally.

---

## 5. Parser Output

### Annotation data structures

The parser produces ECSL annotation data structures that attach to Clang AST nodes. These are plain C++ structs — no MLIR, no custom IR. Verification passes walk the Clang AST + annotations directly.

### Pipeline

```
/*@ requires x > 0; ensures \result > 0; */
  │
  ▼
ECSL Parser (recursive descent + Clang expression delegation)
  │  produces ECSLFunctionContract containing Clang Expr* nodes
  ▼
ECSL annotations attached to Clang AST nodes
  │
  ▼
Verification passes (WP, test gen) walk Clang AST + annotations
```

### Data structures

```cpp
/// Clause modifier: check, admit, or none
enum class ClauseModifier { None, Check, Admit };

/// A predicate in ECSL — tree of logic connectives with Clang Expr* at leaves
struct ECSLPred {
    enum Kind {
        True, False,
        CExpr,            // leaf: Clang Expr* (delegated C/C++ expression)
        And, Or, Implies, Equiv, Not, Xor,
        Forall, Exists,   // quantifiers: binders + body pred
        Let,              // local binding
        Valid, ValidRead, Separated, Freeable, Allocable, // built-in predicates
        Initialized, Dangling,
        PredicateApp,     // user-defined predicate call
        Named,            // syntactic naming: id : pred
    } kind;

    // For CExpr leaf:
    clang::Expr *cExpr = nullptr;

    // For connectives:
    std::unique_ptr<ECSLPred> left, right;

    // For quantifiers:
    std::vector<ECSLBinder> binders;
    std::unique_ptr<ECSLPred> body;

    // For built-in predicates:
    std::vector<ECSLTerm> args;

    SourceRange loc;
};

/// A term in ECSL — wraps Clang Expr* with ECSL extensions
struct ECSLTerm {
    enum Kind {
        CExpr,            // delegated C/C++ expression
        Old,              // \old(term)
        At,               // \at(term, label)
        Result,           // \result
        Exception,        // \exception
        Range,            // t1 .. t2
        Nothing,          // \nothing
        Everything,       // \everything
        BaseAddr, Offset, BlockLength,  // memory terms
        LogicFuncApp,     // user-defined logic function
        Lambda,           // \lambda
        FunctionalUpdate, // { s \with .field = v }
    } kind;

    clang::Expr *cExpr = nullptr;  // for CExpr kind
    std::unique_ptr<ECSLTerm> inner;  // for Old, At, etc.
    std::string label;  // for At
    SourceRange loc;
};

/// Function contract
struct ECSLFunctionContract {
    struct Requires { ClauseModifier mod; std::unique_ptr<ECSLPred> pred; SourceRange loc; };
    struct Ensures  { ClauseModifier mod; std::unique_ptr<ECSLPred> pred; SourceRange loc; };
    struct Assigns  { std::vector<ECSLTerm> locations; SourceRange loc; };
    struct Behavior {
        std::string name;
        std::unique_ptr<ECSLPred> assumes;
        std::vector<Requires> requires;
        std::vector<Ensures> ensures;
        std::vector<Assigns> assigns;
    };

    std::vector<Requires> requires;
    std::vector<Ensures> ensures;
    std::vector<Assigns> assigns;
    std::unique_ptr<ECSLPred> terminates;  // optional
    std::unique_ptr<ECSLTerm> decreases;   // optional
    std::vector<Behavior> behaviors;
    SourceRange loc;
};

/// Loop annotation
struct ECSLLoopAnnotation {
    std::vector<std::unique_ptr<ECSLPred>> invariants;
    std::vector<ECSLTerm> assigns;  // loop assigns locations
    std::unique_ptr<ECSLTerm> variant;  // optional
    SourceRange loc;
};

/// Class invariant
struct ECSLClassInvariant {
    std::string name;
    std::unique_ptr<ECSLPred> pred;
    SourceRange loc;
};

/// Assertion
struct ECSLAssertion {
    ClauseModifier mod;
    std::unique_ptr<ECSLPred> pred;
    SourceRange loc;
};
```

### Attachment to Clang AST

Annotations are stored in a side map keyed by Clang AST node pointer:

```cpp
class ECSLAnnotationStore {
    llvm::DenseMap<const clang::FunctionDecl*, ECSLFunctionContract> FuncContracts;
    llvm::DenseMap<const clang::Stmt*, ECSLLoopAnnotation> LoopAnnotations;
    llvm::DenseMap<const clang::CXXRecordDecl*, std::vector<ECSLClassInvariant>> ClassInvariants;
    llvm::DenseMap<const clang::Stmt*, ECSLAssertion> Assertions;
public:
    // Lookup
    const ECSLFunctionContract* getContract(const FunctionDecl *FD) const;
    const ECSLLoopAnnotation* getLoopAnnotation(const Stmt *Loop) const;
    // ... etc
};
```

Verification passes receive the Clang AST + `ECSLAnnotationStore` and walk both together.

### Well-formedness checks

Validation is done by the parser during construction and by a post-parse validation pass:

| Rule | When checked |
|---|---|
| `\result` only in `ensures`, not in `requires` | During parsing (parser tracks current clause kind) |
| `\old(e)` only in `ensures`/`assigns`/`frees` | During parsing |
| `assigns` target must be an lvalue | Post-parse validation pass |
| Behavior names in `complete behaviors` must exist | Post-parse validation pass |
| `class invariant` must be inside a class | During annotation-to-AST association |
| `throws` only valid if exception support enabled | During parsing (compile flag check) |

---

## 6. Error Handling

### ECSL syntax errors

If the ECSL parser encounters an unexpected token:

```
/*@ requires x > 0
    ensurse \result > 0; */  // typo: "ensurse"
```

The parser reports:
```
error: expected 'ensures', 'assigns', or ';' in function contract
  --> file.cpp:1:24
   | ensurse \result > 0;
   | ^~~~~~~
```

Recovery: skip to the next `;` or `*/` and continue parsing.

### Clang expression errors

If Clang rejects a delegated expression:

```
/*@ requires thiss->x > 0; */  // typo: "thiss"
```

Clang reports:
```
error: use of undeclared identifier 'thiss'
  --> file.cpp:1:15
   | requires thiss->x > 0;
   |          ^~~~~
```

Recovery: the ECSL parser gets a null `ExprResult`. It records the error, replaces the term with an error node, and continues parsing the rest of the contract.

### Unknown ECSL keywords

```
/*@ requires \vvalid(p); */  // typo: "\vvalid"
```

The ECSL lexer reports:
```
error: unknown ECSL keyword '\vvalid'; did you mean '\valid'?
  --> file.cpp:1:15
   | requires \vvalid(p);
   |          ^~~~~~~
```

### Source location mapping

All ECSL AST nodes store their source location relative to the original `.cpp` file, not the annotation text. This is done by computing offsets:

```
annotation_start_loc + offset_within_annotation = original_source_loc
```

Diagnostics point to the exact position in the original source file, so the user sees the error in context.

---

## 7. Parser Entry Points

### Public API

The parser returns annotation data structures that are stored in the `ECSLAnnotationStore`.

```cpp
class ECSLParser {
public:
    /// Parse a function contract annotation.
    std::optional<ECSLFunctionContract>
    parseFunctionContract(StringRef Text, SourceLocation Loc,
                          const FunctionDecl *FD,
                          Sema &S, Preprocessor &PP);

    /// Parse a loop annotation.
    std::optional<ECSLLoopAnnotation>
    parseLoopAnnotation(StringRef Text, SourceLocation Loc,
                        const Stmt *LoopStmt,
                        Sema &S, Preprocessor &PP);

    /// Parse a class invariant declaration.
    std::optional<ECSLClassInvariant>
    parseClassInvariant(StringRef Text, SourceLocation Loc,
                        const CXXRecordDecl *RD,
                        Sema &S, Preprocessor &PP);

    /// Parse a standalone assertion.
    std::optional<ECSLAssertion>
    parseAssertion(StringRef Text, SourceLocation Loc,
                   const Stmt *EnclosingStmt,
                   Sema &S, Preprocessor &PP);

    /// Parse a logic definition.
    std::optional<ECSLLogicDef>
    parseLogicDefinition(StringRef Text, SourceLocation Loc,
                         Sema &S, Preprocessor &PP);
};
```

### Annotation dispatch logic

```cpp
void Arcanum::processAnnotation(const PendingAnnotation &Ann,
                                     const Decl *NextDecl,
                                     const Stmt *NextStmt) {
    StringRef Text = Ann.Body;
    SourceLocation Loc = Ann.Loc;

    if (auto *FD = dyn_cast_or_null<FunctionDecl>(NextDecl)) {
        if (auto C = Parser.parseFunctionContract(Text, Loc, FD, S, PP))
            Store.addContract(FD, std::move(*C));
    }
    else if (auto *RD = dyn_cast_or_null<CXXRecordDecl>(NextDecl)) {
        if (Text.starts_with("class invariant")) {
            if (auto Inv = Parser.parseClassInvariant(Text, Loc, RD, S, PP))
                Store.addClassInvariant(RD, std::move(*Inv));
        }
    }
    else if (isLoopStmt(NextStmt)) {
        if (auto LA = Parser.parseLoopAnnotation(Text, Loc, NextStmt, S, PP))
            Store.addLoopAnnotation(NextStmt, std::move(*LA));
    }
    else if (Text.starts_with("assert")) {
        if (auto A = Parser.parseAssertion(Text, Loc, NextStmt, S, PP))
            Store.addAssertion(NextStmt, std::move(*A));
    }
    else if (isLogicDefinition(Text)) {
        if (auto D = Parser.parseLogicDefinition(Text, Loc, S, PP))
            Store.addLogicDef(std::move(*D));
    }
}
```

---

## 8. Milestone Scope

### M1: Pure functions

Parser supports:
- `requires`, `ensures`, `assigns \nothing`
- `\result`
- Predicate expressions: `==`, `!=`, `<`, `<=`, `>`, `>=`, `&&`, `||`, `!`
- Arithmetic terms: `+`, `-`, `*`, `/`, `%` (delegated to Clang)
- Logic types: `integer`, `boolean`
- Integer/character literals, variable references (delegated to Clang)

### M2: Full C

Adds:
- `assigns <locations>`, `frees`, `allocates`, `\old(e)`
- `loop invariant`, `loop assigns`, `loop variant`, `decreases`
- `\valid`, `\valid_read`, `\separated`, `\freeable`, `\allocable`
- `\everything`, `t1 .. t2` (range)
- `assert`
- `\forall`, `\exists` (quantifier parsing + binders)
- `real` type, float-related predicates

### M3: WP equivalent

Adds:
- `behavior`, `assumes`, `complete behaviors`, `disjoint behaviors`
- `check`/`admit` modifiers
- `terminates`
- `exits`, `breaks`, `continues`, `returns`
- `\at(e, L)`, program point labels
- `==>`, `<==>`, `^^`
- Logic definitions: `logic`, `predicate`, `lemma`, `axiomatic`, `inductive`
- Higher-order: `\lambda`, `\sum`, `\product`, `\max`, `\min`, `\numof`, `\let`
- Set operations: `\union`, `\inter`, `\empty`, `\subset`
- List operations: `\Nil`, `\Cons`, `\nth`, `\length`, `\repeat`, `\concat`
- Functional update: `{ s \with .field = v }`
- Bitwise in logic: `&`, `|`, `^`, `~`, `<<`, `>>`, `-->`, `<-->`
- Ghost variables/statements
- `type invariant`, `global invariant`, `model` fields

### M4: Basic C++

Adds:
- `class invariant` parsing (new ECSL construct)
- `this` in contracts (delegated to Clang — just works)
- Namespace-qualified identifiers in contracts
- Contract on constructors, destructors, operators (same grammar, different AST context)

### M5: C++ References + Casts

No new parser features. References and casts are C++ expressions delegated to Clang.

### M6: Advanced C++

Adds (if exceptions are supported):
- `throws` clause in function/statement contracts
- `\exception` keyword in terms
- Lambda contract parsing (contract between `)` and `{`)

---

## 9. References

### Language design

- **ACSL v1.23** — the base specification language grammar. ECSL is a superset. All ACSL grammar productions are adopted directly. https://frama-c.com/download/acsl.pdf
- **ACSL++ v0.1.1** — C++ extensions to ACSL. Studied extensively; well-designed parts adopted (class contracts, `this`, Liskov inheritance, operator contracts, lambda contract placement, `throws`/`\exception`). Most features marked Experimental. https://github.com/acsl-language/acsl
- **P2900R6 (C++ Contracts)** — C++26 contracts proposal. Different goals (runtime checking, not formal verification) but informs `\result` naming and postcondition syntax. https://isocpp.org/files/papers/P2900R6.pdf

### Clang expression delegation mechanism

The core of our parser — delegating C/C++ expression fragments to Clang's `Parser::ParseExpression()` — has no direct external precedent as a plugin. The technique is based on Clang's own internal patterns:

- **Late-parsed member initializers** — Clang delays parsing of default member initializers until the class is complete, then reparses with `EnterTokenStream`. See `clang/lib/Parse/ParseCXXInlineMethods.cpp`, `Parser::ParseLexedMemberInitializer()`.
- **Late-parsed default arguments** — same pattern for function default arguments. See `Parser::ParseLexedMethodDeclaration()`.
- **Delayed template parsing** (`-fdelayed-template-parsing`)  — Clang stores token streams for template bodies and reparses when instantiated. See `Parser::ParseLateTemplatedFuncDef()`.
- **`Preprocessor::EnterTokenStream()`** — the API for injecting tokens into the preprocessor's token stream. Used by all of the above mechanisms.

These are the authoritative references for token injection and parser reentry in Clang.

### Clang plugin infrastructure

- **Clang Plugins documentation** — `PluginASTAction`, `ASTConsumer`, `CommentHandler` registration. https://clang.llvm.org/docs/ClangPlugins.html
- **clang-tutor** — out-of-tree plugin examples with CMake build setup and tests. Good template for project structure. https://github.com/banach-space/clang-tutor
- **Clang `AnnotateFunctions` example** — canonical example of attaching metadata to AST nodes via a plugin. In-tree at `clang/examples/AnnotateFunctions/`.
- **Clang CFE Internals Manual** — describes Parser/Sema interaction, `ExprResult`/`StmtResult` opaque types, Type system. https://clang.llvm.org/docs/InternalsManual.html

### Comment interception

- **Frama-Clang** — Clang plugin that intercepts `/*@` comments for ACSL++ annotation parsing. Demonstrates the comment detection and AST association pattern. Frama-Clang's overall approach (C++ → C lowering) differs from ours, but comment interception is the same problem. https://frama-c.com/fc-plugins/frama-clang.html, source at https://git.frama-c.com/pub/frama-clang
- **Clang `RawComment` / `RawCommentList`** — Clang's built-in comment storage, used by documentation comment parsing (`-Wdocumentation`). May be usable for initial comment extraction. See `clang/include/clang/AST/RawCommentList.h`.
