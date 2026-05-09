//==--- DiagnosticECSLKinds.td - ECSL annotation parser diagnostics -------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

let Component = "ECSL" in {
let CategoryName = "ECSL Issue" in {

def err_ecsl_expected_term : Error<
  "expected expression term">;

def err_ecsl_expected_rparen : Error<
  "expected ')' after predicate">;

def err_ecsl_expected_semi_after_requires : Error<
  "expected ';' after requires predicate">;

def err_ecsl_expected_semi_after_ensures : Error<
  "expected ';' after ensures predicate">;

def err_ecsl_assigns_not_nothing : Error<
  "expected '\\nothing' after 'assigns'; only 'assigns \\nothing' is supported in M1">;

def err_ecsl_expected_semi_after_assigns : Error<
  "expected ';' after 'assigns \\nothing'">;

def err_ecsl_unexpected_clause_token : Error<
  "expected 'requires', 'ensures', or 'assigns' at clause start">;

def err_ecsl_invalid_c_expr : Error<
  "invalid C expression in ECSL annotation">;

} // CategoryName
} // Component
