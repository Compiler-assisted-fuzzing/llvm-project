//===----------------------------------------------------------------------===//
///
/// \file
/// This pass is to be used during L1I CPU caches fuzzing. It splits functions
/// in a module as much as possible into separate sections whose names start
/// with "fuzz.". Essentially it is just the cut version of HotColdSplitting pass
///
//===----------------------------------------------------------------------===//

#include "llvm/Transforms/IPO/BasicBlocksExtraction.h"
#include "llvm/ADT/PostOrderIterator.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/Statistic.h"
#include "llvm/Analysis/AssumptionCache.h"
#include "llvm/Analysis/BlockFrequencyInfo.h"
#include "llvm/Analysis/OptimizationRemarkEmitter.h"
#include "llvm/Analysis/PostDominators.h"
#include "llvm/Analysis/ProfileSummaryInfo.h"
#include "llvm/Analysis/TargetTransformInfo.h"
#include "llvm/CompilerAssistedFuzzing/FuzzInfo.h"
#include "llvm/IR/BasicBlock.h"
#include "llvm/IR/CFG.h"
#include "llvm/IR/DiagnosticInfo.h"
#include "llvm/IR/Dominators.h"
#include "llvm/IR/EHPersonalities.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/Instruction.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/Module.h"
#include "llvm/IR/PassManager.h"
#include "llvm/IR/ProfDataUtils.h"
#include "llvm/IR/User.h"
#include "llvm/IR/Value.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/Transforms/IPO.h"
#include "llvm/Transforms/Utils/CodeExtractor.h"
#include <algorithm>
#include <cassert>
#include <limits>
#include <string>

#define DEBUG_TYPE "basicblocksextraction"

STATISTIC(NumRegionsFound, "Number of extracting regions found.");
STATISTIC(NumRegionsOutlined, "Number of regions outlined.");

using namespace llvm;

static cl::opt<bool> EnableExtractedSection(
    "enable-section", cl::init(true), cl::Hidden,
    cl::desc("Enable placement of extracted basic blocks"
             " into a separate section."));

static cl::opt<std::string>
    ExtractedSectionNamePrefix("bb-extraction-section-name", cl::init("__llvm_extracted"),
                    cl::Hidden,
                    cl::desc("Prefix of name of the section containing extracted BBs."));

namespace {

/// Check whether it's safe to outline \p BB.
static bool mayExtractBlock(const BasicBlock &BB) {
  // EH pads are unsafe to outline because doing so breaks EH type tables. It
  // follows that invoke instructions cannot be extracted, because CodeExtractor
  // requires unwind destinations to be within the extraction region.
  //
  // Resumes that are not reachable from a cleanup landing pad are considered to
  // be unreachable. It’s not safe to split them out either.

  if (BB.hasAddressTaken() || BB.isEHPad())
    return false;
  auto Term = BB.getTerminator();
  if (isa<InvokeInst>(Term) || isa<ResumeInst>(Term))
    return false;

  // Do not outline basic blocks that have token type instructions. e.g.,
  // exception:
  // %0 = cleanuppad within none []
  // call void @"?terminate@@YAXXZ"() [ "funclet"(token %0) ]
  // br label %continue-exception
  if (llvm::any_of(
          BB, [](const Instruction &I) { return I.getType()->isTokenTy(); })) {
    return false;
  }

  return true;
}

} // end anonymous namespace

