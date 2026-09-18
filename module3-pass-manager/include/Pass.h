//===- Pass.h - Minimal pass + pass manager ------------------------------===//
//
// The signature that matters:
//
//     virtual bool Run(ExprPtr &Root) = 0;
//                      ^^^^^^^^^^^^
//
// A pass takes the root by *reference to the owning unique_ptr*, not by
// pointer. Folding `1 + 2` at the root replaces the root node itself, and a
// pass that only received `ExprAST *` would have no way to publish that
// replacement to its caller. LLVM's function passes get away with
// `Function &` because LLVM transformations mutate instructions in place and
// never replace the container; an AST pass genuinely does replace nodes, so
// the owning edge has to be part of the interface.
//
// `bool` return = "did I change anything", the same contract as
// llvm::FunctionPass::runOnFunction. The pass manager uses it to report what
// happened and, in a real pipeline, to decide whether cached analyses are
// still valid.
//
//===---------------------------------------------------------------------===//

#ifndef WORKBENCH_PASS_H
#define WORKBENCH_PASS_H

#include "AST.h"

#include <memory>
#include <ostream>
#include <vector>

namespace workbench {

/// Base class for AST transformations.
class Pass {
public:
  virtual ~Pass() = default;

  /// Short identifier, as it would appear on a command line.
  virtual const char *GetName() const = 0;

  /// Transform the tree rooted at \p Root. Returns true if anything changed.
  virtual bool Run(ExprPtr &Root) = 0;

  /// One-line summary of what this pass did on its last run, for the log.
  virtual void PrintSummary(std::ostream &OS) const = 0;

protected:
  Pass() = default;
  Pass(const Pass &) = delete;
  Pass &operator=(const Pass &) = delete;
};

/// Runs a fixed sequence of passes over a tree.
///
/// Owns its passes (`unique_ptr`, like everything else here). Real pass
/// managers add analysis caching, invalidation, and iteration-until-fixpoint;
/// the ownership and "replace through the owning edge" story is the part worth
/// showing at this size.
class PassManager {
public:
  /// \p Log is optional; pass nullptr to run silently.
  explicit PassManager(std::ostream *Log = nullptr) : Log(Log) {}

  void AddPass(std::unique_ptr<Pass> P);

  /// Runs every registered pass in order. Returns true if any pass reported a
  /// change.
  bool Run(ExprPtr &Root);

  size_t GetNumPasses() const { return Passes.size(); }

private:
  // std::vector, not SmallVector: a pass pipeline is built once at startup
  // and is not on any hot path, so there is nothing to optimize here. Reaching
  // for SmallVector everywhere is how you end up with bloated objects for no
  // measured benefit.
  std::vector<std::unique_ptr<Pass>> Passes;
  std::ostream *Log;
};

} // namespace workbench

#endif // WORKBENCH_PASS_H
