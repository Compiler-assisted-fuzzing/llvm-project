//===----------------------------------------------------------------------===//
///
/// \file
/// This pass extracts global data into a unique separate sections (if no other
/// section is provided already). Section names are: .globalVariable.fuzz1,
/// .globalVariable.fuzz2, etc.
///
//===----------------------------------------------------------------------===//

#include "llvm/Transforms/IPO/GlobalDataExtraction.h"
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

#define DEBUG_TYPE "globaldataextraction"

using namespace llvm;

bool GlobalDataExtraction::run(Module &M) {
  bool Changed = false;

  unsigned long SectionsCount = 1;
  for (auto &G : M.globals()) {
    if (!G.getSection().empty()) // Don't modify unique sections
      continue;
    if (G.getLinkage() != GlobalValue::LinkageTypes::ExternalLinkage)
      continue;
    G.setSection(".globalVariable.fuzz" + std::to_string(SectionsCount));
    SectionsCount++;
    Changed = true;
  }

  return Changed;
}

PreservedAnalyses
GlobalDataExtractionPass::run(Module &M, ModuleAnalysisManager &AM) {
  if (GlobalDataExtraction().run(M))
    return PreservedAnalyses::none();
  return PreservedAnalyses::all();
}
