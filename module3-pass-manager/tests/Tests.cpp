//===- Tests.cpp - Module 3 unit tests ----------------------------------===//

#include "ASTPrinter.h"
#include "AllocCounter.h"
#include "Casting.h"
#include "Pass.h"
#include "Passes.h"
#include "Testing.h"

#include <memory>
#include <sstream>
#include <string>
#include <utility>

using namespace workbench;

namespace {

ExprPtr Number(double V) { return std::make_unique<NumberExprAST>(V); }

ExprPtr Binary(char Op, ExprPtr Lhs, ExprPtr Rhs) {
  return std::make_unique<BinaryExprAST>(Op, std::move(Lhs), std::move(Rhs));
}

double NumberValue(const ExprAST *E) {
  return cast<NumberExprAST>(E)->GetValue();
}

void TestFoldsNestedConstants() {
  std::printf("ConstantFolding: nested constants\n");

  // (1 + 2) * 4  ->  12, in a single post-order traversal.
  ExprPtr Root = Binary('*', Binary('+', Number(1), Number(2)), Number(4));
  CHECK(CountNodes(Root.get()) == 5);

  ConstantFoldingPass Folding;
  CHECK(Folding.Run(Root));
  CHECK(isa<NumberExprAST>(Root.get()));
  CHECK(NumberValue(Root.get()) == 12.0);
  CHECK(CountNodes(Root.get()) == 1);
  CHECK(Folding.GetNumFolded() == 2); // the inner + and the outer *
}

void TestReplacesTheRootThroughTheCallersPointer() {
  std::printf("ConstantFolding: root replacement\n");

  ExprPtr Root = Binary('+', Number(1), Number(2));
  CHECK(isa<BinaryExprAST>(Root.get()));

  ConstantFoldingPass Folding;
  CHECK(Folding.Run(Root));

  // The caller's unique_ptr now owns a different node entirely. This is the
  // reason Pass::Run takes ExprPtr & rather than ExprAST *.
  CHECK(isa<NumberExprAST>(Root.get()));
  CHECK(NumberValue(Root.get()) == 3.0);
}

void TestIdentityMovesTheSubtreeInsteadOfCopying() {
  std::printf("ConstantFolding: identity hoists by move\n");

  ExprPtr Bar = std::make_unique<CallExprAST>("bar");
  const ExprAST *BarAddress = Bar.get();

  ExprPtr Root = Binary('+', std::move(Bar), Number(0)); // bar + 0
  CHECK(Bar == nullptr);                                 // ownership moved in

  ConstantFoldingPass Folding;
  CHECK(Folding.Run(Root));

  // Same node, same address: the subtree was transferred out of the parent
  // before the parent was destroyed. If it had been copied, the address would
  // differ; if it had been left owned by the parent, this would be a
  // use-after-free.
  CHECK(Root.get() == BarAddress);
  CHECK(isa<CallExprAST>(Root.get()));
  CHECK(Folding.GetNumSimplified() == 1);
  CHECK(Folding.GetNumFolded() == 0);
}

void TestAllIdentityForms() {
  std::printf("ConstantFolding: identity forms\n");

  struct Case {
    char Op;
    double Lhs;
    bool LhsIsCall; // true: `bar <op> Rhs`, false: `Lhs <op> bar`
    double Rhs;
  };

  // bar + 0, 0 + bar, bar - 0, bar * 1, 1 * bar all reduce to bar.
  const Case Cases[] = {
      {'+', 0.0, true, 0.0}, {'+', 0.0, false, 0.0}, {'-', 0.0, true, 0.0},
      {'*', 1.0, true, 1.0}, {'*', 1.0, false, 1.0},
  };

  for (const Case &C : Cases) {
    ExprPtr Root = C.LhsIsCall
                       ? Binary(C.Op, std::make_unique<CallExprAST>("bar"),
                                Number(C.Rhs))
                       : Binary(C.Op, Number(C.Lhs),
                                std::make_unique<CallExprAST>("bar"));
    ConstantFoldingPass Folding;
    Folding.Run(Root);
    CHECK(isa<CallExprAST>(Root.get()));
  }
}

void TestLeavesNonConstantsAlone() {
  std::printf("ConstantFolding: leaves non-constants alone\n");

  // 3 * bar has no constant form and no applicable identity.
  ExprPtr Root =
      Binary('*', Number(3), std::make_unique<CallExprAST>("bar"));
  ConstantFoldingPass Folding;
  CHECK(!Folding.Run(Root));
  CHECK(isa<BinaryExprAST>(Root.get()));
  CHECK(CountNodes(Root.get()) == 3);

  // x * 0 is deliberately not folded: dropping the subtree would drop the
  // side effects of any call inside it.
  ExprPtr Annihilate =
      Binary('*', std::make_unique<CallExprAST>("bar"), Number(0));
  ConstantFoldingPass Folding2;
  CHECK(!Folding2.Run(Annihilate));
  CHECK(isa<BinaryExprAST>(Annihilate.get()));

  // An unsupported operator is left untouched rather than guessed at.
  ExprPtr Unknown = Binary('%', Number(4), Number(2));
  ConstantFoldingPass Folding3;
  CHECK(!Folding3.Run(Unknown));
  CHECK(isa<BinaryExprAST>(Unknown.get()));
}

ExprPtr BuildBlockWithDeadTail() {
  auto Block = std::make_unique<BlockAST>();
  Block->AddStatement(std::make_unique<CallExprAST>("foo"));
  Block->AddStatement(std::make_unique<ReturnExprAST>(Number(1)));
  Block->AddStatement(std::make_unique<CallExprAST>("baz"));
  Block->AddStatement(std::make_unique<ReturnExprAST>(Number(2)));
  return Block;
}

void TestDeadCodeAfterReturn() {
  std::printf("DeadCodeElimination: trailing statements\n");

  ExprPtr Root = BuildBlockWithDeadTail();
  CHECK(cast<BlockAST>(Root.get())->GetStatements().size() == 4);

  DeadCodeEliminationPass DCE;
  CHECK(DCE.Run(Root));
  CHECK(DCE.GetNumRemoved() == 2);

  const auto *Block = cast<BlockAST>(Root.get());
  CHECK(Block->GetStatements().size() == 2);
  CHECK(isa<CallExprAST>(Block->GetStatements()[0].get()));
  CHECK(isa<ReturnExprAST>(Block->GetStatements()[1].get()));

  // Idempotent: a second run has nothing left to do.
  DeadCodeEliminationPass Again;
  CHECK(!Again.Run(Root));
  CHECK(Again.GetNumRemoved() == 0);
}

void TestDeadCodeInNestedBlock() {
  std::printf("DeadCodeElimination: nested blocks\n");

  auto Inner = std::make_unique<BlockAST>();
  Inner->AddStatement(std::make_unique<ReturnExprAST>(Number(1)));
  Inner->AddStatement(std::make_unique<CallExprAST>("dead"));

  auto Outer = std::make_unique<BlockAST>();
  Outer->AddStatement(std::move(Inner));
  Outer->AddStatement(std::make_unique<CallExprAST>("live"));

  ExprPtr Root = std::move(Outer);
  DeadCodeEliminationPass DCE;
  CHECK(DCE.Run(Root));
  CHECK(DCE.GetNumRemoved() == 1);

  const auto *OuterBlock = cast<BlockAST>(Root.get());
  CHECK(OuterBlock->GetStatements().size() == 2); // outer is untouched
  const auto *InnerBlock =
      cast<BlockAST>(OuterBlock->GetStatements()[0].get());
  CHECK(InnerBlock->GetStatements().size() == 1);
}

void TestNothingRemovedWhenReturnIsLast() {
  std::printf("DeadCodeElimination: return already last\n");

  auto Block = std::make_unique<BlockAST>();
  Block->AddStatement(std::make_unique<CallExprAST>("foo"));
  Block->AddStatement(std::make_unique<ReturnExprAST>(Number(1)));

  ExprPtr Root = std::move(Block);
  DeadCodeEliminationPass DCE;
  CHECK(!DCE.Run(Root));
  CHECK(cast<BlockAST>(Root.get())->GetStatements().size() == 2);
}

void TestPipelineOrderAndReporting() {
  std::printf("PassManager: pipeline\n");

  std::ostringstream Log;
  PassManager PM(&Log);
  PM.AddPass(CreateConstantFoldingPass());
  PM.AddPass(CreateDeadCodeEliminationPass());
  CHECK(PM.GetNumPasses() == 2);

  auto Block = std::make_unique<BlockAST>();
  Block->AddStatement(std::make_unique<ReturnExprAST>(
      Binary('+', Binary('*', Number(2), Number(5)),
             Binary('-', Number(10), Number(4)))));
  Block->AddStatement(std::make_unique<CallExprAST>("dead"));

  ExprPtr Root = std::move(Block);
  CHECK(PM.Run(Root));

  const auto *Result = cast<BlockAST>(Root.get());
  CHECK(Result->GetStatements().size() == 1);
  const auto *Ret = cast<ReturnExprAST>(Result->GetStatements()[0].get());
  CHECK(NumberValue(Ret->GetValue()) == 16.0); // (2*5) + (10-4)

  const std::string Text = Log.str();
  CHECK(Text.find("constant-folding: changed") != std::string::npos);
  CHECK(Text.find("dead-code-elimination: changed") != std::string::npos);
  // Pass order is preserved.
  CHECK(Text.find("constant-folding") < Text.find("dead-code-elimination"));
}

void TestMutationLeavesNoLeak() {
  std::printf("Ownership: allocation balance across mutation\n");

  const AllocStats Before = GetAllocStats();
  {
    ExprPtr Root = BuildBlockWithDeadTail();
    cast<BlockAST>(Root.get())
        ->GetStatementSlots()
        .push_back(std::make_unique<ReturnExprAST>(
            Binary('+', Number(20), Number(22))));

    PassManager PM;
    PM.AddPass(CreateConstantFoldingPass());
    PM.AddPass(CreateDeadCodeEliminationPass());
    PM.Run(Root);
  }
  const AllocStats After = GetAllocStats();

  // Folding destroys nodes and DCE destroys subtrees; every one of those
  // frees must be accounted for exactly once.
  CHECK(After.Allocations - Before.Allocations ==
        After.Deallocations - Before.Deallocations);
}

} // namespace

int main() {
  TestFoldsNestedConstants();
  TestReplacesTheRootThroughTheCallersPointer();
  TestIdentityMovesTheSubtreeInsteadOfCopying();
  TestAllIdentityForms();
  TestLeavesNonConstantsAlone();
  TestDeadCodeAfterReturn();
  TestDeadCodeInNestedBlock();
  TestNothingRemovedWhenReturnIsLast();
  TestPipelineOrderAndReporting();
  TestMutationLeavesNoLeak();
  return testing::Summary("module3-pass-manager");
}
