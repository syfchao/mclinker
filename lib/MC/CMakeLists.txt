add_llvm_library(MCLDMC
  Attribute.cpp
  AttributeSet.cpp
  CommandAction.cpp
  ContextFactory.cpp
  FileAction.cpp
  Input.cpp
  InputAction.cpp
  InputBuilder.cpp
  InputFactory.cpp
  MCLDDirectory.cpp
  SearchDirs.cpp
  SymbolCategory.cpp
  ZOption.cpp
  LINK_LIBS
    MCLDSupport
  )
