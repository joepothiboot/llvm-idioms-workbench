//===- PassManager.cpp ----------------------------------------------------===//

#include "Pass.h"

#include <cassert>
#include <utility>

namespace workbench {

void PassManager::AddPass(std::unique_ptr<Pass> P) {
  assert(P && "cannot register a null pass");
  Passes.push_back(std::move(P));
}

bool PassManager::Run(ExprPtr &Root) {
  assert(Root && "cannot run a pipeline on an empty tree");

  bool AnyChange = false;
  for (const std::unique_ptr<Pass> &P : Passes) {
    // Root is handed down by reference the whole way, so a pass that replaces
    // the root node updates the caller's unique_ptr directly.
    const bool Changed = P->Run(Root);
    AnyChange |= Changed;

    if (Log) {
      *Log << "  " << P->GetName() << ": " << (Changed ? "changed" : "no change")
           << " - ";
      P->PrintSummary(*Log);
      *Log << "\n";
    }
  }
  return AnyChange;
}

} // namespace workbench
