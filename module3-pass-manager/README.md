# Module 3 — AST Transformation Passes & Move Semantics

**Interview proof point:** *memory lifetime during a transformation.*

## What this builds

- `Pass` — an abstract transformation: `virtual bool Run(ExprPtr &Root)`.
- `PassManager` — owns a sequence of passes, runs them in order, reports what changed.
- `ConstantFoldingPass` (Pass A) — evaluates constant subexpressions and hoists subtrees out of
  algebraic identities.
- `DeadCodeEliminationPass` (Pass B) — drops statements after a `return` in a block.
- A `ReturnExprAST` node (`NK_Return`) added to Module 2's kind enum, so there is a terminator to
  reason about.

## The signature is the lesson

```cpp
virtual bool Run(ExprPtr &Root) = 0;   // ExprPtr = std::unique_ptr<ExprAST>
//               ^^^^^^^^^^^^
```

Two things fall out of that reference:

**A pass can replace the root.** Folding a tree that is entirely constant (`1 + 2`) means the root node
is destroyed and a `NumberExprAST` takes its place. A pass that received `ExprAST *` could mutate the
node's *contents* but could never tell its caller "your root is a different object now." The demo prints
the before and after addresses to make this concrete.

**Every recursive step operates on an owning edge, not on a node.** The AST exposes *slot* accessors
(`GetLhsSlot()` returns `ExprPtr &`). This is the same distinction LLVM draws between a `Value *` and a
`Use &`: you cannot `replaceAllUsesWith` anything if all you hold is the value. A transformation needs
the pointer that owns the node in order to replace it.

The `bool` return is LLVM's `runOnFunction` contract: "did I change anything?" A real pass manager uses
it to invalidate cached analyses; here it drives the log.

## Pass A — Constant folding

Bottom-up (post-order), so nested constants collapse in one traversal instead of needing a fixpoint loop.

### Case 1: both operands constant — build a replacement

```
BEFORE                          AFTER
------                          -----
Return                          Return
└── Binary '+'                  └── Number 16
    ├── Binary '*'
    │   ├── Number 2
    │   └── Number 5
    └── Binary '-'
        ├── Number 10
        └── Number 4

7 nodes                         1 node
```

The traversal visits the `*` node first (→ `Number 10`), then the `-` node (→ `Number 6`), then the `+`
node sees two constants and folds to 16. Four `Binary` nodes and their children are destroyed; nothing
is destroyed twice.

```cpp
ExprPtr Folded = std::make_unique<NumberExprAST>(Value);  // build first
Slot = std::move(Folded);                                 // then publish
// Bin, Lhs, Rhs are dangling from here on.
```

### Case 2: algebraic identity — hoist a surviving subtree

```
BEFORE                          AFTER
------                          -----
Binary '+'                      Call 'bar'      <-- the SAME node object,
├── Call 'bar'                                      same address
└── Number 0
```

This is the interesting one, because a subtree has to **outlive its own parent**:

```cpp
ExprPtr Hoisted = std::move(Bin->GetLhsSlot());  // parent's edge is now null
Slot = std::move(Hoisted);                       // parent dies; 'bar' survives
```

The unit test asserts that the resulting root has the *same address* as the original `bar` node. If the
implementation had copied, the address would differ; if it had left the parent owning the subtree, this
would be a use-after-free.

### How `std::move` prevents the double free

There are exactly three unsafe ways to do this by hand, and `unique_ptr` + `std::move` rules all of them
out:

1. **Two owners.** With raw pointers you would write `Parent->Lhs` into the grandparent's slot and then
   `delete Parent`. `Parent`'s destructor also frees `Lhs` → double free. With `unique_ptr`, moving out
   of `Bin->GetLhsSlot()` **nulls the source edge as part of the move**. The parent's destructor then
   frees nothing. Ownership is transferred, never duplicated, and the transfer is atomic with respect to
   the destructor that would otherwise compete for it.
2. **Freeing the survivor.** If you destroyed the parent first and then tried to read its child pointer,
   you would read freed memory. The move happens *before* the parent dies, so the surviving subtree is
   already owned by a local `unique_ptr` at the moment the parent is destroyed.
3. **Leaking the replaced subtree.** Assigning to the owning slot (`Slot = std::move(Folded)`) *is* the
   deallocation: `unique_ptr::operator=` releases the source, then deletes the old pointee, which
   recursively destroys the entire replaced subtree. There is no `delete` to forget.

