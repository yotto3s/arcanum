;;;; define-ast-schema.lisp
;;;;
;;;; Macro that expands AST schema definitions into:
;;;;   1. CL defstructs for each node
;;;;   2. CL deserializer (S-expression → struct)
;;;;   3. C++ emitter generator (writes .cpp file)
;;;;
;;;; Accessor convention:
;;;;   :clang value is a symbol  → auto-convert kebab to camelCase + "()"
;;;;     get-lhs        → getLHS()
;;;;     is-arrow        → isArrow()
;;;;     get-sub-expr    → getSubExpr()
;;;;     children        → children()
;;;;   :clang value is a string → use verbatim (for complex expressions)
;;;;     "getDecl()->getName()" → getDecl()->getName()

(in-package :arcanum)

;;; ============================================================
;;; Utility
;;; ============================================================

(defun symbolicate (&rest parts)
  "Concatenate parts into an interned symbol."
  (intern (format nil "~{~A~}" (mapcar #'string-upcase
                                       (mapcar #'string parts)))))

(defun kebab-to-case (name &key (capitalize-first nil))
  "Convert kebab-case string. If capitalize-first, PascalCase; otherwise camelCase."
  (let ((str (string name)))
    (with-output-to-string (s)
      (let ((capitalize capitalize-first))
        (loop for ch across str
              do (cond ((char= ch #\-)
                        (setf capitalize t))
                       (capitalize
                        (write-char (char-upcase ch) s)
                        (setf capitalize nil))
                       (t
                        (write-char ch s))))))))

(defun kebab-to-camel (name)
  "\"get-lhs\" → \"getLhs\""
  (kebab-to-case name))

(defun kebab-to-pascal (name)
  "\"binary-op\" → \"BinaryOp\""
  (kebab-to-case name :capitalize-first t))

(defun clang-accessor-string (accessor-spec)
  "Convert an accessor spec to a C++ accessor string.
   Symbol → kebab-to-camel + \"()\"
   String → verbatim"
  (etypecase accessor-spec
    (symbol (format nil "~A()" (kebab-to-camel (string-downcase (string accessor-spec)))))
    (string accessor-spec)))

;;; ============================================================
;;; Schema data structures
;;; ============================================================

(defvar *ast-schema* (make-hash-table :test 'eq)
  "Maps node name (symbol) → ast-node-def.")

(defvar *ast-categories* (make-hash-table :test 'eq)
  "Maps category keyword → list of node names.")

(defstruct ast-field
  name       ; symbol
  type       ; type spec
  clang)     ; accessor spec: symbol or string, or nil

(defstruct ast-node-def
  name       ; symbol
  category   ; keyword
  fields)    ; list of ast-field

;;; ============================================================
;;; Schema parser
;;; ============================================================

(defun parse-field-def (field-sexp)
  "Parse (name type [:clang accessor]) into ast-field."
  (destructuring-bind (name type &key clang) field-sexp
    (make-ast-field :name name :type type :clang clang)))

(defun parse-node-def (category body)
  "Parse (category name field...) into ast-node-def."
  (make-ast-node-def
   :name (car body)
   :category category
   :fields (mapcar #'parse-field-def (cdr body))))

;;; ============================================================
;;; CL defstruct generation
;;; ============================================================

(defun generate-defstruct (node-def)
  `(defstruct ,(ast-node-def-name node-def)
     ,@(loop for f in (ast-node-def-fields node-def)
             collect (ast-field-name f))))

;;; ============================================================
;;; CL deserializer generation
;;; ============================================================

(defun generate-field-deser (field index)
  "Generate deserialization code for field at position INDEX in sexp."
  (let ((type (ast-field-type field))
        (acc `(nth ,index sexp)))
    (cond
      ;; Leaf types — use directly
      ((member type '(:string :keyword :integer :float :character :bool :source-loc))
       acc)
      ;; Recursive AST node
      ((member type '(:expr :stmt :decl :c-type :ecsl-pred :ecsl-term :ecsl :ecsl-clause :ecsl-behavior))
       `(deserialize-node ,acc))
      ;; Maybe
      ((and (listp type) (eq (car type) :maybe))
       `(when ,acc (deserialize-node ,acc)))
      ;; List
      ((and (listp type) (eq (car type) :list))
       `(mapcar #'deserialize-node ,acc))
      ;; Fallback
      (t acc))))

(defun generate-deser-clause (node-def)
  "Generate a case clause for deserialize-node."
  (let ((name (ast-node-def-name node-def))
        (fields (ast-node-def-fields node-def)))
    `(,name
      (,(symbolicate 'make- name)
       ,@(loop for f in fields
               for i from 1
               append (list (intern (string (ast-field-name f)) :keyword)
                            (generate-field-deser f i)))))))

(defun generate-deserializer (all-nodes)
  `(defun deserialize-node (sexp)
     "Deserialize an S-expression into an AST struct."
     (cond
       ((null sexp) nil)
       ;; Leaf nodes (no fields) — bare symbols
       ((symbolp sexp)
        (case sexp
          ,@(loop for n in all-nodes
                  when (null (ast-node-def-fields n))
                  collect `(,(ast-node-def-name n)
                            (,(symbolicate 'make- (ast-node-def-name n)))))
          (t (error "Unknown leaf node: ~A" sexp))))
       ;; Nodes with fields — lists
       ((listp sexp)
        (case (car sexp)
          ,@(loop for n in all-nodes
                  when (ast-node-def-fields n)
                  collect (generate-deser-clause n))
          (t (error "Unknown node: ~A" (car sexp)))))
       (t (error "Invalid sexp: ~A" sexp)))))

;;; ============================================================
;;; C++ emitter generation
;;; ============================================================

(defun cpp-var-for-category (category)
  (case category
    (:stmt "S") (:expr "E") (:decl "D") (:type "T") (t "N")))

(defun cpp-class-name (node-name)
  "binary-op → BinaryOp"
  (kebab-to-pascal (string-downcase (string node-name))))

(defun cpp-emit-field (field category os)
  "Generate C++ code to emit one field value."
  (let* ((type (ast-field-type field))
         (var (cpp-var-for-category category))
         (accessor (clang-accessor-string (ast-field-clang field)))
         (full (format nil "~A->~A" var accessor)))
    (cond
      ((eq type :string)
       (format nil "~A << quoted(~A);" os full))
      ((eq type :keyword)
       (format nil "~A << \":\" << ~A;" os full))
      ((member type '(:integer :float))
       (format nil "~A << ~A;" os full))
      ((eq type :bool)
       (format nil "~A << (~A ? \"t\" : \"nil\");" os full))
      ((eq type :source-loc)
       (format nil "emitLoc(~A, ~A);" full os))
      ((eq type :expr)
       (format nil "emitExpr(~A, ~A);" full os))
      ((eq type :stmt)
       (format nil "emitStmt(~A, ~A);" full os))
      ((eq type :decl)
       (format nil "emitDecl(~A, ~A);" full os))
      ((eq type :c-type)
       (format nil "emitType(~A, ~A);" full os))
      ((and (listp type) (eq (car type) :maybe))
       (format nil "if (~A) { ~A } else { ~A << \"nil\"; }"
               full
               (cpp-emit-field (make-ast-field :name (ast-field-name field)
                                              :type (cadr type)
                                              :clang (ast-field-clang field))
                              category os)
               os))
      ((and (listp type) (eq (car type) :list))
       (let ((emit-fn (case (cadr type)
                        (:expr "emitExpr") (:stmt "emitStmt")
                        (:decl "emitDecl") (:c-type "emitType")
                        (t "emitNode"))))
         (format nil "~A << \"(\"; for (auto *child : ~A) { ~A << \" \"; ~A(child, ~A); } ~A << \")\";"
                 os full os emit-fn os os)))
      (t (format nil "// TODO: ~A" (ast-field-name field))))))

(defun generate-cpp-stmt-case (node-def os)
  "Generate a case clause for a Stmt/Expr node."
  (let* ((name (ast-node-def-name node-def))
         (category (ast-node-def-category node-def))
         (fields (ast-node-def-fields node-def))
         (cpp-class (cpp-class-name name))
         (var (cpp-var-for-category category))
         (tag (string-downcase (string name))))
    (with-output-to-string (out)
      (if fields
          (progn
            (format out "  case Stmt::~AClass: {~%" cpp-class)
            (format out "    auto *~A = cast<~A>(Node);~%" var cpp-class)
            (format out "    ~A << \"(~A\";" os tag)
            (loop for f in fields
                  do (format out "~%    ~A << \" \"; ~A" os (cpp-emit-field f category os)))
            (format out "~%    ~A << \")\";~%" os)
            (format out "    break;~%")
            (format out "  }~%"))
          ;; Leaf node (no fields)
          (progn
            (format out "  case Stmt::~AClass:~%" cpp-class)
            (format out "    ~A << \"~A\";~%" os tag)
            (format out "    break;~%"))))))

(defun generate-cpp-decl-case (node-def os)
  "Generate a case clause for a Decl node."
  (let* ((name (ast-node-def-name node-def))
         (fields (ast-node-def-fields node-def))
         (cpp-class (cpp-class-name name))
         (tag (string-downcase (string name))))
    (with-output-to-string (out)
      (format out "  case Decl::~A: {~%" cpp-class)
      (format out "    auto *D = cast<~ADecl>(Node);~%" cpp-class)
      (format out "    ~A << \"(~A\";" os tag)
      (loop for f in fields
            do (format out "~%    ~A << \" \"; ~A" os (cpp-emit-field f :decl os)))
      (format out "~%    ~A << \")\";~%" os)
      (format out "    break;~%")
      (format out "  }~%"))))

(defun generate-cpp-emitter (all-nodes &key (output-path "ast_emitter.cpp"))
  "Write a complete C++ emitter source file."
  (let ((stmts (remove-if-not (lambda (n) (member (ast-node-def-category n) '(:stmt :expr)))
                              all-nodes))
        (decls (remove-if-not (lambda (n) (eq (ast-node-def-category n) :decl)) all-nodes)))
    (with-open-file (out output-path :direction :output :if-exists :supersede)
      (format out "// AUTO-GENERATED from arcanum-ast-schema.lisp~%")
      (format out "// Do not edit manually.~%~%")
      (format out "#include \"ASTEmitter.h\"~%")
      (format out "#include \"clang/AST/Expr.h\"~%")
      (format out "#include \"clang/AST/Decl.h\"~%")
      (format out "#include \"clang/AST/Stmt.h\"~%~%")
      (format out "using namespace clang;~%~%")
      (format out "static std::string quoted(StringRef S) {~%")
      (format out "  return \"\\\"\" + S.str() + \"\\\"\";~%")
      (format out "}~%~%")

      ;; emitStmt
      (format out "void ASTEmitter::emitStmt(const Stmt *Node, raw_ostream &OS) {~%")
      (format out "  if (!Node) { OS << \"nil\"; return; }~%")
      (format out "  switch (Node->getStmtClass()) {~%")
      (loop for n in stmts do (format out "~A" (generate-cpp-stmt-case n "OS")))
      (format out "  default: OS << \"(unknown-stmt)\"; break;~%")
      (format out "  }~%}~%~%")

      ;; emitExpr
      (format out "void ASTEmitter::emitExpr(const Expr *Node, raw_ostream &OS) {~%")
      (format out "  emitStmt(Node, OS);~%}~%~%")

      ;; emitDecl
      (format out "void ASTEmitter::emitDecl(const Decl *Node, raw_ostream &OS) {~%")
      (format out "  if (!Node) { OS << \"nil\"; return; }~%")
      (format out "  switch (Node->getKind()) {~%")
      (loop for n in decls do (format out "~A" (generate-cpp-decl-case n "OS")))
      (format out "  default: OS << \"(unknown-decl)\"; break;~%")
      (format out "  }~%}~%~%")

      ;; emitType — simplified, emit as string for now
      (format out "void ASTEmitter::emitType(QualType QT, raw_ostream &OS) {~%")
      (format out "  if (QT.isNull()) { OS << \"nil\"; return; }~%")
      (format out "  OS << \"(type \" << quoted(QT.getAsString()) << \")\";~%")
      (format out "}~%~%")

      ;; emitLoc
      (format out "void ASTEmitter::emitLoc(SourceLocation Loc, raw_ostream &OS) {~%")
      (format out "  if (Loc.isInvalid()) { OS << \"nil\"; return; }~%")
      (format out "  auto &SM = Context.getSourceManager();~%")
      (format out "  OS << \"(loc \" << quoted(SM.getFilename(Loc))~%")
      (format out "     << \" \" << SM.getSpellingLineNumber(Loc)~%")
      (format out "     << \" \" << SM.getSpellingColumnNumber(Loc) << \")\";~%")
      (format out "}~%")))

  (format t "Generated ~A~%" output-path))

;;; ============================================================
;;; The macro
;;; ============================================================

(defmacro define-ast-schema (&body node-defs)
  "Define AST schema. Expands into defstructs + deserializer.
   Call generate-cpp-emitter-file to produce the C++ emitter."
  (let ((parsed (loop for (category . body) in node-defs
                      collect (parse-node-def category body))))
    `(progn
       ;; Register definitions
       ,@(loop for n in parsed
               collect `(setf (gethash ',(ast-node-def-name n) *ast-schema*)
                              (make-ast-node-def
                               :name ',(ast-node-def-name n)
                               :category ,(ast-node-def-category n)
                               :fields (list ,@(loop for f in (ast-node-def-fields n)
                                                     collect `(make-ast-field
                                                               :name ',(ast-field-name f)
                                                               :type ',(ast-field-type f)
                                                               :clang ',(ast-field-clang f)))))))
       ,@(loop for n in parsed
               collect `(pushnew ',(ast-node-def-name n)
                                 (gethash ,(ast-node-def-category n) *ast-categories* nil)))

       ;; Defstructs
       ,@(mapcar #'generate-defstruct parsed)

       ;; Deserializer
       ,(generate-deserializer parsed)

       ;; C++ emitter generator
       (defun generate-cpp-emitter-file (&optional (path "ast_emitter.cpp"))
         (generate-cpp-emitter
          (loop for v being the hash-values of *ast-schema* collect v)
          :output-path path))

       ,(length parsed))))
