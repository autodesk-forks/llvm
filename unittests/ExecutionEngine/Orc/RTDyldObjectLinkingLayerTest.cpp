//===- RTDyldObjectLinkingLayerTest.cpp - RTDyld linking layer unit tests -===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include "llvm/ExecutionEngine/Orc/RTDyldObjectLinkingLayer.h"
#include "OrcTestCommon.h"
#include "llvm/ExecutionEngine/ExecutionEngine.h"
#include "llvm/ExecutionEngine/Orc/CompileUtils.h"
#include "llvm/ExecutionEngine/Orc/LambdaResolver.h"
#include "llvm/ExecutionEngine/Orc/NullResolver.h"
#include "llvm/ExecutionEngine/SectionMemoryManager.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/LLVMContext.h"
#include "gtest/gtest.h"

using namespace llvm;
using namespace llvm::orc;

namespace {

class RTDyldObjectLinkingLayerExecutionTest : public testing::Test,
                                              public OrcExecutionTest {

};

class SectionMemoryManagerWrapper : public SectionMemoryManager {
public:
  int FinalizationCount = 0;
  int NeedsToReserveAllocationSpaceCount = 0;

  bool needsToReserveAllocationSpace() override {
    ++NeedsToReserveAllocationSpaceCount;
    return SectionMemoryManager::needsToReserveAllocationSpace();
  }

