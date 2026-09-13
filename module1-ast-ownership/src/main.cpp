//===- main.cpp - Module 1 demo: foo(1 + 2, 3 * bar) --------------------===//
//
// Builds an AST by hand and shows:
//   1. the tree is owned by exactly one unique_ptr chain,
//   2. the 2-element argument list costs zero heap allocations,
//   3. what it costs when a list outgrows its inline capacity.
//
//===---------------------------------------------------------------------===//

#include "AST.h"
#include "AllocCounter.h"

#include <iostream>
#include <memory>
#include <utility>

using namespace workbench;

namespace {

/// `1 + 2`
ExprPtr MakeOnePlusTwo() {
  return std::make_unique<BinaryExprAST>(
      '+', std::make_unique<NumberExprAST>(1.0),
      std::make_unique<NumberExprAST>(2.0));
}

/// `3 * bar`  (`bar` is modelled as a zero-argument call)
ExprPtr MakeThreeTimesBar() {
  return std::make_unique<BinaryExprAST>(
      '*', std::make_unique<NumberExprAST>(3.0),
      std::make_unique<CallExprAST>("bar"));
}

void DemoZeroAllocationArgumentList() {
  std::cout << "== foo(1 + 2, 3 * bar) ==\n\n";

  // Build the subtrees *first* so the measured region below contains nothing
  // but the argument-list bookkeeping.
  ExprPtr Arg0 = MakeOnePlusTwo();
  ExprPtr Arg1 = MakeThreeTimesBar();

  ExprList Args;
  {
    AllocScope Scope(std::cout, "2-element argument list (inline capacity 4)");
    Args.push_back(std::move(Arg0));
    Args.push_back(std::move(Arg1));
  }
  std::cout << "  storage is " << (Args.IsSmall() ? "inline" : "heap")
            << ", size " << Args.size() << ", capacity " << Args.capacity()
            << "\n\n";

  // One unique_ptr owns the entire tree from here on.
  ExprPtr Root = std::make_unique<CallExprAST>("foo", std::move(Args));
  Root->Print(std::cout);

  std::cout << "\n  sizeof(CallExprAST) = " << sizeof(CallExprAST)
            << " bytes (argument storage included)\n";
  std::cout << "  sizeof(ExprList)    = " << sizeof(ExprList)
            << " bytes (3 words + 4 inline unique_ptrs)\n\n";

  // Root goes out of scope here: one destructor call tears down six nodes,
  // in reverse construction order, with no explicit delete anywhere.
}

void DemoSpillToHeap() {
  std::cout << "== outgrowing the inline capacity ==\n\n";

  ExprList Args;
  {
    AllocScope Scope(std::cout, "pushing 4 elements (fits inline)");
    for (int I = 0; I != 4; ++I)
      Args.push_back(std::make_unique<NumberExprAST>(I));
  }
  std::cout << "  storage is " << (Args.IsSmall() ? "inline" : "heap")
            << " (4 allocations above are the nodes themselves)\n";

  {
    AllocScope Scope(std::cout, "pushing the 5th element (spills)");
    Args.push_back(std::make_unique<NumberExprAST>(4));
  }
  std::cout << "  storage is " << (Args.IsSmall() ? "inline" : "heap")
            << ", capacity grew to " << Args.capacity()
            << " (1 node + 1 buffer = 2 allocations above)\n\n";
}

} // namespace

int main() {
  DemoZeroAllocationArgumentList();
  DemoSpillToHeap();

  const AllocStats Stats = GetAllocStats();
  std::cout << "== process totals ==\n\n";
  std::cout << "  allocations:   " << Stats.Allocations << "\n";
  std::cout << "  deallocations: " << Stats.Deallocations << "\n";
  std::cout << "  bytes:         " << Stats.BytesAllocated << "\n";
  return 0;
}
