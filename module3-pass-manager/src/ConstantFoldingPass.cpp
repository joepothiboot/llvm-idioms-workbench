//===- ConstantFoldingPass.cpp - In-place node replacement --------------===//
//
// This file is where the move-semantics story lives. Read TryFoldBinary().
//
//===---------------------------------------------------------------------===//

#include "Casting.h"
#include "Passes.h"

#include <memory>
#include <utility>

namespace workbench {

namespace {

/// Evaluate \p Op on two constants. Returns false for an operator we do not
/// know how to fold (no exception, no abort — folding is best-effort).
bool ApplyOperator(char Op, double Lhs, double Rhs, double &Out) {
  switch (Op) {
  case '+':
    Out = Lhs + Rhs;
    return true;
  case '-':
    Out = Lhs - Rhs;
    return true;
  case '*':
    Out = Lhs * Rhs;
    return true;
  default:
    return false;
  }
}

/// Is \p E the literal \p Value?
bool IsLiteral(const ExprAST *E, double Value) {
  const auto *Num = dyn_cast_if_present<NumberExprAST>(E);
  return Num && Num->GetValue() == Value;
}

} // namespace

bool ConstantFoldingPass::Run(ExprPtr &Root) {
  NumFolded = 0;
  NumSimplified = 0;
  return Visit(Root);
}

void ConstantFoldingPass::PrintSummary(std::ostream &OS) const {
  OS << NumFolded << " node(s) folded, " << NumSimplified
     << " identity(ies) simplified";
}

/// Post-order walk. Children are visited first so that `(1 + 2) * 4` has
/// already become `3 * 4` by the time this node is considered, and the whole
/// tree collapses in one traversal instead of needing a fixpoint loop.
bool ConstantFoldingPass::Visit(ExprPtr &Slot) {
  if (!Slot)
    return false;

  bool Changed = false;

  if (auto *Bin = dyn_cast<BinaryExprAST>(Slot.get())) {
    Changed |= Visit(Bin->GetLhsSlot());
    Changed |= Visit(Bin->GetRhsSlot());
    // Bin is still valid: visiting a child can replace the child, never the
    // parent.
    Changed |= TryFoldBinary(Slot);
    return Changed;
  }

  if (auto *Call = dyn_cast<CallExprAST>(Slot.get())) {
    for (ExprPtr &Arg : Call->GetArgSlots())
      Changed |= Visit(Arg);
    return Changed;
  }

  if (auto *Block = dyn_cast<BlockAST>(Slot.get())) {
    for (ExprPtr &Stmt : Block->GetStatementSlots())
      Changed |= Visit(Stmt);
    return Changed;
  }

  if (auto *Ret = dyn_cast<ReturnExprAST>(Slot.get()))
    return Visit(Ret->GetValueSlot());

  return false; // NumberExprAST: nothing to do.
}

/// Replace `*Slot` with something cheaper, if possible.
///
/// \p Slot is the unique_ptr that *owns* the binary node. Writing through it
/// is what makes the replacement visible to the parent, and assigning to it is
/// what destroys the old subtree — exactly once.
bool ConstantFoldingPass::TryFoldBinary(ExprPtr &Slot) {
  auto *Bin = cast<BinaryExprAST>(Slot.get());
  const auto *Lhs = dyn_cast_if_present<NumberExprAST>(Bin->GetLhs());
  const auto *Rhs = dyn_cast_if_present<NumberExprAST>(Bin->GetRhs());

  // --- Case 1: both operands constant -> evaluate. --------------------------
  if (Lhs && Rhs) {
    double Value = 0.0;
    if (!ApplyOperator(Bin->GetOp(), Lhs->GetValue(), Rhs->GetValue(), Value))
      return false;

    // Build the replacement BEFORE touching Slot. If construction is the last
    // thing that can fail, the tree is never left in a half-updated state.
    ExprPtr Folded = std::make_unique<NumberExprAST>(Value);

    // unique_ptr::operator=(unique_ptr&&) releases the source first, then
    // deletes the old pointee. So this single line:
    //   * detaches the new Number from `Folded`,
    //   * destroys the BinaryExprAST,
    //   * which destroys its two child Numbers,
    //   * and installs the Number in the parent's edge.
    // No raw delete, no window where two owners exist, no window where the
    // edge points at freed memory.
    Slot = std::move(Folded);

    // `Bin`, `Lhs` and `Rhs` are DANGLING from here on. Reading any of them
    // is use-after-free. This is why the replacement is the last statement
    // that mentions them.
    ++NumFolded;
    return true;
  }

  // --- Case 2: algebraic identity -> hoist the surviving subtree. ----------
  // `x + 0`, `0 + x`, `x - 0`, `x * 1`, `1 * x` all reduce to `x`, where x is
  // an arbitrary subtree that must SURVIVE the destruction of its parent.
  ExprPtr Hoisted;
  switch (Bin->GetOp()) {
  case '+':
    if (IsLiteral(Bin->GetRhs(), 0.0))
      Hoisted = std::move(Bin->GetLhsSlot());
    else if (IsLiteral(Bin->GetLhs(), 0.0))
      Hoisted = std::move(Bin->GetRhsSlot());
    break;
  case '-':
    if (IsLiteral(Bin->GetRhs(), 0.0))
      Hoisted = std::move(Bin->GetLhsSlot());
    break;
  case '*':
    if (IsLiteral(Bin->GetRhs(), 1.0))
      Hoisted = std::move(Bin->GetLhsSlot());
    else if (IsLiteral(Bin->GetLhs(), 1.0))
      Hoisted = std::move(Bin->GetRhsSlot());
    break;
  default:
    break;
  }

  if (Hoisted) {
    // `Hoisted` owns the surviving subtree and the parent's edge to it is now
    // null, so the parent's destructor cannot free it. Ownership was
    // *transferred*, not shared — which is precisely what stops this from
    // being a double free.
    //
    // Writing `Slot = std::move(Bin->GetLhsSlot())` directly would be
    // correct in this implementation (the source is released before the old
    // pointee is deleted), but it depends on the caller knowing the internal
    // ordering of unique_ptr's move-assignment. The two-step version says
    // what it means and stays correct if the expression is ever reordered.
    Slot = std::move(Hoisted);
    ++NumSimplified;
    return true;
  }

  return false;
}

std::unique_ptr<Pass> CreateConstantFoldingPass() {
  return std::make_unique<ConstantFoldingPass>();
}

} // namespace workbench
