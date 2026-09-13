//===- AST.cpp - AST printing ---------------------------------------------===//

#include "AST.h"

namespace workbench {

void ExprAST::Indentation(std::ostream &OS, unsigned Indent) {
  for (unsigned I = 0; I != Indent; ++I)
    OS << "  ";
}

void NumberExprAST::Print(std::ostream &OS, unsigned Indent) const {
  Indentation(OS, Indent);
  OS << "Number " << Val << "\n";
}

void BinaryExprAST::Print(std::ostream &OS, unsigned Indent) const {
  Indentation(OS, Indent);
  OS << "Binary '" << Op << "'\n";
  Lhs->Print(OS, Indent + 1);
  Rhs->Print(OS, Indent + 1);
}

void CallExprAST::Print(std::ostream &OS, unsigned Indent) const {
  Indentation(OS, Indent);
  OS << "Call '" << Callee << "' (" << Args.size() << " arg"
     << (Args.size() == 1 ? "" : "s") << ", storage "
     << (Args.IsSmall() ? "inline" : "heap") << ")\n";
  for (const ExprPtr &Arg : Args)
    Arg->Print(OS, Indent + 1);
}

void BlockAST::Print(std::ostream &OS, unsigned Indent) const {
  Indentation(OS, Indent);
  OS << "Block (" << Stmts.size() << " statement"
     << (Stmts.size() == 1 ? "" : "s") << ")\n";
  for (const ExprPtr &Stmt : Stmts)
    Stmt->Print(OS, Indent + 1);
}

} // namespace workbench
