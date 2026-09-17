//===- AST.h - The Module 1 AST, now with LLVM-style RTTI ---------------===//
//
// Same ownership model as Module 1 (every edge is a unique_ptr, child lists
// live in a SmallVector). The difference: node identity is carried by an
// explicit `Kind` field instead of by the C++ type system.
//
// Note what is *gone* compared to Module 1: there is no `virtual Print`.
// Traversal and printing now live outside the node hierarchy (Visitor.h),
// dispatching on Kind via dyn_cast<>. That is how clang's AST and LLVM's
// Value hierarchy work.
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
  /// The discriminator. One enumerator per concrete node type; `NK_` stands
  /// for "node kind", matching LLVM's habit of prefixing enumerators so they
  /// stay readable at use sites.
  ///
  /// In a deeper hierarchy LLVM keeps the kinds of a subtree *contiguous* and
  /// adds `NK_FirstFoo` / `NK_LastFoo` markers, so an abstract base's classof
  /// becomes a single range check:
  ///     return N->GetKind() >= NK_FirstFoo && N->GetKind() <= NK_LastFoo;
  /// Two comparisons for "is this any kind of Foo", regardless of how many
  /// concrete subclasses exist. `dynamic_cast` cannot do that.
  enum class Kind {
    NK_Number,
    NK_Binary,
    NK_Call,
    NK_Block,
  };

  // Virtual destructor only. The Kind field, not the vtable, answers "what
  // type is this?" — the vtable is here purely so that deleting through
  // ExprPtr runs the right destructor.
  //
  // Real clang goes one step further: AST nodes are bump-allocated in an
  // ASTContext and never individually destroyed, so they have no virtual
  // destructor at all and pay no vtable pointer.
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
  explicit NumberExprAST(double Val)
      : ExprAST(Kind::NK_Number), Val(Val) {}

  double GetValue() const { return Val; }

  /// The single hook the casting machinery needs. Note the signature: it takes
  /// the *base* pointer and answers a question about it. It is static, so
  /// there is no dispatch cost and no vtable entry.
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

  static bool classof(const ExprAST *N) {
    return N->GetKind() == Kind::NK_Block;
  }

private:
  ExprList Stmts;
};

} // namespace workbench

#endif // WORKBENCH_AST_H
