//===- main.cpp - Module 3 demo: a two-pass pipeline --------------------===//
//
// Input program:
//
//   {
//     foo(1 + 2, 3 * bar)
//     return (2 * 5) + (10 - 4)
//     baz(9)                        // unreachable
//     return 0                      // unreachable
//   }
//
// Also demonstrates the reason Pass::Run takes `ExprPtr &`: folding a root
// that is itself constant replaces the root node.
//
//===---------------------------------------------------------------------===//

#include "ASTPrinter.h"
#include "AllocCounter.h"
#include "Pass.h"
#include "Passes.h"

#include <cstdint>
#include <iostream>
#include <memory>
#include <utility>

using namespace workbench;

namespace {

ExprPtr Number(double V) { return std::make_unique<NumberExprAST>(V); }

ExprPtr Binary(char Op, ExprPtr Lhs, ExprPtr Rhs) {
  return std::make_unique<BinaryExprAST>(Op, std::move(Lhs), std::move(Rhs));
}

ExprPtr BuildProgram() {
  ExprList FooArgs;
  FooArgs.push_back(Binary('+', Number(1), Number(2)));
  FooArgs.push_back(Binary('*', Number(3), std::make_unique<CallExprAST>("bar")));

  ExprList BazArgs;
  BazArgs.push_back(Number(9));

  auto Block = std::make_unique<BlockAST>();
  Block->AddStatement(
      std::make_unique<CallExprAST>("foo", std::move(FooArgs)));
  Block->AddStatement(std::make_unique<ReturnExprAST>(
      Binary('+', Binary('*', Number(2), Number(5)),
             Binary('-', Number(10), Number(4)))));
  Block->AddStatement(std::make_unique<CallExprAST>("baz", std::move(BazArgs)));
  Block->AddStatement(std::make_unique<ReturnExprAST>(Number(0)));
  return Block;
}

void DemoPipeline() {
  std::cout << "== before ==\n\n";
  ExprPtr Root = BuildProgram();
  PrintTree(Root.get(), std::cout, 1);
  std::cout << "\n  " << CountNodes(Root.get()) << " nodes\n\n";

  std::cout << "== pipeline ==\n\n";
  PassManager PM(&std::cout);
  PM.AddPass(CreateConstantFoldingPass());
  PM.AddPass(CreateDeadCodeEliminationPass());
  const bool Changed = PM.Run(Root);
  std::cout << "  pipeline reported " << (Changed ? "changes" : "no changes")
            << "\n\n";

  std::cout << "== after ==\n\n";
  PrintTree(Root.get(), std::cout, 1);
  std::cout << "\n  " << CountNodes(Root.get()) << " nodes\n\n";
}

void DemoRootReplacement() {
  std::cout << "== why Run() takes unique_ptr<ExprAST> & ==\n\n";

  ExprPtr Root = Binary('+', Number(1), Number(2));
  // Recorded as an integer, not as a pointer: the node it refers to is about
  // to be freed, and even reading a dangling pointer's value is undefined.
  const auto BeforeAddress = reinterpret_cast<std::uintptr_t>(Root.get());

  ConstantFoldingPass Folding;
  Folding.Run(Root);

  std::cout << std::hex;
  std::cout << "  before: Binary '+' at 0x" << BeforeAddress << "\n";
  std::cout << std::dec;
  std::cout << "  after:  ";
  PrintTree(Root.get(), std::cout, 0);
  std::cout << "          at " << static_cast<const void *>(Root.get()) << "\n";
  std::cout << "  the root node itself was replaced, so the pass had to write\n"
               "  through the caller's owning pointer.\n\n";
}

void DemoIdentityHoistsTheSameNode() {
  std::cout << "== identity simplification moves, it does not copy ==\n\n";

  ExprPtr Bar = std::make_unique<CallExprAST>("bar");
  const ExprAST *BarAddress = Bar.get();

  // bar + 0
  ExprPtr Root = Binary('+', std::move(Bar), Number(0));

  ConstantFoldingPass Folding;
  Folding.Run(Root);

  std::cout << "  'bar' node before: " << static_cast<const void *>(BarAddress)
            << "\n";
  std::cout << "  root node after:   " << static_cast<const void *>(Root.get())
            << "\n";
  std::cout << "  same node: " << std::boolalpha
            << (Root.get() == BarAddress)
            << " - the subtree survived its parent's destruction because\n"
               "  ownership was transferred out of the parent first.\n\n";
}

void DemoNoLeaksNoDoubleFrees() {
  std::cout << "== allocation balance across mutation ==\n\n";

  const AllocStats Before = GetAllocStats();
  {
    ExprPtr Root = BuildProgram();
    PassManager PM;
    PM.AddPass(CreateConstantFoldingPass());
    PM.AddPass(CreateDeadCodeEliminationPass());
    PM.Run(Root);
  }
  const AllocStats After = GetAllocStats();

  const unsigned long Allocated = After.Allocations - Before.Allocations;
  const unsigned long Freed = After.Deallocations - Before.Deallocations;
  std::cout << "  " << Allocated << " allocations, " << Freed << " frees\n";
  std::cout << "  balanced: " << std::boolalpha << (Allocated == Freed)
            << " (a leak would show up here; a double free would have "
               "crashed)\n\n";
}

} // namespace

int main() {
  DemoPipeline();
  DemoRootReplacement();
  DemoIdentityHoistsTheSameNode();
  DemoNoLeaksNoDoubleFrees();
  return 0;
}
