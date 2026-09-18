//===- ASTPrinter.cpp -----------------------------------------------------===//

#include "ASTPrinter.h"

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
    OS << "<empty>\n";
    return;
  }

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

  if (const auto *Ret = dyn_cast<ReturnExprAST>(E)) {
    Indent(OS, Depth);
    OS << "Return\n";
    if (Ret->GetValue())
      PrintTree(Ret->GetValue(), OS, Depth + 1);
    return;
  }

  Indent(OS, Depth);
  OS << "<unknown node kind>\n";
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

  if (const auto *Ret = dyn_cast<ReturnExprAST>(E))
    return 1 + CountNodes(Ret->GetValue());

  return 1;
}

} // namespace workbench
