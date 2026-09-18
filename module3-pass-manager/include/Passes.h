//===- Passes.h - The two concrete transformations -----------------------===//

#ifndef WORKBENCH_PASSES_H
#define WORKBENCH_PASSES_H

#include "Pass.h"

#include <memory>
#include <ostream>

namespace workbench {

/// Pass A: evaluate constant subexpressions, and apply the algebraic
/// identities that let a whole subtree be hoisted.
///
///   (1 + 2) * 4   ->   Number 12          [both operands constant]
///   bar + 0       ->   Call 'bar'         [identity: subtree hoisted]
///
/// Runs bottom-up (post-order), so nested constants collapse in a single
/// traversal.
///
/// Deliberately NOT implemented: `x * 0 -> 0`. That rewrite discards the
/// subtree `x`, and if `x` contains a call it discards its side effects too.
/// Doing it would need an effect analysis first. Knowing which "obvious"
/// algebraic identity is unsound is most of the job.
class ConstantFoldingPass : public Pass {
public:
  const char *GetName() const override { return "constant-folding"; }
  bool Run(ExprPtr &Root) override;
  void PrintSummary(std::ostream &OS) const override;

  unsigned GetNumFolded() const { return NumFolded; }
  unsigned GetNumSimplified() const { return NumSimplified; }

private:
  bool Visit(ExprPtr &Slot);
  bool TryFoldBinary(ExprPtr &Slot);

  unsigned NumFolded = 0;
  unsigned NumSimplified = 0;
};

/// Pass B: drop statements that follow a `return` in a block.
///
///   { foo(); return 1; baz(); return 2; }  ->  { foo(); return 1; }
class DeadCodeEliminationPass : public Pass {
public:
  const char *GetName() const override { return "dead-code-elimination"; }
  bool Run(ExprPtr &Root) override;
  void PrintSummary(std::ostream &OS) const override;

  unsigned GetNumRemoved() const { return NumRemoved; }

private:
  bool Visit(ExprPtr &Slot);

  unsigned NumRemoved = 0;
};

/// Convenience factories, mirroring LLVM's `createXPass()` convention: the
/// pipeline builder never needs the concrete class definition.
std::unique_ptr<Pass> CreateConstantFoldingPass();
std::unique_ptr<Pass> CreateDeadCodeEliminationPass();

} // namespace workbench

#endif // WORKBENCH_PASSES_H
