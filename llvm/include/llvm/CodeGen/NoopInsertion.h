//===-- NoopInsertion.h - Noop Insertion ------------------------*- C++ -*-===//
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

#ifndef LLVM_CODEGEN_NOOPINSERTION_H
#define LLVM_CODEGEN_NOOPINSERTION_H

#include "llvm/CodeGen/MachinePassManager.h"

namespace llvm {

class NoopInsertionPass : public PassInfoMixin<NoopInsertionPass> {
public:
  PreservedAnalyses run(MachineFunction &MF, MachineFunctionAnalysisManager &MFAM);
};

}

#endif // LLVM_CODEGEN_NOOPINSERTION_H
