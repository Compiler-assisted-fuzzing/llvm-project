#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/SetVector.h"
#include "llvm/ADT/SmallPtrSet.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/Statistic.h"
#include "llvm/CodeGen/MachineBasicBlock.h"
#include "llvm/CodeGen/MachineFunction.h"
#include "llvm/CodeGen/MachineFunctionPass.h"
#include "llvm/CodeGen/TargetPassConfig.h"
#include "llvm/CompilerAssistedFuzzing/FuzzInfo.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/PrintPasses.h"
#include "llvm/InitializePasses.h"
#include "llvm/Pass.h"
#include "llvm/Target/TargetMachine.h"
#include "llvm/Support/CodeGen.h"


#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/SetVector.h"
#include "llvm/ADT/SmallPtrSet.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/Statistic.h"
#include "llvm/Analysis/BlockFrequencyInfoImpl.h"
#include "llvm/Analysis/ProfileSummaryInfo.h"
#include "llvm/CodeGen/MBFIWrapper.h"
#include "llvm/CodeGen/MachineBasicBlock.h"
#include "llvm/CodeGen/MachineBlockFrequencyInfo.h"
#include "llvm/CodeGen/MachineBranchProbabilityInfo.h"
#include "llvm/CodeGen/MachineFunction.h"
#include "llvm/CodeGen/MachineFunctionPass.h"
#include "llvm/CodeGen/MachineLoopInfo.h"
#include "llvm/CodeGen/MachinePostDominators.h"
#include "llvm/CodeGen/MachineSizeOpts.h"
#include "llvm/CodeGen/TailDuplicator.h"
#include "llvm/CodeGen/TargetInstrInfo.h"
#include "llvm/CodeGen/TargetLowering.h"
#include "llvm/CodeGen/TargetPassConfig.h"
#include "llvm/CodeGen/TargetSubtargetInfo.h"
#include "llvm/CompilerAssistedFuzzing/FuzzInfo.h"
#include "llvm/IR/DebugLoc.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/Module.h"
#include "llvm/IR/PrintPasses.h"
#include "llvm/InitializePasses.h"
#include "llvm/Pass.h"
#include "llvm/Support/Allocator.h"
#include "llvm/Support/BlockFrequency.h"
#include "llvm/Support/BranchProbability.h"
#include "llvm/Support/CodeGen.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/Compiler.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/Support/RandomNumberGenerator.h"
#include "llvm/Target/TargetMachine.h"
#include "llvm/Transforms/Utils/CodeLayout.h"
#include <algorithm>
#include <cassert>
#include <cstdint>
#include <iterator>
#include <memory>
#include <random>
#include <string>
#include <tuple>
#include <utility>
#include <vector>

using namespace llvm;

#define DEBUG_TYPE "block-shuffling"

static cl::opt<bool>
    ForceBlockShuffling("block-shuffling",
                       cl::desc("Force basic blocks shuffling."),
                       cl::init(false), cl::Hidden);

ALWAYS_ENABLED_STATISTIC(NumBlockShufflingEntry, "Number of times MachineBlockShuffling was called");
ALWAYS_ENABLED_STATISTIC(NumBasicBlocksShuffled, "Amount of machine basic blocks were shuffled");


namespace llvm {
  class RandomNumberGenerator;
}

namespace {

class MachineBlockShuffling : public MachineFunctionPass {
public:
    static char ID; // Pass identification, replacement for typeid

  MachineBlockShuffling() : MachineFunctionPass(ID) {
    initializeMachineBlockShufflingPass(*PassRegistry::getPassRegistry());
  }

  bool runOnMachineFunction(MachineFunction &F) override;

private:
  std::unique_ptr<RandomNumberGenerator> RNG;
};

}

char MachineBlockShuffling::ID = 0;

char &llvm::MachineBlockShufflingID = MachineBlockShuffling::ID;

INITIALIZE_PASS_BEGIN(MachineBlockShuffling, DEBUG_TYPE,
                      "Shuffle basic blocks", false, false)
INITIALIZE_PASS_END(MachineBlockShuffling, DEBUG_TYPE,
                    "Shuffle basic blocks", false, false)


template<typename Iter, typename RandomGenerator>
static Iter select_randomly(Iter start, Iter end, RandomGenerator& g) {
    std::uniform_int_distribution<> dis(0, std::distance(start, end) - 1);
    std::advance(start, dis(g));
    return start;
}

bool MachineBlockShuffling::runOnMachineFunction(MachineFunction &MF) {
  if (skipFunction(MF.getFunction()))
    return false;

  // Check for single-block functions and skip them.
  if (std::next(MF.begin()) == MF.end())
    return false;

    if (!ForceBlockShuffling && !isFuzzed(fuzz::BPU, NumBlockShufflingEntry) && !isFuzzed(fuzz::L1I, NumBlockShufflingEntry)) {
      return false;
    }

    NumBlockShufflingEntry--; // due to double-increment in isFuzzed checks

    if (!RNG) {
      RNG = std::move(MF.getFunction().getParent()->createRNG("MBB_shuffling"));
    }


    std::vector<MachineBasicBlock*> blocks{};
    blocks.reserve(MF.size());

    for (MachineBasicBlock &bb : MF) {
      blocks.push_back(&bb);
    }

    std::shuffle(blocks.begin() + 1, blocks.end(), *RNG);

    for (size_t i = 0; i < blocks.size(); i++) {
      if (blocks[i] != MF.getBlockNumbered(i)) {
        NumBasicBlocksShuffled++;
      }
    }

    DenseMap<const MachineBasicBlock*, size_t> newIndices;
    for (const MachineBasicBlock *MBB : blocks) {
      newIndices[MBB] = newIndices.size();
    }

    MF.sort([&](MachineBasicBlock &L, MachineBasicBlock &R) {
      return newIndices[&L] < newIndices[&R];
    });

    return true;

}