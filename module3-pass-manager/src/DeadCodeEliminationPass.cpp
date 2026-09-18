//===- DeadCodeEliminationPass.cpp - Drop code after a return -----------===//

#include "Casting.h"
#include "Passes.h"

#include <memory>

namespace workbench {

bool DeadCodeEliminationPass::Run(ExprPtr &Root) {
  NumRemoved = 0;
  return Visit(Root);
}

void DeadCodeEliminationPass::PrintSummary(std::ostream &OS) const {
  OS << NumRemoved << " unreachable statement(s) removed";
}

bool DeadCodeEliminationPass::Visit(ExprPtr &Slot) {
  if (!Slot)
    return false;

  if (auto *Block = dyn_cast<BlockAST>(Slot.get())) {
    ExprList &Stmts = Block->GetStatementSlots();
    bool Changed = false;

    for (size_t I = 0, E = Stmts.size(); I != E; ++I) {
      // Recurse first: a nested block can contain its own unreachable tail,
      // and it is still worth cleaning up even if this whole block turns out
      // to be dead itself.
      Changed |= Visit(Stmts[I]);

      if (!isa<ReturnExprAST>(Stmts[I].get()))
        continue;

      // Everything after a return in the same block is unreachable.
      const size_t Survivors = I + 1;
      if (Survivors == E)
        break; // the return is already the last statement

      NumRemoved += static_cast<unsigned>(E - Survivors);

      // truncate() destroys the dropped statements, and each of those is a
      // unique_ptr, so destroying it destroys its whole subtree. One call,
      // no leak, no dangling parent edge: the container no longer has a slot
      // pointing at the freed nodes.
      Stmts.truncate(Survivors);
      Changed = true;
      break;
    }
    return Changed;
  }

  // Keep walking: a return's operand, a call's arguments and a binary node's
  // operands can all contain nested blocks.
  if (auto *Ret = dyn_cast<ReturnExprAST>(Slot.get()))
    return Visit(Ret->GetValueSlot());

  if (auto *Call = dyn_cast<CallExprAST>(Slot.get())) {
    bool Changed = false;
    for (ExprPtr &Arg : Call->GetArgSlots())
      Changed |= Visit(Arg);
    return Changed;
  }

  if (auto *Bin = dyn_cast<BinaryExprAST>(Slot.get())) {
    bool Changed = Visit(Bin->GetLhsSlot());
    Changed |= Visit(Bin->GetRhsSlot());
    return Changed;
  }

  return false;
}

std::unique_ptr<Pass> CreateDeadCodeEliminationPass() {
  return std::make_unique<DeadCodeEliminationPass>();
}

} // namespace workbench