// Split the single \p EntryPoint region. \p CE is the region code extractor.
Function *BasicBlocksExtraction::extractRegion(
    BasicBlock &EntryPoint, CodeExtractor &CE,
    const CodeExtractorAnalysisCache &CEAC,
    TargetTransformInfo &TTI, OptimizationRemarkEmitter &ORE) {
  Function *OrigF = EntryPoint.getParent();
  if (Function *OutF = CE.extractCodeRegion(CEAC)) {
    User *U = *OutF->user_begin();
    CallInst *CI = cast<CallInst>(U);
    NumRegionsOutlined++;
    if (TTI.useColdCCForColdCall(*OutF)) {
      OutF->setCallingConv(CallingConv::Cold);
      CI->setCallingConv(CallingConv::Cold);
    }
    CI->setIsNoInline();
    OutF->addFnAttr(Attribute::Extracted);

    LLVM_DEBUG(llvm::dbgs() << "Outlined Region: " << *OutF);
    ORE.emit([&]() {
      return OptimizationRemark(DEBUG_TYPE, "BBExtraction",
                                &*EntryPoint.begin())
             << ore::NV("Original", OrigF) << " split function into "
             << ore::NV("Split", OutF);
    });
    return OutF;
  }

  ORE.emit([&]() {
    return OptimizationRemarkMissed(DEBUG_TYPE, "ExtractFailed",
                                    &*EntryPoint.begin())
           << "Failed to extract region at block "
           << ore::NV("Block", &EntryPoint);
  });
  return nullptr;
}

/// A pair of (basic block, score).
using BlockTy = std::pair<BasicBlock *, unsigned>;

namespace {
/// A maximal outlining region. This contains all blocks post-dominated by a
/// sink block, the sink block itself, and all blocks dominated by the sink.
/// If sink-predecessors and sink-successors cannot be extracted in one region,
/// the static constructor returns a list of suitable extraction regions.
class OutliningRegion {
  /// A list of (block, score) pairs. A block's score is non-zero iff it's a
  /// viable sub-region entry point. Blocks with higher scores are better entry
  /// points (i.e. they are more distant ancestors of the sink block).
  SmallVector<BlockTy, 0> Blocks = {};

  /// The suggested entry point into the region. If the region has multiple
  /// entry points, all blocks within the region may not be reachable from this
  /// entry point.
  BasicBlock *SuggestedEntryPoint = nullptr;

  /// Whether the entire function is cold.
  bool EntireFunctionCold = false;

  /// If \p BB is a viable entry point, return \p Score. Return 0 otherwise.
  static unsigned getEntryPointScore(BasicBlock &BB, unsigned Score) {
    return mayExtractBlock(BB) ? Score : 0;
  }

  /// These scores should be lower than the score for predecessor blocks,
  /// because regions starting at predecessor blocks are typically larger.
  static constexpr unsigned ScoreForSuccBlock = 1;
  static constexpr unsigned ScoreForSinkBlock = 1;

  OutliningRegion(const OutliningRegion &) = delete;
  OutliningRegion &operator=(const OutliningRegion &) = delete;

public:
  OutliningRegion() = default;
  OutliningRegion(OutliningRegion &&) = default;
  OutliningRegion &operator=(OutliningRegion &&) = default;

  static std::vector<OutliningRegion> create(BasicBlock &SinkBB,
                                             const DominatorTree &DT,
                                             const PostDominatorTree &PDT) {
    std::vector<OutliningRegion> Regions;
    SmallPtrSet<BasicBlock *, 4> RegionBlocks;

    Regions.emplace_back();
    OutliningRegion *RegionToBeExtracted = &Regions.back();

    auto addBlockToRegion = [&](BasicBlock *BB, unsigned Score) {
      RegionBlocks.insert(BB);
      RegionToBeExtracted->Blocks.emplace_back(BB, Score);
    };

    // The ancestor farthest-away from SinkBB, and also post-dominated by it.
    unsigned SinkScore = getEntryPointScore(SinkBB, ScoreForSinkBlock);
    RegionToBeExtracted->SuggestedEntryPoint = (SinkScore > 0) ? &SinkBB : nullptr;

    // Ignore SinkBB's ancestors.

    // If the sink can be added to the extracting region, do so. It's considered as
    // an entry point before any sink-successor blocks.
    //
    // Otherwise, split cold sink-successor blocks using a separate region.
    // This satisfies the requirement that all extraction blocks other than the
    // first have predecessors within the extraction region.
    if (mayExtractBlock(SinkBB))
      addBlockToRegion(&SinkBB, SinkScore);

    // Ignore all successors of SinkBB.
    return Regions;
  }

