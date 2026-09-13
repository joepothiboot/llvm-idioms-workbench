# Module 1 — AST Ownership & Allocation Engine

**Interview proof point:** *small-array allocation optimization, and who owns an AST node.*

## What this builds

An expression AST (`NumberExprAST`, `BinaryExprAST`, `CallExprAST`, `BlockAST`) where:

- every parent→child edge is a `std::unique_ptr<ExprAST>` — no owning raw pointers, no `shared_ptr`;
- child *lists* (call arguments, block statements) live in a `SmallVector<ExprPtr, 4>` whose first four
  elements are stored **inside the parent node**;
- a replacement global `operator new` proves the claim instead of asserting it in a comment.

```
$ ./build/module1-demo
== foo(1 + 2, 3 * bar) ==

  [heap] 2-element argument list (inline capacity 4): 0 allocations, 0 bytes
  storage is inline, size 2, capacity 4

Call 'foo' (2 args, storage inline)
  Binary '+'
    Number 1
    Number 2
  Binary '*'
    Number 3
    Call 'bar' (0 args, storage inline)
```

## Why the idiom exists

### Why `unique_ptr` and not `shared_ptr`

An AST is a tree, and a tree has exactly one owner per node. `shared_ptr` would buy nothing and cost a
lot: two words per edge instead of one, an atomic increment on every copy, a control block allocation
per node, and — worst of all — it would make the ownership question *unanswerable by reading the code*.
Anyone could stash a copy of a node and keep it alive after the tree that contains it is gone. Cycles
introduced later would silently leak.

With `unique_ptr`, the lifetime rule is mechanical: destroying the root destroys the entire tree exactly
once, and the only way to move a subtree elsewhere is `std::move`, which is visible at the call site and
grep-able in review. That property is what Module 3 relies on to mutate the tree in place safely.

The non-owning accessors (`GetLhs()` returning `const ExprAST *`) are the other half of the convention:
LLVM passes raw pointers around *constantly*, but a raw pointer in LLVM never implies ownership. That
distinction — `unique_ptr` for edges, raw pointers for observation — is the whole ownership discipline.

### Why `SmallVector` and not `std::vector`

`std::vector` always heap-allocates the moment you push the first element. A compiler builds millions of
tiny lists: argument lists, operand lists, successor lists, use lists. Profiling real compilers shows the
distribution is brutally skewed — the overwhelming majority of these lists hold one to four elements.
Paying a `malloc` + `free` + cache miss per list is the single easiest large win available in a frontend,
which is why `llvm::SmallVector` is arguably the most-used type in all of LLVM.

The trade is explicit: the parent object gets bigger (`sizeof(CallExprAST)` is 88 bytes here, with four
inline `unique_ptr` slots), and you must pick `N` deliberately. Too large and you bloat every node and
blow out the cache lines you were trying to save; too small and you pay the allocation anyway plus the
wasted inline bytes. LLVM picks these numbers by measuring, not by intuition, and `llvm::SmallVector`'s
own header warns against guessing.

The spill path is worth reading in `Grow()`: once the list outgrows `N`, the container behaves exactly
like a `std::vector` (double capacity, move elements, free the old buffer). Nothing about the API
changes, which is why the type is a drop-in replacement.

### A subtlety this module makes visible

`SmallVector` is **not** trivially movable while its data is inline. Moving a heap-backed vector just
steals a pointer; moving an inline-backed one must move every element, because the storage is part of the
source object. The unit test asserts both behaviours. This is the reason `llvm::SmallVector` is passed by
`&`/`*` and the reason LLVM APIs take `ArrayRef<T>` (a view) rather than a container by value.

### Note on this implementation

`include/SmallVector.h` is a ~200-line educational re-implementation so that Module 1 builds with **zero
external dependencies**. Production code should `#include "llvm/ADT/SmallVector.h"` and use
`llvm::SmallVector` directly — it additionally handles over-aligned types, `SmallVectorImpl<T>` as the
size-erased base class for function parameters, `grow_pod` specializations for trivially-copyable types,
and 32-bit size fields to keep the header small. The growth policy and the inline-storage trick are the
same.

Container methods here are deliberately spelled `push_back`/`size`/`begin` (STL style) rather than
PascalCase: `llvm::SmallVector` is a drop-in `std::vector` replacement, and LLVM's coding standard keeps
STL-compatible container APIs spelled the STL way.

## Build and run

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build
./build/module1-demo
ctest --test-dir build --output-on-failure
```

Compiles with `-fno-exceptions -fno-rtti -Wall -Wextra`, so LLVM's conventions are enforced by the
compiler rather than by convention.

## What I'd explain in an interview

- "Every edge in my AST is a `unique_ptr`, so the lifetime question has exactly one answer: the parent
  owns the child, and dropping the root drops the tree. Raw pointers appear in the accessors, but in
  LLVM a raw pointer never means ownership — that split is the discipline that makes the in-place tree
  mutation in Module 3 provably safe."
- "Argument lists go in a `SmallVector<ExprPtr, 4>` because compilers allocate an enormous number of
  tiny lists, and the length distribution is skewed to 1–4 elements. I instrumented the global
  `operator new` to show a 2-element argument list costs zero allocations, and that the fifth push costs
  exactly one."
- "The trade-off is node size, not free lunch: the inline buffer makes `CallExprAST` 88 bytes. You pick
  `N` by measuring the real distribution — too big and you lose the cache benefit you were chasing."
- "The interesting edge case is that `SmallVector` moves aren't uniform. Heap-backed moves steal a
  pointer; inline-backed moves have to move each element, because the storage lives inside the object.
  That's exactly why LLVM passes `SmallVectorImpl&` or `ArrayRef` across API boundaries instead of
  passing containers by value."
