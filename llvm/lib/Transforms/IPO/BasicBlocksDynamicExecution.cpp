//===----------------------------------------------------------------------===//
///
/// \file
/// This pass is to be used during L1I CPU caches fuzzing. It takes basic blocks from functions,
/// copies them into dynamically allocated buffer,
/// and calls this buffer as function pointer
///
//===----------------------------------------------------------------------===//

#include "llvm/Transforms/IPO/BasicBlocksDynamicExecution.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/Statistic.h"
#include "llvm/Analysis/TargetLibraryInfo.h"
#include "llvm/CompilerAssistedFuzzing/FuzzInfo.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/Instruction.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/Module.h"
#include "llvm/IR/User.h"
#include "llvm/IR/Value.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/RandomNumberGenerator.h"
#include "llvm/Transforms/Utils/BasicBlockUtils.h"
#include "llvm/Transforms/Utils/BuildLibCalls.h"
#include <string>

#define DEBUG_TYPE "basic-blocks-dynamic-execution"

STATISTIC(NumFunctionsFound, "Number of instrumented functions found.");
STATISTIC(NumFunctionsProcessed, "Number of functions processed.");

using namespace llvm;

static cl::opt<unsigned> BasicBlocksDynamicExecutionPercentage(
    "basic-blocks-dynamic-execution-percentage",
    cl::desc("Percentage of basic blocks that have be copied into dynamically allocated buffer"),
    cl::init(25)); // Default is a good balance between entropy and
                   // performance impact

namespace {

static size_t FUNCTION_SIZE_FACTOR = 4 * 16;
static size_t FUNCTION_SIZE_CONSTANT = 128;
static int PROT_READ_VALUE = 0x1;                /* Page can be read. */
static int PROT_WRITE_VALUE = 0x2;                /* Page can be written. */
static int PROT_EXEC_VALUE = 0x4;                /* Page can be executed. */
static int MAP_PRIVATE_VALUE = 0x02;                /* Changes are private. */
static int MAP_ANONYMOUS_VALUE = 0x20;                /* Don't use a file. */

static FunctionCallee getOrDeclareMmap(Module &M, const TargetLibraryInfo &TLI) {
  PointerType *VoidPtrTy = PointerType::get(M.getContext(), 0);
  IntegerType *IntTy = IntegerType::get(M.getContext(), TLI.getIntSize());
  IntegerType *SizeTTy = IntegerType::get(M.getContext(), TLI.getSizeTSize(M));
  return M.getOrInsertFunction("mmap", VoidPtrTy, VoidPtrTy, SizeTTy, IntTy, IntTy, IntTy, SizeTTy);
}

static FunctionCallee getOrDeclareMunmap(Module &M, const TargetLibraryInfo &TLI) {
  PointerType *VoidPtrTy = PointerType::get(M.getContext(), 0);
  IntegerType *IntTy = IntegerType::get(M.getContext(), TLI.getIntSize());
  IntegerType *SizeTTy = IntegerType::get(M.getContext(), TLI.getSizeTSize(M));
  return M.getOrInsertFunction("munmap", IntTy, VoidPtrTy, SizeTTy);
}

static FunctionCallee getOrDeclareMempcpy(Module &M, const TargetLibraryInfo &TLI) {
  PointerType *VoidPtrTy = PointerType::get(M.getContext(), 0);
  IntegerType *SizeTTy = IntegerType::get(M.getContext(), TLI.getSizeTSize(M));
  return getOrInsertLibFunc(&M, TLI, LibFunc_mempcpy, VoidPtrTy, VoidPtrTy, VoidPtrTy, SizeTTy);
}

} // namespace

