//===- Visitor.cpp - dyn_cast-based AST traversals -----------------------===//

#include "Visitor.h"

#include "Casting.h"

namespace workbench {

namespace {

void Indent(std::ostream &OS, unsigned Depth) {
  for (unsigned I = 0; I != Depth; ++I)
    OS << "  ";
}

} // namespace

void PrintTree(const ExprAST *E, std::ostream &OS, unsigned Depth) {
  if (!E) {
    Indent(OS, Depth);
    OS << "<null>\n";
    return;
  }

  // The canonical LLVM traversal shape: a chain of `if (auto *X =
  // dyn_cast<...>(E))`. Each test is one load of the Kind field and one
  // integer compare.
  if (const auto *Num = dyn_cast<NumberExprAST>(E)) {
    Indent(OS, Depth);
    OS << "Number " << Num->GetValue() << "\n";
    return;
  }

  if (const auto *Bin = dyn_cast<BinaryExprAST>(E)) {
    Indent(OS, Depth);
    OS << "Binary '" << Bin->GetOp() << "'\n";
    PrintTree(Bin->GetLhs(), OS, Depth + 1);
    PrintTree(Bin->GetRhs(), OS, Depth + 1);
    return;
  }

  if (const auto *Call = dyn_cast<CallExprAST>(E)) {
    Indent(OS, Depth);
    OS << "Call '" << Call->GetCallee() << "' (" << Call->GetArgs().size()
       << " arg" << (Call->GetArgs().size() == 1 ? "" : "s") << ")\n";
    for (const ExprPtr &Arg : Call->GetArgs())
      PrintTree(Arg.get(), OS, Depth + 1);
    return;
  }

  if (const auto *Block = dyn_cast<BlockAST>(E)) {
    Indent(OS, Depth);
    OS << "Block (" << Block->GetStatements().size() << " statement"
       << (Block->GetStatements().size() == 1 ? "" : "s") << ")\n";
    for (const ExprPtr &Stmt : Block->GetStatements())
      PrintTree(Stmt.get(), OS, Depth + 1);
    return;
  }

  Indent(OS, Depth);
  OS << "<unknown node kind>\n";
}

bool TryEvaluateConstant(const ExprAST *E, double &Out) {
  if (!E)
    return false;

  if (const auto *Num = dyn_cast<NumberExprAST>(E)) {
    Out = Num->GetValue();
    return true;
  }

  if (const auto *Bin = dyn_cast<BinaryExprAST>(E)) {
    double Lhs = 0.0;
    double Rhs = 0.0;
    if (!TryEvaluateConstant(Bin->GetLhs(), Lhs) ||
        !TryEvaluateConstant(Bin->GetRhs(), Rhs))
      return false;

    switch (Bin->GetOp()) {
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
      // Unknown operator: report failure rather than guessing. No exception,
      // no abort — the caller decides what to do.
      return false;
    }
  }

  // Calls and blocks are opaque to constant folding.
  return false;
}

unsigned CountNodes(const ExprAST *E) {
  if (!E)
    return 0;

  if (const auto *Bin = dyn_cast<BinaryExprAST>(E))
    return 1 + CountNodes(Bin->GetLhs()) + CountNodes(Bin->GetRhs());

  if (const auto *Call = dyn_cast<CallExprAST>(E)) {
    unsigned Count = 1;
    for (const ExprPtr &Arg : Call->GetArgs())
      Count += CountNodes(Arg.get());
    return Count;
  }

  if (const auto *Block = dyn_cast<BlockAST>(E)) {
    unsigned Count = 1;
    for (const ExprPtr &Stmt : Block->GetStatements())
      Count += CountNodes(Stmt.get());
    return Count;
  }

  return 1; // NumberExprAST: a leaf.
}

const char *KindName(const ExprAST *E) {
  if (!E)
    return "<null>";

  switch (E->GetKind()) {
  case ExprAST::Kind::NK_Number:
    return "NK_Number";
  case ExprAST::Kind::NK_Binary:
    return "NK_Binary";
  case ExprAST::Kind::NK_Call:
    return "NK_Call";
  case ExprAST::Kind::NK_Block:
    return "NK_Block";
  }
  // No default: adding a Kind enumerator makes -Wswitch flag this function.
  return "<unhandled>";
}

} // namespace workbench
