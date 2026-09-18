//===- AST.h - Module 2's AST, made mutable for passes -------------------===//
//
// Two changes relative to Module 2:
//
// 1. A `ReturnExprAST` node (kind `NK_Return`) so the dead-code pass has a
//    terminator to reason about.
//
// 2. *Slot* accessors: `GetLhsSlot()` returns `ExprPtr &` — a reference to the
//    owning unique_ptr, not to the node. A transformation needs the owning
//    edge, because replacing a node means writing through the pointer that
//    owns it. Handing a pass a bare `ExprAST *` would let it inspect a node it
//    has no way to replace.
//
//    This is the same distinction LLVM draws between a `Value *` and a `Use &`:
//    you cannot RAUW anything if all you hold is the value.
//
//===---------------------------------------------------------------------===//

#ifndef WORKBENCH_AST_H
#define WORKBENCH_AST_H

#include "SmallVector.h"

#include <memory>
#include <string>
#include <utility>

namespace workbench {

class ExprAST;
using ExprPtr = std::unique_ptr<ExprAST>;

static constexpr unsigned ExprListInlineCapacity = 4;
using ExprList = SmallVector<ExprPtr, ExprListInlineCapacity>;

/// Base class for all expressions.
class ExprAST {
public:
  /// Adding a node type means adding an enumerator here and a `classof`
  /// below. A real hierarchy would keep related kinds contiguous and add
  /// NK_First*/NK_Last* markers so an abstract base's classof is a range
  /// check.
  enum class Kind {
    NK_Number,
    NK_Binary,
    NK_Call,
    NK_Block,
    NK_Return,
  };

  virtual ~ExprAST() = default;

  Kind GetKind() const { return NodeKind; }

protected:
  explicit ExprAST(Kind NodeKind) : NodeKind(NodeKind) {}

  ExprAST(const ExprAST &) = delete;
  ExprAST &operator=(const ExprAST &) = delete;

private:
  const Kind NodeKind;
};

/// Numeric literal: `42`.
class NumberExprAST : public ExprAST {
public:
  explicit NumberExprAST(double Val) : ExprAST(Kind::NK_Number), Val(Val) {}

  double GetValue() const { return Val; }

  static bool classof(const ExprAST *N) {
    return N->GetKind() == Kind::NK_Number;
  }

private:
  double Val;
};

/// Binary operation: `Lhs Op Rhs`.
class BinaryExprAST : public ExprAST {
public:
  BinaryExprAST(char Op, ExprPtr Lhs, ExprPtr Rhs)
      : ExprAST(Kind::NK_Binary), Op(Op), Lhs(std::move(Lhs)),
        Rhs(std::move(Rhs)) {}

  char GetOp() const { return Op; }

  const ExprAST *GetLhs() const { return Lhs.get(); }
  const ExprAST *GetRhs() const { return Rhs.get(); }

  /// The owning edges. A pass writes through these to replace a child, and
  /// moves out of them to hoist a subtree.
  ExprPtr &GetLhsSlot() { return Lhs; }
  ExprPtr &GetRhsSlot() { return Rhs; }

  static bool classof(const ExprAST *N) {
    return N->GetKind() == Kind::NK_Binary;
  }

private:
  char Op;
  ExprPtr Lhs;
  ExprPtr Rhs;
};

/// Call expression: `Callee(Args...)`. A zero-argument call also stands in for
/// a variable reference (`bar`).
class CallExprAST : public ExprAST {
public:
  CallExprAST(std::string Callee, ExprList Args)
      : ExprAST(Kind::NK_Call), Callee(std::move(Callee)),
        Args(std::move(Args)) {}

  explicit CallExprAST(std::string Callee)
      : ExprAST(Kind::NK_Call), Callee(std::move(Callee)) {}

  const std::string &GetCallee() const { return Callee; }
  const ExprList &GetArgs() const { return Args; }
  ExprList &GetArgSlots() { return Args; }
  bool ArgsAreInline() const { return Args.IsSmall(); }

  static bool classof(const ExprAST *N) {
    return N->GetKind() == Kind::NK_Call;
  }

private:
  std::string Callee;
  ExprList Args;
};

/// A sequence of statements: `{ a; b; c; }`.
class BlockAST : public ExprAST {
public:
  BlockAST() : ExprAST(Kind::NK_Block) {}
  explicit BlockAST(ExprList Stmts)
      : ExprAST(Kind::NK_Block), Stmts(std::move(Stmts)) {}

  void AddStatement(ExprPtr Stmt) { Stmts.push_back(std::move(Stmt)); }

  const ExprList &GetStatements() const { return Stmts; }
  ExprList &GetStatementSlots() { return Stmts; }

  static bool classof(const ExprAST *N) {
    return N->GetKind() == Kind::NK_Block;
  }

private:
  ExprList Stmts;
};

/// `return <expr>;` — the terminator the dead-code pass looks for. The
/// operand may be null (`return;`).
class ReturnExprAST : public ExprAST {
public:
  explicit ReturnExprAST(ExprPtr Value = nullptr)
      : ExprAST(Kind::NK_Return), Value(std::move(Value)) {}

  const ExprAST *GetValue() const { return Value.get(); }
  ExprPtr &GetValueSlot() { return Value; }

  static bool classof(const ExprAST *N) {
    return N->GetKind() == Kind::NK_Return;
  }

private:
  ExprPtr Value;
};

} // namespace workbench

#endif // WORKBENCH_AST_H
