set(LLVM_LINK_COMPONENTS
  Support
  )

add_clang_library(clangECSL
  ECSLAnnotationStore.cpp
  ECSLCommentHandler.cpp
  ECSLLexer.cpp
  ECSLParser.cpp

  LINK_LIBS
  clangBasic
  clangLex
  )
