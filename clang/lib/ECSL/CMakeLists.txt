set(LLVM_LINK_COMPONENTS
  Support
  )

add_clang_library(clangECSL
  ECSLCommentHandler.cpp

  LINK_LIBS
  clangBasic
  clangLex
  )
