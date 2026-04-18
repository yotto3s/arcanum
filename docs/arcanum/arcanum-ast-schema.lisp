;;;; arcanum-ast-schema.lisp
;;;;
;;;; AST node schema — single source of truth for Clang AST ↔ S-expression.
;;;;
;;;; :clang accessor convention:
;;;;   symbol → auto-converted kebab-to-camelCase + "()"
;;;;   string → verbatim (for complex expressions)
;;;;   omitted → ECSL-only node, no C++ counterpart

(in-package :arcanum)

(define-ast-schema

  ;;; ==========================================================
  ;;; Statements
  ;;; ==========================================================

  (:stmt compound-stmt
    (stmts (:list :stmt) :clang children))

  (:stmt return-stmt
    (value (:maybe :expr) :clang get-ret-value))

  (:stmt if-stmt
    (cond :expr :clang get-cond)
    (then :stmt :clang get-then)
    (else-branch (:maybe :stmt) :clang get-else))

  (:stmt decl-stmt
    (decls (:list :decl) :clang decls))

  (:stmt expr-stmt
    (expr :expr :clang get-expr))

  (:stmt null-stmt)

  ;; M2: Loops & control flow

  (:stmt for-stmt
    (init (:maybe :stmt) :clang get-init)
    (cond (:maybe :expr) :clang get-cond)
    (inc (:maybe :expr) :clang get-inc)
    (body :stmt :clang get-body))

  (:stmt while-stmt
    (cond :expr :clang get-cond)
    (body :stmt :clang get-body))

  (:stmt do-stmt
    (cond :expr :clang get-cond)
    (body :stmt :clang get-body))

  (:stmt break-stmt)
  (:stmt continue-stmt)

  (:stmt switch-stmt
    (cond :expr :clang get-cond)
    (body :stmt :clang get-body))

  (:stmt case-stmt
    (value :expr :clang get-lhs)
    (body :stmt :clang get-sub-stmt))

  (:stmt default-stmt
    (body :stmt :clang get-sub-stmt))

  ;;; ==========================================================
  ;;; Expressions
  ;;; ==========================================================

  (:expr binary-op
    (opcode :keyword :clang "getOpcodeStr(getOpcode())")
    (lhs :expr :clang get-lhs)
    (rhs :expr :clang get-rhs)
    (type :c-type :clang get-type))

  (:expr unary-op
    (opcode :keyword :clang "getOpcodeStr(getOpcode())")
    (operand :expr :clang get-sub-expr)
    (is-postfix :bool :clang is-postfix)
    (type :c-type :clang get-type))

  (:expr conditional-op
    (cond :expr :clang get-cond)
    (then :expr :clang get-true-expr)
    (else-branch :expr :clang get-false-expr)
    (type :c-type :clang get-type))

  (:expr var-ref
    (name :string :clang "getDecl()->getName()")
    (type :c-type :clang get-type))

  (:expr int-literal
    (value :integer :clang "getValue().getExtValue()")
    (type :c-type :clang get-type))

  (:expr float-literal
    (value :float :clang "getValue().convertToDouble()")
    (type :c-type :clang get-type))

  (:expr char-literal
    (value :integer :clang get-value)
    (type :c-type :clang get-type))

  (:expr call-expr
    (callee :string :clang "getDirectCallee()->getName()")
    (args (:list :expr) :clang arguments)
    (type :c-type :clang get-type))

  (:expr paren-expr
    (inner :expr :clang get-sub-expr))

  (:expr implicit-cast
    (operand :expr :clang get-sub-expr)
    (cast-kind :keyword :clang "getCastKindName(getCastKind())")
    (type :c-type :clang get-type))

  ;; M2: Pointer/array/member

  (:expr array-subscript
    (base :expr :clang get-base)
    (index :expr :clang get-idx)
    (type :c-type :clang get-type))

  (:expr member-expr
    (base :expr :clang get-base)
    (field :string :clang "getMemberDecl()->getName()")
    (is-arrow :bool :clang is-arrow)
    (type :c-type :clang get-type))

  (:expr addr-of
    (operand :expr :clang get-sub-expr)
    (type :c-type :clang get-type))

  (:expr deref
    (operand :expr :clang get-sub-expr)
    (type :c-type :clang get-type))

  (:expr compound-assign
    (opcode :keyword :clang "getOpcodeStr(getOpcode())")
    (lhs :expr :clang get-lhs)
    (rhs :expr :clang get-rhs)
    (type :c-type :clang get-type))

  (:expr sizeof-expr
    (is-type :bool :clang is-argument-type)
    (arg-type (:maybe :c-type) :clang get-argument-type)
    (arg-expr (:maybe :expr) :clang get-argument-expr)
    (type :c-type :clang get-type))

  (:expr init-list
    (inits (:list :expr) :clang inits)
    (type :c-type :clang get-type))

  (:expr c-style-cast
    (operand :expr :clang get-sub-expr)
    (target-type :c-type :clang get-type-as-written)
    (type :c-type :clang get-type))

  ;;; ==========================================================
  ;;; Declarations
  ;;; ==========================================================

  (:decl function-decl
    (name :string :clang get-name)
    (mangled-name :string :clang "getMangledName()")
    (params (:list :decl) :clang parameters)
    (return-type :c-type :clang get-return-type)
    (body (:maybe :stmt) :clang get-body)
    (is-variadic :bool :clang is-variadic))

  (:decl param-decl
    (name :string :clang get-name)
    (type :c-type :clang get-type))

  (:decl var-decl
    (name :string :clang get-name)
    (type :c-type :clang get-type)
    (init (:maybe :expr) :clang get-init))

  (:decl field-decl
    (name :string :clang get-name)
    (type :c-type :clang get-type))

  (:decl record-decl
    (name :string :clang get-name)
    (tag :keyword :clang "isStruct() ? \"struct\" : isUnion() ? \"union\" : \"class\"")
    (fields (:list :decl) :clang fields))

  (:decl enum-decl
    (name :string :clang get-name)
    (enumerators (:list :decl) :clang enumerators))

  (:decl enum-constant-decl
    (name :string :clang get-name)
    (value (:maybe :expr) :clang get-init-expr))

  (:decl typedef-decl
    (name :string :clang get-name)
    (underlying-type :c-type :clang get-underlying-type))

  ;;; ==========================================================
  ;;; Types
  ;;; ==========================================================

  (:type builtin-type
    (kind :keyword :clang "getName(Policy)"))

  (:type pointer-type
    (pointee :c-type :clang get-pointee-type))

  (:type reference-type
    (referent :c-type :clang get-pointee-type))

  (:type record-type
    (name :string :clang "getDecl()->getName()"))

  (:type enum-type
    (name :string :clang "getDecl()->getName()"))

  (:type array-type
    (element :c-type :clang get-element-type)
    (size (:maybe :integer) :clang "getSize().getExtValue()"))

  (:type function-proto-type
    (return-type :c-type :clang get-return-type)
    (param-types (:list :c-type) :clang param-types))

  (:type qualified-type
    (base :c-type :clang get-unqualified-type)
    (is-const :bool :clang is-const-qualified)
    (is-volatile :bool :clang is-volatile-qualified))

  ;;; ==========================================================
  ;;; ECSL Contracts (no :clang — produced by ECSL parser)
  ;;; ==========================================================

  (:ecsl ecsl-contract
    (requires (:list :ecsl-clause))
    (ensures (:list :ecsl-clause))
    (assigns (:list :ecsl-term))
    (behaviors (:list :ecsl-behavior))
    (terminates (:maybe :ecsl-pred))
    (decreases (:maybe :ecsl-term)))

  (:ecsl ecsl-clause
    (modifier :keyword)
    (pred :ecsl-pred))

  (:ecsl ecsl-behavior
    (name :string)
    (assumes (:maybe :ecsl-pred))
    (requires (:list :ecsl-clause))
    (ensures (:list :ecsl-clause))
    (assigns (:list :ecsl-term)))

  (:ecsl ecsl-loop-annotation
    (invariants (:list :ecsl-pred))
    (assigns (:list :ecsl-term))
    (variant (:maybe :ecsl-term)))

  (:ecsl ecsl-class-invariant
    (name :string)
    (pred :ecsl-pred))

  ;;; ==========================================================
  ;;; ECSL Predicates
  ;;; ==========================================================

  (:ecsl-pred ecsl-true)
  (:ecsl-pred ecsl-false)

  (:ecsl-pred ecsl-comparison
    (op :keyword)
    (lhs :ecsl-term)
    (rhs :ecsl-term))

  (:ecsl-pred ecsl-and (lhs :ecsl-pred) (rhs :ecsl-pred))
  (:ecsl-pred ecsl-or (lhs :ecsl-pred) (rhs :ecsl-pred))
  (:ecsl-pred ecsl-not (operand :ecsl-pred))
  (:ecsl-pred ecsl-implies (lhs :ecsl-pred) (rhs :ecsl-pred))
  (:ecsl-pred ecsl-equiv (lhs :ecsl-pred) (rhs :ecsl-pred))

  (:ecsl-pred ecsl-forall
    (binders (:list :ecsl-binder))
    (body :ecsl-pred))

  (:ecsl-pred ecsl-exists
    (binders (:list :ecsl-binder))
    (body :ecsl-pred))

  (:ecsl-pred ecsl-valid (arg :ecsl-term))
  (:ecsl-pred ecsl-valid-read (arg :ecsl-term))
  (:ecsl-pred ecsl-separated (args (:list :ecsl-term)))
  (:ecsl-pred ecsl-freeable (arg :ecsl-term))
  (:ecsl-pred ecsl-allocable (arg :ecsl-term))
  (:ecsl-pred ecsl-initialized (arg :ecsl-term))

  (:ecsl-pred ecsl-predicate-app
    (name :string)
    (args (:list :ecsl-term)))

  ;;; ==========================================================
  ;;; ECSL Terms
  ;;; ==========================================================

  (:ecsl-term ecsl-c-expr (expr :expr))
  (:ecsl-term ecsl-result)
  (:ecsl-term ecsl-old (inner :ecsl-term))
  (:ecsl-term ecsl-at (inner :ecsl-term) (label :string))
  (:ecsl-term ecsl-nothing)
  (:ecsl-term ecsl-everything)
  (:ecsl-term ecsl-range (low :ecsl-term) (high :ecsl-term))
  (:ecsl-term ecsl-base-addr (arg :ecsl-term))
  (:ecsl-term ecsl-offset (arg :ecsl-term))
  (:ecsl-term ecsl-block-length (arg :ecsl-term))

  (:ecsl-term ecsl-logic-func-app
    (name :string)
    (args (:list :ecsl-term)))

  (:ecsl-term ecsl-let
    (name :string)
    (value :ecsl-term)
    (body :ecsl-term))

  ;;; ==========================================================
  ;;; ECSL Binder
  ;;; ==========================================================

  (:ecsl ecsl-binder
    (name :string)
    (type :c-type))

  ) ; end define-ast-schema