The ordering guarantee matters and is worth being explicit about: `unique_ptr::operator=(unique_ptr&&)`
releases the right-hand side first and only then deletes the old pointee, so even
`Slot = std::move(Bin->GetLhsSlot())` — where the source lives *inside* the object about to be destroyed
— is well defined. The implementation still splits it into two statements, because correctness that
depends on the reader knowing the internal ordering of a library operator is correctness you will lose
in the next refactor.

The rule that falls out, and the one worth saying out loud in an interview:

> **After you write through an owning slot, every raw pointer into the old subtree is dangling.** Do the
> replacement last, and never read `Bin` again.

`tests/Tests.cpp` pins this down with an allocation-balance check (the instrumented `operator new` from
Module 1), and the whole module runs clean under
`-fsanitize=address,undefined`:

```bash
cmake -S . -B build-asan -DCMAKE_BUILD_TYPE=Debug \
      -DCMAKE_CXX_FLAGS="-fsanitize=address,undefined -fno-omit-frame-pointer"
cmake --build build-asan && ./build-asan/module3-tests
```

### One identity deliberately missing

`x * 0 → 0` is **not** implemented. It discards the subtree `x`, and if `x` contains a call it discards
that call's side effects. Doing it soundly needs an effect analysis first. This is the shape of most real
compiler bugs: the algebra is right and the semantics are wrong.

## Pass B — Dead code elimination

```
BEFORE                          AFTER
------                          -----
Block (4 statements)            Block (2 statements)
├── Call 'foo'                  ├── Call 'foo'
├── Return                      └── Return
│   └── Number 16                   └── Number 16
├── Call 'baz'          <-- dropped
│   └── Number 9        <-- dropped with its parent
└── Return              <-- dropped
    └── Number 0        <-- dropped with its parent
```

The removal is a single `Stmts.truncate(I + 1)`. Because the container holds `unique_ptr`s, truncating
destroys each dropped statement, and destroying a statement destroys its entire subtree — four nodes
freed by one call, with no window in which the block holds a slot pointing at freed memory. That is the
same property as Pass A, stated for a container instead of a single edge, and it is why `truncate` is a
better tool here than erasing elements one at a time.

The pass recurses into nested blocks first, so an unreachable tail inside a block that is itself about to
be deleted still gets counted correctly. It stops short of a real reachability analysis — a block whose
last statement is a `return` does not make the *following* statement in the enclosing block dead, in this
implementation. Real DCE works on a CFG, not on nesting; this pass is here to demonstrate ownership
during removal, not to be a complete analysis.

## How this compares to Module 4

This module walks the tree by hand: every pass owns its own recursion, its own match logic, and its own
replacement logic, and the three are interleaved in the same function. Module 4 does the same class of
transformation in MLIR, where the traversal is supplied by the framework and the pass only declares
*patterns*. Comparing `TryFoldBinary` with Module 4's `matchAndRewrite` is the clearest way to see what
pattern-based rewriting actually buys.

## Build and run

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build
./build/module3-demo
ctest --test-dir build --output-on-failure
```

## What I'd explain in an interview

- "My pass interface takes `std::unique_ptr<ExprAST>&`, not `ExprAST*`, because folding a constant root
  replaces the root node — the pass has to be able to publish a new node through the caller's owning
  pointer. The same reasoning applies at every level, so the AST exposes slot accessors that hand out the
  owning edge. It's the `Value*` versus `Use&` distinction in LLVM."
- "The dangerous rewrite is the one where a child has to outlive its parent — `bar + 0 → bar`. I move the
  child out of the parent's slot first, which nulls the parent's edge, then assign into the parent's own
  slot. Moving is what makes the double free impossible: ownership transfers, it never duplicates, so the
  parent's destructor has nothing left to free. My test asserts the surviving node has the same address
  it started with, which proves it was moved and not copied."
- "The invariant I'd write on the whiteboard: after writing through an owning slot, every raw pointer into
  the old subtree is dangling — so the replacement is always the last statement that mentions them. I
  verify it with an allocation-balance assertion and by running the suite under ASan and UBSan."
- "I left `x * 0 → 0` out on purpose. It's algebraically correct and semantically wrong, because the
  subtree you're discarding might contain a call with side effects. Knowing which obvious rewrite is
  unsound is more of the job than knowing the rewrites."