  bool finalizeMemory(std::string *ErrMsg = nullptr) override {
    ++FinalizationCount;
    return SectionMemoryManager::finalizeMemory(ErrMsg);
  }
};

TEST(RTDyldObjectLinkingLayerTest, TestSetProcessAllSections) {
  class SectionMemoryManagerWrapper : public SectionMemoryManager {
  public:
    SectionMemoryManagerWrapper(bool &DebugSeen) : DebugSeen(DebugSeen) {}
    uint8_t *allocateDataSection(uintptr_t Size, unsigned Alignment,
                                 unsigned SectionID,
                                 StringRef SectionName,
                                 bool IsReadOnly) override {
      if (SectionName == ".debug_str")
        DebugSeen = true;
      return SectionMemoryManager::allocateDataSection(Size, Alignment,
                                                         SectionID,
                                                         SectionName,
                                                         IsReadOnly);
    }
  private:
    bool &DebugSeen;
  };

  RTDyldObjectLinkingLayer ObjLayer;

  LLVMContext Context;
  auto M = llvm::make_unique<Module>("", Context);
  M->setTargetTriple("x86_64-unknown-linux-gnu");
  Type *Int32Ty = IntegerType::get(Context, 32);
  GlobalVariable *GV =
    new GlobalVariable(*M, Int32Ty, false, GlobalValue::ExternalLinkage,
                         ConstantInt::get(Int32Ty, 42), "foo");

  GV->setSection(".debug_str");


  // Initialize the native target in case this is the first unit test
  // to try to build a TM.
  OrcNativeTarget::initialize();
  std::unique_ptr<TargetMachine> TM(
    EngineBuilder().selectTarget(Triple(M->getTargetTriple()), "", "",
                                 SmallVector<std::string, 1>()));
  if (!TM)
    return;

  auto Obj =
    std::make_shared<object::OwningBinary<object::ObjectFile>>(
      SimpleCompiler(*TM)(*M));

  bool DebugSectionSeen = false;
  auto SMMW =
    std::make_shared<SectionMemoryManagerWrapper>(DebugSectionSeen);
  auto Resolver =
    createLambdaResolver(
      [](const std::string &Name) {
        return JITSymbol(nullptr);
      },
      [](const std::string &Name) {
        return JITSymbol(nullptr);
      });

  {
    // Test with ProcessAllSections = false (the default).
    auto H = ObjLayer.addObject(Obj, SMMW, &*Resolver);
    ObjLayer.emitAndFinalize(H);
    EXPECT_EQ(DebugSectionSeen, false)
      << "Unexpected debug info section";
    ObjLayer.removeObject(H);
  }

  {
    // Test with ProcessAllSections = true.
    ObjLayer.setProcessAllSections(true);
    auto H = ObjLayer.addObject(Obj, SMMW, &*Resolver);
    ObjLayer.emitAndFinalize(H);
    EXPECT_EQ(DebugSectionSeen, true)
      << "Expected debug info section not seen";
    ObjLayer.removeObject(H);
  }
}

TEST_F(RTDyldObjectLinkingLayerExecutionTest, NoDuplicateFinalization) {
  if (!TM)
    return;

  RTDyldObjectLinkingLayer ObjLayer;
  SimpleCompiler Compile(*TM);

  // Create a pair of modules that will trigger recursive finalization:
  // Module 1:
  //   int bar() { return 42; }
  // Module 2:
  //   int bar();
  //   int foo() { return bar(); }
  //
  // Verify that the memory manager is only finalized once (for Module 2).
  // Failure suggests that finalize is being called on the inner RTDyld
  // instance (for Module 1) which is unsafe, as it will prevent relocation of
  // Module 2.

  ModuleBuilder MB1(Context, "", "dummy");
  {
    MB1.getModule()->setDataLayout(TM->createDataLayout());
    Function *BarImpl = MB1.createFunctionDecl<int32_t(void)>("bar");
    BasicBlock *BarEntry = BasicBlock::Create(Context, "entry", BarImpl);
    IRBuilder<> Builder(BarEntry);
    IntegerType *Int32Ty = IntegerType::get(Context, 32);
    Value *FourtyTwo = ConstantInt::getSigned(Int32Ty, 42);
    Builder.CreateRet(FourtyTwo);
  }

  auto Obj1 =
    std::make_shared<object::OwningBinary<object::ObjectFile>>(
      Compile(*MB1.getModule()));

  ModuleBuilder MB2(Context, "", "dummy");
  {
    MB2.getModule()->setDataLayout(TM->createDataLayout());
    Function *BarDecl = MB2.createFunctionDecl<int32_t(void)>("bar");
    Function *FooImpl = MB2.createFunctionDecl<int32_t(void)>("foo");
    BasicBlock *FooEntry = BasicBlock::Create(Context, "entry", FooImpl);
    IRBuilder<> Builder(FooEntry);
    Builder.CreateRet(Builder.CreateCall(BarDecl));
  }
  auto Obj2 =
    std::make_shared<object::OwningBinary<object::ObjectFile>>(
      Compile(*MB2.getModule()));

  auto Resolver =
    createLambdaResolver(
      [&](const std::string &Name) {
        if (auto Sym = ObjLayer.findSymbol(Name, true))
          return Sym;
        return JITSymbol(nullptr);
      },
      [](const std::string &Name) {
        return JITSymbol(nullptr);
      });

  auto SMMW = std::make_shared<SectionMemoryManagerWrapper>();
  ObjLayer.addObject(std::move(Obj1), SMMW, &*Resolver);
  auto H = ObjLayer.addObject(std::move(Obj2), SMMW, &*Resolver);
  ObjLayer.emitAndFinalize(H);
  ObjLayer.removeObject(H);
  
  // Finalization of module 2 should trigger finalization of module 1.
  // Verify that finalize on SMMW is only called once.
  EXPECT_EQ(SMMW->FinalizationCount, 1)
      << "Extra call to finalize";
}

TEST_F(RTDyldObjectLinkingLayerExecutionTest, NoPrematureAllocation) {
  if (!TM)
    return;

  RTDyldObjectLinkingLayer ObjLayer;
  SimpleCompiler Compile(*TM);

  // Create a pair of unrelated modules:
  //
  // Module 1:
  //   int foo() { return 42; }
  // Module 2:
  //   int bar() { return 7; }
  //
  // Both modules will share a memory manager. We want to verify that the
  // second object is not loaded before the first one is finalized. To do this
  // in a portable way, we abuse the
  // RuntimeDyld::MemoryManager::needsToReserveAllocationSpace hook, which is
  // called once per object before any sections are allocated.

  ModuleBuilder MB1(Context, "", "dummy");
  {
    MB1.getModule()->setDataLayout(TM->createDataLayout());
    Function *BarImpl = MB1.createFunctionDecl<int32_t(void)>("foo");
    BasicBlock *BarEntry = BasicBlock::Create(Context, "entry", BarImpl);
    IRBuilder<> Builder(BarEntry);
    IntegerType *Int32Ty = IntegerType::get(Context, 32);
    Value *FourtyTwo = ConstantInt::getSigned(Int32Ty, 42);
    Builder.CreateRet(FourtyTwo);
  }

  auto Obj1 =
    std::make_shared<object::OwningBinary<object::ObjectFile>>(
      Compile(*MB1.getModule()));

  ModuleBuilder MB2(Context, "", "dummy");
  {
    MB2.getModule()->setDataLayout(TM->createDataLayout());
    Function *BarImpl = MB2.createFunctionDecl<int32_t(void)>("bar");
    BasicBlock *BarEntry = BasicBlock::Create(Context, "entry", BarImpl);
    IRBuilder<> Builder(BarEntry);
    IntegerType *Int32Ty = IntegerType::get(Context, 32);
    Value *Seven = ConstantInt::getSigned(Int32Ty, 7);
    Builder.CreateRet(Seven);
  }
  auto Obj2 =
    std::make_shared<object::OwningBinary<object::ObjectFile>>(
      Compile(*MB2.getModule()));

  auto SMMW = std::make_shared<SectionMemoryManagerWrapper>();
  NullResolver NR;
  auto H = ObjLayer.addObject(std::move(Obj1), SMMW, &NR);
  ObjLayer.addObject(std::move(Obj2), SMMW, &NR);
  ObjLayer.emitAndFinalize(H);
  ObjLayer.removeObject(H);
  
  // Only one call to needsToReserveAllocationSpace should have been made.
  EXPECT_EQ(SMMW->NeedsToReserveAllocationSpaceCount, 1)
      << "More than one call to needsToReserveAllocationSpace "
         "(multiple unrelated objects loaded prior to finalization)";
}

} // end anonymous namespace
