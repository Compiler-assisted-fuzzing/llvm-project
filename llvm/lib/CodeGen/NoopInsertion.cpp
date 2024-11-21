//===- NoopInsertion.cpp - Noop Insertion ---------------------------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This pass adds fine-grained diversity by displacing code using randomly
// placed (optionally target supplied) Noop instructions.
//
//===----------------------------------------------------------------------===//

#include "llvm/CodeGen/NoopInsertion.h"
#include "llvm/ADT/Statistic.h"
#include "llvm/CodeGen/MachineInstrBuilder.h"
#include "llvm/CodeGen/MachineModuleInfo.h"
#include "llvm/CodeGen/MachineRegisterInfo.h"
#include "llvm/CodeGen/Passes.h"
#include "llvm/CodeGen/TargetInstrInfo.h"
#include "llvm/IR/BasicBlock.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/Module.h"
#include "llvm/InitializePasses.h"
#include "llvm/Support/Allocator.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/RandomNumberGenerator.h"

#include <random>

using namespace llvm;

#define DEBUG_TYPE "noop-insertion"

static cl::opt<unsigned> NoopInsertionPercentage(
    "noop-insertion-percentage",
    cl::desc("Percentage of instructions that have Noops prepended"),
    cl::init(25)); // Default is a good balance between entropy and
                   // performance impact

static cl::opt<unsigned> MaxNoopsPerInstruction(
    "max-noops-per-instruction",
    llvm::cl::desc("Maximum number of Noops per instruction"),
    llvm::cl::init(1));

STATISTIC(InsertedNoops,
          "Total number of noop type instructions inserted for diversity");

namespace {

class NoopInsertion : public MachineFunctionPass {
public:
  static char ID;

  NoopInsertion();

  bool runOnMachineFunction(MachineFunction &MF) override;

  void getAnalysisUsage(AnalysisUsage &AU) const override;

private:
  std::unique_ptr<RandomNumberGenerator> RNG;

  // Uniform real distribution from 0 to 100
  std::uniform_real_distribution<double> Distribution =
      std::uniform_real_distribution<double>(0, 100);
};

NoopInsertion::NoopInsertion()
  : MachineFunctionPass(ID) {
  initializeNoopInsertionPass(*PassRegistry::getPassRegistry());
  // clamp percentage to 100
  if (NoopInsertionPercentage > 100) {
    NoopInsertionPercentage = 100;
  }
}

void NoopInsertion::getAnalysisUsage(AnalysisUsage &AU) const {
  AU.setPreservesCFG();
  MachineFunctionPass::getAnalysisUsage(AU);
}

bool NoopInsertion::runOnMachineFunction(MachineFunction &Fn) {
  // The RNG must be initialized on first use so we have a Module to
  // construct it from
  if (!RNG) {
    RNG = std::move(Fn.getFunction().getParent()->createRNG("NoopInsertion"));
  }
  const TargetInstrInfo *TII = Fn.getSubtarget().getInstrInfo();
  unsigned FnInsertedNoopCount = 0;
  for (auto &BB : Fn) {
    MachineBasicBlock::iterator FirstTerm = BB.getFirstTerminator();
    for (MachineBasicBlock::iterator I = BB.begin(), E = BB.end(); I != E; ++I) {
      if (I->isPseudo()) {
        continue;
      }
      // Insert random number of Noop-like instructions.
      for (unsigned i = 0; i < MaxNoopsPerInstruction; ++i) {
        if (Distribution(*RNG) >= NoopInsertionPercentage) {
          continue;
        }
        TII->insertNoop(BB, I, *RNG);
        ++FnInsertedNoopCount;
      }
      if (I == FirstTerm) {
        break;
      }
    }
  }
  InsertedNoops += FnInsertedNoopCount;
  return FnInsertedNoopCount > 0;
}

}

PreservedAnalyses NoopInsertionPass::run(MachineFunction &MF, MachineFunctionAnalysisManager &) {
  if (!NoopInsertion().runOnMachineFunction(MF))
    return PreservedAnalyses::all();
  PreservedAnalyses PA = getMachineFunctionPassPreservedAnalyses();
  PA.preserveSet<CFGAnalyses>();
  return PA;
}

char NoopInsertion::ID = 0;
char &llvm::NoopInsertionID = NoopInsertion::ID;
INITIALIZE_PASS(NoopInsertion, "noop-insertion",
                "Noop Insertion for fine-grained code randomization", false,
                false)
