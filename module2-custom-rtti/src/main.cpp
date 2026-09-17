//===- main.cpp - Module 2 demo: downcasting without language RTTI ------===//
//
// Builds the same `foo(1 + 2, 3 * bar)` tree as Module 1 and walks it using
// only isa<> / cast<> / dyn_cast<>. This file is compiled with -fno-rtti, so
// `dynamic_cast` and `typeid` would not even compile here.
//
//===---------------------------------------------------------------------===//

#include "AST.h"
#include "Casting.h"
#include "Visitor.h"

#include <iostream>
#include <memory>
#include <type_traits>
#include <utility>

using namespace workbench;

namespace {

ExprPtr BuildFooCall() {
  ExprList Args;
  // 1 + 2
  Args.push_back(std::make_unique<BinaryExprAST>(
      '+', std::make_unique<NumberExprAST>(1.0),
      std::make_unique<NumberExprAST>(2.0)));
  // 3 * bar
  Args.push_back(std::make_unique<BinaryExprAST>(
      '*', std::make_unique<NumberExprAST>(3.0),
      std::make_unique<CallExprAST>("bar")));
  return std::make_unique<CallExprAST>("foo", std::move(Args));
}

void DemoTraversal(const ExprAST *Root) {
  std::cout << "== dyn_cast<>-based traversal ==\n\n";
  PrintTree(Root, std::cout, 1);
  std::cout << "\n  nodes in tree: " << CountNodes(Root) << "\n";
  std::cout << "  root kind:     " << KindName(Root) << "\n\n";
}

void DemoQueryVersusAssert(const ExprAST *Root) {
  std::cout << "== isa<> / cast<> / dyn_cast<> contracts ==\n\n";

  // isa<>: a question, answered with a bool.
  std::cout << "  isa<CallExprAST>(root)   = " << std::boolalpha
            << isa<CallExprAST>(Root) << "\n";
  std::cout << "  isa<NumberExprAST>(root) = " << isa<NumberExprAST>(Root)
            << "\n";

  // dyn_cast<>: a question whose answer is the pointer, or null.
  const auto *NotANumber = dyn_cast<NumberExprAST>(Root);
  std::cout << "  dyn_cast<NumberExprAST>(root) = "
            << (NotANumber ? "non-null" : "nullptr")
            << "  <- the query form, null is a normal answer\n";

  // cast<>: an assertion. Only legal once the type is already known, either
  // from an isa<> check or from the surrounding invariant.
  const auto *Call = cast<CallExprAST>(Root);
  std::cout << "  cast<CallExprAST>(root)->GetCallee() = " << Call->GetCallee()
            << "  <- never null; asserts if wrong\n";

  // cast<NumberExprAST>(Root) here would abort with
  //   "cast<> argument of incompatible type!"
  // in a debug build, and be undefined behaviour in a release build. That is
  // the deal: cast<> is a claim by the programmer, not a check.

  std::cout << "\n== constant folding via dyn_cast, no exceptions ==\n\n";
  for (const ExprPtr &Arg : Call->GetArgs()) {
    double Value = 0.0;
    if (TryEvaluateConstant(Arg.get(), Value))
      std::cout << "  argument folded to " << Value << "\n";
    else
      std::cout << "  argument is not a constant (contains a call)\n";
  }
  std::cout << "\n";
}

void DemoConstPropagation(const ExprAST *Root) {
  std::cout << "== const-ness survives the cast ==\n\n";

  // dyn_cast<T>(const U *) yields const T *, checked at compile time.
  static_assert(std::is_same<decltype(dyn_cast<CallExprAST>(Root)),
                             const CallExprAST *>::value,
                "dyn_cast must propagate const from the source pointer");

  ExprPtr Mutable = std::make_unique<NumberExprAST>(7.0);
  static_assert(std::is_same<decltype(dyn_cast<NumberExprAST>(Mutable.get())),
                             NumberExprAST *>::value,
                "dyn_cast on a mutable pointer must yield a mutable pointer");

  std::cout << "  dyn_cast<CallExprAST>(const ExprAST *)   -> const "
               "CallExprAST *\n";
  std::cout << "  dyn_cast<NumberExprAST>(ExprAST *)       -> NumberExprAST "
               "*\n";
  std::cout << "  (both checked by static_assert at compile time)\n\n";
}

} // namespace

int main() {
  ExprPtr Root = BuildFooCall();

  DemoTraversal(Root.get());
  DemoQueryVersusAssert(Root.get());
  DemoConstPropagation(Root.get());

  std::cout << "== cost ==\n\n";
  std::cout << "  sizeof(ExprAST)       = " << sizeof(ExprAST)
            << " bytes (vtable pointer + Kind)\n";
  std::cout << "  a dyn_cast<> is one load of Kind plus one compare;\n";
  std::cout << "  dynamic_cast walks a runtime type graph and may call "
               "strcmp.\n";
  return 0;
}