  /// Whether this region has nothing to extract.
  bool empty() const { return !SuggestedEntryPoint; }

  /// The blocks in this region.
  ArrayRef<std::pair<BasicBlock *, unsigned>> blocks() const { return Blocks; }

  /// Whether the entire function containing this region is cold.
  bool isEntireFunctionCold() const { return EntireFunctionCold; }

  /// Remove a sub-region from this region and return it as a block sequence.
  BlockSequence takeSingleEntrySubRegion(DominatorTree &DT) {
    assert(!empty() && !isEntireFunctionCold() && "Nothing to extract");

    // Remove blocks dominated by the suggested entry point from this region.
    // During the removal, identify the next best entry point into the region.
    // Ensure that the first extracted block is the suggested entry point.
    BlockSequence SubRegion = {SuggestedEntryPoint};
    BasicBlock *NextEntryPoint = nullptr;
    unsigned NextScore = 0;
    auto RegionEndIt = Blocks.end();
    auto RegionStartIt = remove_if(Blocks, [&](const BlockTy &Block) {
      BasicBlock *BB = Block.first;
      unsigned Score = Block.second;
      bool InSubRegion =
          BB == SuggestedEntryPoint  || DT.dominates(SuggestedEntryPoint, BB);
      if (!InSubRegion && Score > NextScore) {
        NextEntryPoint = BB;
        NextScore = Score;
      }
      if (InSubRegion && BB != SuggestedEntryPoint)
        SubRegion.push_back(BB);
      return InSubRegion;
    });
    if (!Blocks.empty())
        Blocks.erase(RegionStartIt, RegionEndIt);

    // Update the suggested entry point.
    SuggestedEntryPoint = NextEntryPoint;

    return SubRegion;
  }
};
} // namespace

bool BasicBlocksExtraction::outlineRegions(Function &F) {
  // The set of blocks outlined.
  SmallPtrSet<BasicBlock *, 4> Blocks;

  // The set of cold blocks cannot be outlined.
  SmallPtrSet<BasicBlock *, 4> CannotBeOutlinedBlocks;

  // Set of cold blocks obtained with RPOT.
  SmallPtrSet<BasicBlock *, 4> AnnotatedBlocks;

  // The worklist of non-intersecting regions left to outline. The first member
  // of the pair is the entry point into the region to be outlined.
  SmallVector<std::pair<BasicBlock *, CodeExtractor>, 2> OutliningWorklist;

  // Set up an RPO traversal. Experimentally, this performs better (outlines
  // more) than a PO traversal, because we prevent region overlap by keeping
  // the first region to contain a block.
  ReversePostOrderTraversal<Function *> RPOT(&F);

  // Calculate domtrees lazily. This reduces compile-time significantly.
  std::unique_ptr<DominatorTree> DT;
  std::unique_ptr<PostDominatorTree> PDT;

  TargetTransformInfo &TTI = GetTTI(F);
  OptimizationRemarkEmitter &ORE = (*GetORE)(F);
  AssumptionCache *AC = LookupAC(F);

  unsigned OutlinedFunctionID = 1;
  // Traverse all BBs of F
  for (BasicBlock *BB : RPOT) {
    // This block is already part of some outlining region.
    if (Blocks.count(BB))
      continue;

    // This block is already part of some region cannot be outlined.
    if (CannotBeOutlinedBlocks.count(BB))
      continue;

    LLVM_DEBUG({
      dbgs() << "Extracting a block:\n";
      BB->dump();
    });

    if (!DT)
      DT = std::make_unique<DominatorTree>(F);
    if (!PDT)
      PDT = std::make_unique<PostDominatorTree>(F);

    auto Regions = OutliningRegion::create(*BB, *DT, *PDT);
    for (OutliningRegion &Region : Regions) {
      if (Region.empty())
        continue;

      do {
        BlockSequence SubRegion = Region.takeSingleEntrySubRegion(*DT);
        CodeExtractor CE(
            SubRegion, &*DT, /* AggregateArgs */ false, /* BFI */ nullptr,
            /* BPI */ nullptr, AC, /* AllowVarArgs */ false,
            /* AllowAlloca */ false, /* AllocaBlock */ nullptr,
            /* Suffix */ "fuzz." + std::to_string(OutlinedFunctionID));

        if (CE.isEligible() && !SubRegion.empty() &&
            // If this outlining region intersects with another, drop the new
            // region.
            none_of(SubRegion, [&](BasicBlock *Block) {
              return Blocks.contains(Block);
            })) {
          Blocks.insert(SubRegion.begin(), SubRegion.end());

          LLVM_DEBUG({
            for (auto *Block : SubRegion)
              dbgs() << "  contains block:" << Block->getName() << "\n";
          });

          OutliningWorklist.emplace_back(
              std::make_pair(SubRegion[0], std::move(CE)));
          ++OutlinedFunctionID;
        } else {
          // The block region cannot be outlined.
          for (auto *Block : SubRegion)
            if ((DT->dominates(BB, Block) && PDT->dominates(Block, BB)) ||
                (PDT->dominates(BB, Block) && DT->dominates(Block, BB)))
              // Will skip this block in the loop to save the compile time
              CannotBeOutlinedBlocks.insert(Block);
        }
      } while (!Region.empty());

      ++NumRegionsFound;
    }
  }

  if (OutliningWorklist.empty())
    return false;

  CodeExtractorAnalysisCache CEAC(F);
  uint64_t SectionId = 0;
  for (auto &BCE : OutliningWorklist) {
    Function *Outlined =
        extractRegion(*BCE.first, BCE.second, CEAC, TTI, ORE);
    Outlined->setSection(ExtractedSectionNamePrefix + std::to_string(SectionId));
    SectionId++;
    assert(Outlined && "Should be outlined");
    (void)Outlined;
  }

  return true;
}

