//===- Tests.cpp - Module 1 unit tests ----------------------------------===//

#include "AST.h"
#include "AllocCounter.h"
#include "SmallVector.h"
#include "Testing.h"

#include <memory>
#include <sstream>
#include <string>
#include <utility>

using namespace workbench;

namespace {

/// Counts construction/destruction/moves so the tests can observe what
/// SmallVector actually does to its elements.
struct Tracked {
  static int Live;
  static int Moves;

  int Value = 0;

  explicit Tracked(int Value) : Value(Value) { ++Live; }
  Tracked(Tracked &&RHS) noexcept : Value(RHS.Value) {
    ++Live;
    ++Moves;
  }
  Tracked &operator=(Tracked &&RHS) noexcept {
    Value = RHS.Value;
    ++Moves;
    return *this;
  }
  Tracked(const Tracked &) = delete;
  Tracked &operator=(const Tracked &) = delete;
  ~Tracked() { --Live; }

  static void Reset() {
    Live = 0;
    Moves = 0;
  }
};

int Tracked::Live = 0;
int Tracked::Moves = 0;

void TestInlineStorageDoesNotAllocate() {
  std::printf("SmallVector: inline storage\n");

  SmallVector<int, 4> V;
  CHECK(V.empty());
  CHECK(V.IsSmall());
  CHECK(V.capacity() == 4);

  const unsigned long Before = GetAllocStats().Allocations;
  for (int I = 0; I != 4; ++I)
    V.push_back(I);
  const unsigned long After = GetAllocStats().Allocations;

  CHECK(After - Before == 0); // the whole promise of SmallVector
  CHECK(V.IsSmall());
  CHECK(V.size() == 4);
  CHECK(V[0] == 0);
  CHECK(V[3] == 3);
}

void TestSpillToHeapPreservesElements() {
  std::printf("SmallVector: heap fallback\n");

  SmallVector<int, 4> V;
  for (int I = 0; I != 4; ++I)
    V.push_back(I);

  const unsigned long Before = GetAllocStats().Allocations;
  V.push_back(4);
  const unsigned long After = GetAllocStats().Allocations;

  CHECK(After - Before == 1); // exactly one buffer allocation
  CHECK(!V.IsSmall());
  CHECK(V.size() == 5);
  CHECK(V.capacity() >= 5);
  for (int I = 0; I != 5; ++I)
    CHECK(V[static_cast<size_t>(I)] == I);
}

void TestTruncateDestroysElements() {
  std::printf("SmallVector: truncate destroys\n");

  Tracked::Reset();
  {
    SmallVector<Tracked, 4> V;
    V.emplace_back(1);
    V.emplace_back(2);
    V.emplace_back(3);
    CHECK(Tracked::Live == 3);

    V.truncate(1);
    CHECK(Tracked::Live == 1);
    CHECK(V.size() == 1);
    CHECK(V[0].Value == 1);
  }
  CHECK(Tracked::Live == 0); // destructor cleaned up the rest
}

void TestMoveStealsHeapBufferWithoutMovingElements() {
  std::printf("SmallVector: move semantics\n");

  Tracked::Reset();
  {
    // Force a heap buffer, then move: the pointer is stolen, so no element
    // is touched.
    SmallVector<Tracked, 2> Heap;
    for (int I = 0; I != 4; ++I)
      Heap.emplace_back(I);
    const int MovesAfterGrowth = Tracked::Moves;

    SmallVector<Tracked, 2> Stolen = std::move(Heap);
    CHECK(Tracked::Moves == MovesAfterGrowth); // zero extra element moves
    CHECK(!Stolen.IsSmall());
    CHECK(Stolen.size() == 4);
    CHECK(Heap.empty()); // moved-from vector is usable and empty
    CHECK(Heap.IsSmall());
  }
  CHECK(Tracked::Live == 0);

  Tracked::Reset();
  {
    // Inline storage is part of the object, so it cannot be stolen: the
    // elements must be moved one by one.
    SmallVector<Tracked, 4> Inline;
    Inline.emplace_back(7);
    Inline.emplace_back(8);
    const int MovesBefore = Tracked::Moves;

    SmallVector<Tracked, 4> Moved = std::move(Inline);
    CHECK(Tracked::Moves == MovesBefore + 2);
    CHECK(Moved.IsSmall());
    CHECK(Moved[1].Value == 8);
    CHECK(Inline.empty());
  }
  CHECK(Tracked::Live == 0);
}

void TestTreeOwnershipIsBalanced() {
  std::printf("AST: unique_ptr ownership\n");

  const AllocStats Before = GetAllocStats();
  {
    ExprList Args;
    Args.push_back(std::make_unique<BinaryExprAST>(
        '+', std::make_unique<NumberExprAST>(1.0),
        std::make_unique<NumberExprAST>(2.0)));
    Args.push_back(std::make_unique<BinaryExprAST>(
        '*', std::make_unique<NumberExprAST>(3.0),
        std::make_unique<CallExprAST>("bar")));
    CHECK(Args.IsSmall());

    ExprPtr Root = std::make_unique<CallExprAST>("foo", std::move(Args));
    CHECK(Args.empty()); // ownership transferred out

    const CallExprAST *Call = static_cast<const CallExprAST *>(Root.get());
    CHECK(Call->GetArgs().size() == 2);
    CHECK(Call->ArgsAreInline());
  }
  const AllocStats After = GetAllocStats();

  // Every node allocated inside the scope was freed inside the scope.
  CHECK(After.Allocations - Before.Allocations ==
        After.Deallocations - Before.Deallocations);
}

void TestPrintedShape() {
  std::printf("AST: printed shape\n");

  ExprList Args;
  Args.push_back(std::make_unique<NumberExprAST>(1.0));
  ExprPtr Root = std::make_unique<CallExprAST>("foo", std::move(Args));

  std::ostringstream OS;
  Root->Print(OS);
  const std::string Text = OS.str();

  CHECK(Text.find("Call 'foo' (1 arg, storage inline)") != std::string::npos);
  CHECK(Text.find("  Number 1\n") != std::string::npos);
}

} // namespace

int main() {
  TestInlineStorageDoesNotAllocate();
  TestSpillToHeapPreservesElements();
  TestTruncateDestroysElements();
  TestMoveStealsHeapBufferWithoutMovingElements();
  TestTreeOwnershipIsBalanced();
  TestPrintedShape();
  return testing::Summary("module1-ast-ownership");
}
