//===----------------------------------------------------------------------===//
//
// This pass copies basic blocks into dynamically allocated buffer and executes their
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_TRANSFORMS_IPO_BASICBLOCKSDYNAMICEXECUTION_H
#define LLVM_TRANSFORMS_IPO_BASICBLOCKSDYNAMICEXECUTION_H

#include "llvm/IR/PassManager.h"
#include <random>

namespace llvm {

class CallInst;
class FunctionCallee;
class Module;
class RandomNumberGenerator;
class TargetLibraryInfo;

class BasicBlocksDynamicExecution {
public:
  BasicBlocksDynamicExecution(function_ref<TargetLibraryInfo &(Function &)> GTLI, function_ref<unsigned(Function &)> GIC);

  bool run(Module &M);

private:
  void processCall(Module &M, CallInst *CI, TargetLibraryInfo &TLI, FunctionCallee mmapF, FunctionCallee munmapF, FunctionCallee mempcpyF);

  bool processFunction(Function &F);

  std::unique_ptr<RandomNumberGenerator> RNG;
  std::uniform_real_distribution<double> Distribution;
  function_ref<TargetLibraryInfo &(Function &)> GetTLI;
  function_ref<unsigned(Function &)> GetIC;
};

/// Pass to copy basic blocks from functions into dynamically allocated buffer and execute their.
class BasicBlocksDynamicExecutionPass : public PassInfoMixin<BasicBlocksDynamicExecutionPass> {
public:
  PreservedAnalyses run(Module &M, ModuleAnalysisManager &AM);
};

} // end namespace llvm

#endif // LLVM_TRANSFORMS_IPO_BASICBLOCKSDYNAMICEXECUTION_H
