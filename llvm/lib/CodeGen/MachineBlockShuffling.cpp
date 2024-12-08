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
#include "llvm/Target/TargetMachine.h"
#include "llvm/Transforms/Utils/CodeLayout.h"
#include <algorithm>
#include <cassert>
#include <cstdint>
#include <iterator>
#include <memory>
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


namespace llvm {


}

namespace {

class MachineBlockShuffling : public MachineFunctionPass {
public:
    static char ID; // Pass identification, replacement for typeid

  MachineBlockShuffling() : MachineFunctionPass(ID) {
    initializeMachineBlockShufflingPass(*PassRegistry::getPassRegistry());
  }

  bool runOnMachineFunction(MachineFunction &F) override;
};

}

char MachineBlockShuffling::ID = 0;

char &llvm::MachineBlockShufflingID = MachineBlockShuffling::ID;

INITIALIZE_PASS_BEGIN(MachineBlockShuffling, DEBUG_TYPE,
                      "Shuffle basic blocks", false, false)
INITIALIZE_PASS_END(MachineBlockShuffling, DEBUG_TYPE,
                    "Shuffle basic blocks", false, false)

bool MachineBlockShuffling::runOnMachineFunction(MachineFunction &MF) {
  if (skipFunction(MF.getFunction()))
    return false;

  // Check for single-block functions and skip them.
  if (std::next(MF.begin()) == MF.end())
    return false;

    if (!ForceBlockShuffling || isFuzzed(fuzz::BPU, NumBlockShufflingEntry)) {
        return false;
    }

    NumBlockShufflingEntry++;

    return true;

}