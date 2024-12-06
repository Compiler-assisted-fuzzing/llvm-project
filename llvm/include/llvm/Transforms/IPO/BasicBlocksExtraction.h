//===----------------------------------------------------------------------===//
//
// This pass extracts basic blocks from functions. Essentially it is just the
// cut version of HotColdSplitting pass
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_TRANSFORMS_IPO_BASICBLOCKSEXTRACTION_H
#define LLVM_TRANSFORMS_IPO_BASICBLOCKSEXTRACTION_H

#include "llvm/IR/PassManager.h"
#include "llvm/Support/BranchProbability.h"

namespace llvm {

class Module;
class ProfileSummaryInfo;
class BasicBlock;
class BlockFrequencyInfo;
class TargetTransformInfo;
class OptimizationRemarkEmitter;
class AssumptionCache;
class DominatorTree;
class CodeExtractor;
class CodeExtractorAnalysisCache;

/// A sequence of basic blocks.
///
/// A 0-sized SmallVector is slightly cheaper to move than a std::vector.
using BlockSequence = SmallVector<BasicBlock *, 0>;

class BasicBlocksExtraction {
public:
  BasicBlocksExtraction(ProfileSummaryInfo *ProfSI,
                   function_ref<TargetTransformInfo &(Function &)> GTTI,
                   std::function<OptimizationRemarkEmitter &(Function &)> *GORE,
                   function_ref<AssumptionCache *(Function &)> LAC)
      : PSI(ProfSI), GetTTI(GTTI), GetORE(GORE), LookupAC(LAC) {}
  bool run(Module &M);

private:
  bool outlineRegions(Function &F);
  Function *extractRegion(BasicBlock &EntryPoint, CodeExtractor &CE,
                              const CodeExtractorAnalysisCache &CEAC,
                              TargetTransformInfo &TTI,
                              OptimizationRemarkEmitter &ORE);
  ProfileSummaryInfo *PSI;
  function_ref<BlockFrequencyInfo *(Function &)> GetBFI;
  function_ref<TargetTransformInfo &(Function &)> GetTTI;
  std::function<OptimizationRemarkEmitter &(Function &)> *GetORE;
  function_ref<AssumptionCache *(Function &)> LookupAC;
};

/// Pass to extract basic blocks from functions.
class BasicBlocksExtractionPass : public PassInfoMixin<BasicBlocksExtractionPass> {
public:
  PreservedAnalyses run(Module &M, ModuleAnalysisManager &AM);
};

} // end namespace llvm

#endif // LLVM_TRANSFORMS_IPO_BASICBLOCKSEXTRACTION_H

