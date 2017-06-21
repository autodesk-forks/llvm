set(LLVM_LINK_COMPONENTS
  AsmParser
  core
  ipo
  linker
  )

set(LinkerSources
  LinkModulesTest.cpp
  )

add_llvm_unittest(LinkerTests
  ${LinkerSources}
  )
