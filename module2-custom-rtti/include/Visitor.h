//===- Visitor.h - Tree walks that live outside the node hierarchy -------===//
//
// None of these functions is a member of ExprAST. Adding a new analysis costs
// a new free function, not a new virtual method on every node — the reason
// LLVM and clang put traversals in separate visitor/pass files.
//
//===---------------------------------------------------------------------===//

#ifndef WORKBENCH_VISITOR_H
#define WORKBENCH_VISITOR_H

#include "AST.h"

#include <ostream>

namespace workbench {

/// Pretty-print \p E. Implemented purely with dyn_cast<>: no virtual print
/// method, no dynamic_cast, no typeid.
void PrintTree(const ExprAST *E, std::ostream &OS, unsigned Indent = 0);

/// Fold \p E to a constant if possible.
///
/// Returns false (rather than throwing or sentinel-valuing) when the
/// expression is not a compile-time constant — LLVM's no-exceptions style:
/// a bool return plus an out-parameter, the same shape as
/// `Value::getAsInteger`, `StringRef::getAsInteger`, or `LogicalResult`.
bool TryEvaluateConstant(const ExprAST *E, double &Out);

/// Number of nodes in the subtree rooted at \p E.
unsigned CountNodes(const ExprAST *E);

/// The node's kind as a string, via a switch on GetKind().
///
/// The other half of the discriminator idiom: once the kind is an enum, a
/// traversal can `switch` on it and the compiler will tell you when a new
/// node kind is added and a case is missing. `dynamic_cast` chains give you
/// no such warning.
const char *KindName(const ExprAST *E);

} // namespace workbench

#endif // WORKBENCH_VISITOR_H