void BasicBlocksDynamicExecution::processCall(Module &M, CallInst *CI, TargetLibraryInfo &TLI, FunctionCallee mmapF, FunctionCallee munmapF, FunctionCallee mempcpyF) {
  PointerType *VoidPtrTy = PointerType::get(M.getContext(), 0);
  IntegerType *IntTy = IntegerType::get(M.getContext(), TLI.getIntSize());
  IntegerType *SizeTTy = IntegerType::get(M.getContext(), TLI.getSizeTSize(M));
  FunctionType *FunctionTy = CI->getFunctionType();

  Value *Addr = ConstantPointerNull::get(VoidPtrTy);
  Value *Length = ConstantInt::get(SizeTTy, FUNCTION_SIZE_FACTOR * GetIC(*CI->getCalledFunction()) + FUNCTION_SIZE_CONSTANT);
  Value *Prot = ConstantInt::get(IntTy, PROT_EXEC_VALUE | PROT_READ_VALUE | PROT_WRITE_VALUE);
  Value *Flags = ConstantInt::get(IntTy, MAP_PRIVATE_VALUE | MAP_ANONYMOUS_VALUE);
  Value *FD = ConstantInt::get(IntTy, -1);
  Value *Offset = ConstantInt::get(SizeTTy, 0);
  CallInst *mmapCI = CallInst::Create(mmapF, {Addr, Length, Prot, Flags, FD, Offset});
  const Function *mmapFN = dyn_cast<Function>(mmapF.getCallee()->stripPointerCasts());
  if (mmapFN) {
    mmapCI->setCallingConv(mmapFN->getCallingConv());
  }

  Value *Buf = mmapCI;
  Value *Src = ConstantExpr::getBitCast(CI->getCalledFunction(), VoidPtrTy);
  CallInst *mempcpyCI = CallInst::Create(mempcpyF, {Buf,Src, Length});
  const Function *mempcpyFN = dyn_cast<Function>(mempcpyF.getCallee()->stripPointerCasts());
  if (mempcpyFN) {
    mempcpyCI->setCallingConv(mempcpyFN->getCallingConv());
  }

  CallInst *munmapCI = CallInst::Create(munmapF, {Buf, Length});
  const Function *munmapFN = dyn_cast<Function>(munmapF.getCallee()->stripPointerCasts());
  if (munmapFN) {
    munmapCI->setCallingConv(munmapFN->getCallingConv());
  }

  SmallVector<Value *, 0> Args;
  for (Use &U : CI->args()) {
    Args.push_back(U.get());
  }
  CallInst *invokeCI = CallInst::Create(FunctionTy, Buf, Args);
  invokeCI->setCallingConv(CI->getCalledFunction()->getCallingConv());

  mmapCI->insertBefore(CI);
  mempcpyCI->insertBefore(CI);
  munmapCI->insertAfter(CI);
  ReplaceInstWithInst(CI, invokeCI);
}

bool BasicBlocksDynamicExecution::processFunction(Function &F) {
  ++NumFunctionsFound;
  if (Distribution(*RNG) >= BasicBlocksDynamicExecutionPercentage)
    return false;

  ++NumFunctionsProcessed;
  SmallVector<User *, 0> Users(F.user_begin(), F.user_end());
  for (User *U : Users) {
    CallInst *CI = dyn_cast<CallInst>(U);
    // Process only users that are `call` instructions.
    if (!CI)
      continue;

    Function *FN = CI->getFunction();
    Module *M = CI->getModule();
    TargetLibraryInfo &TLI = GetTLI(*FN);
    LLVM_DEBUG(llvm::dbgs() << "Declaring `mmap`, `mempcpy` and `munmap` in " << M->getName() << "\n");
    FunctionCallee mmapF = getOrDeclareMmap(*M, TLI);
    FunctionCallee munmapF = getOrDeclareMunmap(*M, TLI);
    FunctionCallee mempcpyF = getOrDeclareMempcpy(*M, TLI);
    LLVM_DEBUG(llvm::dbgs() << "Processing " << F.getName() << "'s call\n");
    processCall(*M, CI, TLI, std::move(mmapF), std::move(munmapF), std::move(mempcpyF));
  }
  return true;
}

BasicBlocksDynamicExecution::BasicBlocksDynamicExecution(function_ref<TargetLibraryInfo &(Function &)> GTLI, function_ref<unsigned(Function &)> GIC)
  : Distribution(0, 100)
  , GetTLI(GTLI)
  , GetIC(GIC) {
  // clamp percentage to 100
  if (BasicBlocksDynamicExecutionPercentage > 100) {
    BasicBlocksDynamicExecutionPercentage = 100;
  }
}

bool BasicBlocksDynamicExecution::run(Module &M) {
  // The RNG must be initialized on first use so we have a Module to
  // construct it from
  if (!RNG) {
    RNG = M.createRNG("BasicBlocksDynamicExecution");
  }

  bool Changed = false;

  for (Function &F : M) {
    // Do not touch declarations.
    if (F.isDeclaration())
      continue;

    // Do not modify `optnone` functions.
    if (F.hasOptNone())
      continue;

    // Do not touch functions without `extracted` attribute.
    if (!F.hasFnAttribute(Attribute::Extracted))
      continue;

    Changed |= processFunction(F);
  }
  return Changed;
}

PreservedAnalyses
BasicBlocksDynamicExecutionPass::run(Module &M, ModuleAnalysisManager &AM) {
  auto &FAM = AM.getResult<FunctionAnalysisManagerModuleProxy>(M).getManager();

  auto GetTLI = [&FAM](Function &F) -> TargetLibraryInfo & {
    return FAM.getResult<TargetLibraryAnalysis>(F);
  };

  auto GetIC = [](Function &F) -> unsigned {
    return F.getInstructionCount();
  };

  if (BasicBlocksDynamicExecution(GetTLI, GetIC).run(M))
    return PreservedAnalyses::none();
  return PreservedAnalyses::all();
}