bool BasicBlocksExtraction::run(Module &M) {
  bool Changed = false;

  for (Function &F : M) {
    // Do not touch declarations.
    if (F.isDeclaration())
      continue;

    // Do not modify `optnone` functions.
    if (F.hasOptNone())
      continue;

    // Do not try to extract already extracted BBs
    if (F.hasFnAttribute(Attribute::Extracted))
      continue;

    LLVM_DEBUG(llvm::dbgs() << "Outlining in " << F.getName() << "\n");
    Changed |= outlineRegions(F);
  }

  unsigned long SectionsCount = 1;
  for (auto &G : M.globals()) {
    if (!G.getSection().empty()) // Don't modify unique sections
      continue;
    if (G.getLinkage() != GlobalValue::LinkageTypes::ExternalLinkage)
      continue;
    G.setSection(".globalVariable.fuzz" + std::to_string(SectionsCount));
    SectionsCount++;
  }

  return Changed;
}

PreservedAnalyses
BasicBlocksExtractionPass::run(Module &M, ModuleAnalysisManager &AM) {
  auto &FAM = AM.getResult<FunctionAnalysisManagerModuleProxy>(M).getManager();

  auto LookupAC = [&FAM](Function &F) -> AssumptionCache * {
    return FAM.getCachedResult<AssumptionAnalysis>(F);
  };

  std::function<TargetTransformInfo &(Function &)> GTTI =
      [&FAM](Function &F) -> TargetTransformInfo & {
    return FAM.getResult<TargetIRAnalysis>(F);
  };

  std::unique_ptr<OptimizationRemarkEmitter> ORE;
  std::function<OptimizationRemarkEmitter &(Function &)> GetORE =
      [&ORE](Function &F) -> OptimizationRemarkEmitter & {
    ORE.reset(new OptimizationRemarkEmitter(&F));
    return *ORE;
  };

  ProfileSummaryInfo *PSI = &AM.getResult<ProfileSummaryAnalysis>(M);

  if (BasicBlocksExtraction(PSI, GTTI, &GetORE, LookupAC).run(M))
    return PreservedAnalyses::none();
  return PreservedAnalyses::all();
}
