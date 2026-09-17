//===- Tests.cpp - Module 2 unit tests ----------------------------------===//

#include "AST.h"
#include "Casting.h"
#include "Testing.h"
#include "Visitor.h"

#include <memory>
#include <sstream>
#include <string>
#include <type_traits>
#include <utility>

using namespace workbench;

namespace {

ExprPtr MakeTree() {
  ExprList Args;
  Args.push_back(std::make_unique<BinaryExprAST>(
      '+', std::make_unique<NumberExprAST>(1.0),
      std::make_unique<NumberExprAST>(2.0)));
  Args.push_back(std::make_unique<BinaryExprAST>(
      '*', std::make_unique<NumberExprAST>(3.0),
      std::make_unique<CallExprAST>("bar")));
  return std::make_unique<CallExprAST>("foo", std::move(Args));
}

void TestIsa() {
  std::printf("Casting: isa<>\n");

  ExprPtr Num = std::make_unique<NumberExprAST>(42.0);
  CHECK(isa<NumberExprAST>(Num.get()));
  CHECK(!isa<BinaryExprAST>(Num.get()));
  CHECK(!isa<CallExprAST>(Num.get()));
  CHECK(!isa<BlockAST>(Num.get()));

  // isa_and_present tolerates null; plain isa<> asserts on it.
  const ExprAST *Null = nullptr;
  CHECK(!isa_and_present<NumberExprAST>(Null));
}

void TestCastNeverReturnsNull() {
  std::printf("Casting: cast<>\n");

  ExprPtr Num = std::make_unique<NumberExprAST>(42.0);
  NumberExprAST *Exact = cast<NumberExprAST>(Num.get());
  CHECK(Exact != nullptr);
  CHECK(Exact->GetValue() == 42.0);

  // Casting to the base class is always valid and is a no-op.
  CHECK(cast<NumberExprAST>(Num.get()) == Num.get());

  // cast<BinaryExprAST>(Num.get()) would assert here. Not exercised: the
  // contract is "the caller guarantees the type", so a violation is a bug in
  // the caller, not a case to test around.
}

void TestDynCastReturnsNullOnMismatch() {
  std::printf("Casting: dyn_cast<>\n");

  ExprPtr Tree = MakeTree();

  CHECK(dyn_cast<CallExprAST>(Tree.get()) != nullptr);
  CHECK(dyn_cast<NumberExprAST>(Tree.get()) == nullptr);
  CHECK(dyn_cast<BinaryExprAST>(Tree.get()) == nullptr);

  const auto *Call = cast<CallExprAST>(Tree.get());
  const auto *Arg0 = dyn_cast<BinaryExprAST>(Call->GetArgs()[0].get());
  CHECK(Arg0 != nullptr);
  CHECK(Arg0->GetOp() == '+');
  CHECK(dyn_cast<NumberExprAST>(Arg0->GetLhs()) != nullptr);
  CHECK(dyn_cast<NumberExprAST>(Arg0->GetLhs())->GetValue() == 1.0);

  const ExprAST *Null = nullptr;
  CHECK(dyn_cast_if_present<NumberExprAST>(Null) == nullptr);
}

void TestConstPropagation() {
  std::printf("Casting: const propagation\n");

  ExprPtr Owned = std::make_unique<NumberExprAST>(1.0);
  ExprAST *Mutable = Owned.get();
  const ExprAST *Immutable = Owned.get();

  static_assert(std::is_same<decltype(dyn_cast<NumberExprAST>(Mutable)),
                             NumberExprAST *>::value,
                "mutable in, mutable out");
  static_assert(std::is_same<decltype(dyn_cast<NumberExprAST>(Immutable)),
                             const NumberExprAST *>::value,
                "const in, const out");
  static_assert(std::is_same<decltype(cast<NumberExprAST>(Immutable)),
                             const NumberExprAST *>::value,
                "cast<> propagates const too");

  CHECK(true); // the real assertions above are compile-time
}

void TestKindSwitch() {
  std::printf("Visitor: kind switch\n");

  ExprPtr Tree = MakeTree();
  CHECK(std::string(KindName(Tree.get())) == "NK_Call");

  const auto *Call = cast<CallExprAST>(Tree.get());
  CHECK(std::string(KindName(Call->GetArgs()[0].get())) == "NK_Binary");

  BlockAST Block;
  CHECK(std::string(KindName(&Block)) == "NK_Block");
}

void TestTraversalResults() {
  std::printf("Visitor: traversal\n");

  ExprPtr Tree = MakeTree();
  // foo, (1+2), 1, 2, (3*bar), 3, bar
  CHECK(CountNodes(Tree.get()) == 7);

  const auto *Call = cast<CallExprAST>(Tree.get());

  double Folded = 0.0;
  CHECK(TryEvaluateConstant(Call->GetArgs()[0].get(), Folded));
  CHECK(Folded == 3.0);

  // 3 * bar is not constant: the call makes the whole subtree opaque.
  double Unused = 99.0;
  CHECK(!TryEvaluateConstant(Call->GetArgs()[1].get(), Unused));
  CHECK(Unused == 99.0); // failure leaves the out-parameter untouched

  // An unknown operator reports failure instead of guessing.
  ExprPtr Weird = std::make_unique<BinaryExprAST>(
      '%', std::make_unique<NumberExprAST>(4.0),
      std::make_unique<NumberExprAST>(2.0));
  double Ignored = 0.0;
  CHECK(!TryEvaluateConstant(Weird.get(), Ignored));
}

void TestPrintedTree() {
  std::printf("Visitor: printing\n");

  ExprPtr Tree = MakeTree();
  std::ostringstream OS;
  PrintTree(Tree.get(), OS);
  const std::string Text = OS.str();

  CHECK(Text.find("Call 'foo' (2 args)") != std::string::npos);
  CHECK(Text.find("  Binary '+'") != std::string::npos);
  CHECK(Text.find("    Number 1") != std::string::npos);
  CHECK(Text.find("  Binary '*'") != std::string::npos);
  CHECK(Text.find("    Call 'bar' (0 args)") != std::string::npos);
}

} // namespace

int main() {
  TestIsa();
  TestCastNeverReturnsNull();
  TestDynCastReturnsNullOnMismatch();
  TestConstPropagation();
  TestKindSwitch();
  TestTraversalResults();
  TestPrintedTree();
  return testing::Summary("module2-custom-rtti");
}
