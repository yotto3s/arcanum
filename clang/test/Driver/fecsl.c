// Verify -fecsl / -fno-ecsl are accepted by the driver and forwarded to cc1.

// Driver accepts the flags without error.
// RUN: %clang -### -fecsl -c %s 2>&1 | FileCheck -check-prefix=ACCEPTED %s
// RUN: %clang -### -fno-ecsl -c %s 2>&1 | FileCheck -check-prefix=ACCEPTED %s
// ACCEPTED-NOT: error: unknown argument
// ACCEPTED-NOT: error: unsupported option

// Driver forwards -fecsl to cc1.
// RUN: %clang -### -fecsl -x c -fsyntax-only %s 2>&1 | FileCheck -check-prefix=FORWARDED %s
// FORWARDED: "-fecsl"

// Driver does not forward -fno-ecsl (default-off; absence == off).
// RUN: %clang -### -fno-ecsl -x c -fsyntax-only %s 2>&1 | FileCheck -check-prefix=NOT_FORWARDED %s
// NOT_FORWARDED-NOT: "-fecsl"

// cc1 marshals -fecsl into LangOpts.ECSL; verify cc1 accepts it.
// RUN: %clang -cc1 -fecsl -fsyntax-only %s
