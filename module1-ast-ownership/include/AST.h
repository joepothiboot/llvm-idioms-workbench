//===- AST.h - Expression AST with strict unique_ptr ownership -----------===//
//
// Ownership model (the entire point of Module 1):
//
//   * Every edge in the tree is a std::unique_ptr<ExprAST>.
//   * A node owns its children and nothing else owns them.
//   * Destroying the root destroys the whole tree, once, in one place.
//   * There is no owning raw pointer and no shared_ptr anywhere.
//
// Child *lists* (call arguments, block statements) use SmallVector so that a
// typical 1-4 element list costs zero heap allocations on top of the nodes
// themselves.
//
//===---------------------------------------------------------------------===//

#ifndef WORKBENCH_AST_H
#define WORKBENCH_AST_H

#include "SmallVector.h"

#include <memory>
#include <ostream>
#include <string>
#include <utility>

namespace workbench {

class ExprAST;

/// The one and only owning handle for an AST node.
using ExprPtr = std::unique_ptr<ExprAST>;

/// Inline capacity for child lists. Four covers the overwhelming majority of
/// real call sites; LLVM picks these numbers the same way, by measuring.
static constexpr unsigned ExprListInlineCapacity = 4;

/// A move-only list of owned children.
using ExprList = SmallVector<ExprPtr, ExprListInlineCapacity>;

/// Base class for all expressions.
class ExprAST {
public:
  virtual ~ExprAST() = default;

  /// Module 1 identifies node types with virtual dispatch. Module 2 replaces
  /// this with LLVM-style `classof` + `dyn_cast<>` and explains why.
  virtual void Print(std::ostream &OS, unsigned Indent = 0) const = 0;

protected:
  ExprAST() = default;

  // AST nodes are identified by address and owned by exactly one parent, so
  // copying one would be meaningless at best and a double-ownership bug at
  // worst. Suppress it explicitly.
  ExprAST(const ExprAST &) = delete;
  ExprAST &operator=(const ExprAST &) = delete;

  static void Indentation(std::ostream &OS, unsigned Indent);
};

/// Numeric literal: `42`.
class NumberExprAST : public ExprAST {
public:
  explicit NumberExprAST(double Val) : Val(Val) {}

  double GetValue() const { return Val; }

  void Print(std::ostream &OS, unsigned Indent = 0) const override;

private:
  double Val;
};

/// Binary operation: `Lhs Op Rhs`.
class BinaryExprAST : public ExprAST {
public:
  BinaryExprAST(char Op, ExprPtr Lhs, ExprPtr Rhs)
      : Op(Op), Lhs(std::move(Lhs)), Rhs(std::move(Rhs)) {}

  char GetOp() const { return Op; }

  // Non-owning observers. Callers may read through these but must never
  // delete them or store them past a tree mutation.
  const ExprAST *GetLhs() const { return Lhs.get(); }
  const ExprAST *GetRhs() const { return Rhs.get(); }

  void Print(std::ostream &OS, unsigned Indent = 0) const override;

private:
  char Op;
  ExprPtr Lhs;
  ExprPtr Rhs;
};

/// Call expression: `Callee(Args...)`.
///
/// A zero-argument call also stands in for a variable reference (`bar`), which
/// keeps the node set small enough to read in one sitting.
class CallExprAST : public ExprAST {
public:
  CallExprAST(std::string Callee, ExprList Args)
      : Callee(std::move(Callee)), Args(std::move(Args)) {}

  explicit CallExprAST(std::string Callee) : Callee(std::move(Callee)) {}

  const std::string &GetCallee() const { return Callee; }
  const ExprList &GetArgs() const { return Args; }

  /// True while the argument list still lives inside this node.
  bool ArgsAreInline() const { return Args.IsSmall(); }

  void Print(std::ostream &OS, unsigned Indent = 0) const override;

private:
  std::string Callee;
  ExprList Args;
};

/// A sequence of statements: `{ a; b; c; }`.
class BlockAST : public ExprAST {
public:
  BlockAST() = default;
  explicit BlockAST(ExprList Stmts) : Stmts(std::move(Stmts)) {}

  void AddStatement(ExprPtr Stmt) { Stmts.push_back(std::move(Stmt)); }

  const ExprList &GetStatements() const { return Stmts; }

  void Print(std::ostream &OS, unsigned Indent = 0) const override;

private:
  ExprList Stmts;
};

} // namespace workbench

#endif // WORKBENCH_AST_H
