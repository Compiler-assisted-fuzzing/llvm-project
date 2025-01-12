//===----------------------------------------------------------------------===//
//
// This pass extracts global data into a separate unique sections.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_TRANSFORMS_IPO_GLOBALDATAEXTRACTION_H
#define LLVM_TRANSFORMS_IPO_GLOBALDATAEXTRACTION_H

#include "llvm/IR/PassManager.h"

namespace llvm {

class Module;

class GlobalDataExtraction {
public:
  GlobalDataExtraction() {}
  bool run(Module &M);
};

/// Pass to extract global data from functions.
class GlobalDataExtractionPass : public PassInfoMixin<GlobalDataExtractionPass> {
public:
  PreservedAnalyses run(Module &M, ModuleAnalysisManager &AM);
};

} // end namespace llvm

#endif // LLVM_TRANSFORMS_IPO_GLOBALDATAEXTRACTION_H

