# Arcanum — Copilot Instructions

This is a fork of the LLVM project at tag `llvmorg-22.1.3`. Arcanum development lives on the `arcanum` branch and adds the **ECSL (Extended C Specification Language)** annotation pipeline to Clang.

## Build, Test, and Lint

All commands run inside the devcontainer. Prefix with `devcontainer exec --workspace-folder .` when running from the host. Inside the container, the build tree is at `/workspace/build`.

### Build

```bash
# Full clang build (first time or after changes outside ECSL)
ninja -C /workspace/build clang

# Incremental rebuild of just the parse/ECSL objects (fast)
ninja -C /workspace/build AllClangUnitTests
```

### Test

```bash
# All ECSL unit tests
/workspace/build/tools/clang/unittests/AllClangUnitTests --gtest_filter='ECSL*'

# Single ECSL test suite
/workspace/build/tools/clang/unittests/AllClangUnitTests --gtest_filter='ECSLDeclDispatch*'

# ECSL lit tests
ninja -C /workspace/build check-clang-ecsl

# Driver tests
ninja -C /workspace/build check-clang-driver
```

### Format

CI measures `git diff llvmorg-22.1.3...HEAD` — this covers **every commit since the base tag**, including changes from already-merged PRs. Always format-check before pushing.

```bash
# Auto-fix a file
/workspace/build/bin/clang-format -i <file>

# Verify zero diff (same check as CI)
git diff -U0 llvmorg-22.1.3...HEAD -- '*.c' '*.cc' '*.cpp' '*.cxx' '*.h' '*.hpp' '*.td' \
  | python3 clang/tools/clang-format/clang-format-diff.py -p1 -binary /workspace/build/bin/clang-format \
  | wc -l   # must be 0
```

### Lint

Before running clang-tidy locally, generate the required TableGen headers (same as CI):

```bash
ninja -C /workspace/build clang-tablegen-targets llvm-headers
```

---

## Architecture: The ECSL Annotation Pipeline

All Arcanum additions live under `clang/include/clang/ECSL/` and `clang/lib/ECSL/`. The pipeline is activated only when `-fecsl` is passed and gates entirely on `LangOpts.ECSL`.

```
Driver (-fecsl) ──► cc1 LangOpts.ECSL = true
                         │
        SyntaxOnlyAction::BeginSourceFileAction
                         │ creates and registers:
              ┌──────────┴──────────────────────┐
              │                                 │
   ECSLCommentHandler               ECSLAnnotationStore
   (on Preprocessor)                (on Preprocessor)
              │
     Buffers /*@ */ and //@ comments
     into vector<PendingAnnotation>
              │
    Drained at two boundary types:
              │
   ┌──────────┴──────────────┐
   │                         │
ParseTopLevelDecl       ParseStatementOrDeclaration
(Parser.cpp)            AfterAttributes (ParseStmt.cpp)
   │                         │
   └──── parseECSLAnnotation (stub pass-through) ────┐
                                                      │
                                          ECSLAnnotationStore
                                          addForDecl(Decl*, annotations)
```

### Key files

| File | Role |
|------|------|
| `clang/include/clang/ECSL/ECSLCommentHandler.h` | `PendingAnnotation` struct; handler API |
| `clang/lib/ECSL/ECSLCommentHandler.cpp` | Filters `/*@` / `//@` from all comment tokens |
| `clang/include/clang/ECSL/ECSLAnnotationStore.h` | `DenseMap<Decl*, …>` and `DenseMap<Stmt*, …>` keyed store |
| `clang/include/clang/ECSL/ECSLParser.h` | `parseECSLAnnotation()` stub — identity pass-through for now |
| `clang/lib/Frontend/FrontendActions.cpp` | Wires handler + store onto PP in `BeginSourceFileAction` |
| `clang/lib/Parse/Parser.cpp` | Top-level drain after `ParseExternalDeclaration` |
| `clang/lib/Parse/ParseStmt.cpp` | Block-scope drain after `ParseDeclaration`, before `ActOnDeclStmt` |
| `clang/lib/Driver/ToolChains/Clang.cpp` | Explicit `-fecsl` forwarding to cc1 |

### Dependency rules

`clangECSL` links only `clangBasic + clangLex`. It must **not** depend on `clangParse` — `clangParse` depends on `clangECSL`, not the other way around.

---

## Key Conventions

### Branch and PR workflow

- Development branches off `arcanum`, not `main`.
- Feature branches follow the pattern `ecsl-<feature>` (e.g. `ecsl-stmt-dispatch`).
- PRs target `arcanum`; stacked PRs may temporarily target the preceding feature branch and are retargeted after merge.

### Driver flag forwarding

`CC1Option` visibility in `Options.td` does **not** auto-forward a flag to cc1. Flags like `-fecsl` require explicit handling in `Clang::ConstructJob` in `clang/lib/Driver/ToolChains/Clang.cpp`.

### ECSL drain pattern

Always call `Handler->takePending()` **unconditionally** when the handler is active, even on error-recovery paths where `Decl`/`Result` is empty. This prevents leaked annotations from bridging a parser-error boundary and attaching to a later unrelated construct. Only insert into the store when the Decl group is valid:

```cpp
if (auto *Handler = PP.getECSLCommentHandler()) {
  auto Pending = Handler->takePending();           // always drain
  if (Result && !Pending.empty()) {
    if (auto *Store = PP.getECSLAnnotationStore()) {
      // ... attach Parsed to Store
    }
  }
}
```

### AST name comparisons

Use `getName()` (returns `StringRef`) rather than `getNameAsString()` (returns `std::string`) when comparing `NamedDecl` names. In C-mode translation units, `getTranslationUnitDecl()->decls()` includes many implicit builtin `TypedefDecl`s before user decls — always search by name, never take "first of type".

### Naming (LLDB/ECSL code)

ECSL code follows LLDB naming conventions (which differ from upstream LLVM):
- Variables: `snake_case`
- Functions and methods: `UpperCamelCase`
- Member variables: `m_` prefix
- Static variables: `s_` prefix
- Global variables: `g_` prefix

### Unit test pattern

ECSL dispatch tests use a `SyntaxOnlyAction` subclass with a custom `ASTConsumer` that fires an inspector lambda inside `HandleTranslationUnit`. The store must be inspected there — `EndSourceFileAction` tears it down. See `ECSLDeclDispatchTest.cpp` for the canonical pattern.

### CI: format and lint scope

Both CI jobs diff against `llvmorg-22.1.3...HEAD` (three-dot diff), covering all arcanum commits. Pre-existing format/lint issues in merged PRs will fail CI on new branches. Fix them before opening a PR.
