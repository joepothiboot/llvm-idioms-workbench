//===- ASTPrinter.h - dyn_cast-based printer (carried over from Module 2) ==//

#ifndef WORKBENCH_ASTPRINTER_H
#define WORKBENCH_ASTPRINTER_H

#include "AST.h"

#include <ostream>

namespace workbench {

/// Print the subtree rooted at \p E as an indented tree.
void PrintTree(const ExprAST *E, std::ostream &OS, unsigned Indent = 0);

/// Number of nodes in the subtree rooted at \p E, so before/after sizes can
/// be compared.
unsigned CountNodes(const ExprAST *E);

} // namespace workbench

#endif // WORKBENCH_ASTPRINTER_H
