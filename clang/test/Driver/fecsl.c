// Verify the driver accepts -fecsl / -fno-ecsl without "unknown argument".
// The flag currently has no behavior (scaffolding only), so the driver emits
// an "argument unused" warning for C source inputs; that warning is expected.
// Behavior and cc1 forwarding arrive in a follow-up PR.

// RUN: %clang -### -fecsl -c %s 2>&1 | FileCheck -check-prefix=ACCEPTED %s
// RUN: %clang -### -fno-ecsl -c %s 2>&1 | FileCheck -check-prefix=ACCEPTED %s
// ACCEPTED-NOT: error: unknown argument
// ACCEPTED-NOT: error: unsupported option

// cc1 marshals -fecsl into LangOpts.ECSL; verify cc1 accepts it.
// RUN: %clang -cc1 -fecsl -fsyntax-only %s
