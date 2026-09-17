# Module 2 — Custom LLVM-Style RTTI

**Interview proof point:** *downcasting without C++ RTTI.*

## What this builds

The same AST as Module 1, plus:

- `ExprAST::Kind` — an `enum class` discriminator stored in the base class;
- `static bool classof(const ExprAST *)` on every concrete node;
- `include/Casting.h` — a ~90-line `isa<>` / `cast<>` / `dyn_cast<>` implementation with the same
  contracts as `llvm/Support/Casting.h`;
- `src/Visitor.cpp` — printing, node counting and constant evaluation implemented **outside** the node
  hierarchy, using `dyn_cast<>` only.

The module compiles with `-fno-rtti`, so `dynamic_cast` and `typeid` are not merely avoided here — they
would fail to compile.

Note what disappeared relative to Module 1: `virtual void Print()`. The only virtual function left is
the destructor.

## Why does LLVM avoid `dynamic_cast`?

Four reasons, in rough order of how much LLVM cares:

**1. Binary size and build-wide cost.** `-frtti` makes the compiler emit a `type_info` object, a mangled
type-name string, and an inheritance-graph record for every polymorphic class in the program, whether or
not anyone downcasts. On a codebase with LLVM's number of class hierarchies this is megabytes of
read-only data that must be linked, paged in, and shipped. LLVM and clang are compiled `-fno-rtti`
globally, which also means any library linking against LLVM inherits the constraint.

**2. `dynamic_cast` is slow and its cost is unbounded.** It is a library call (`__dynamic_cast`) that
walks the runtime inheritance graph of the object. In the Itanium ABI, comparing types can come down to
comparing mangled-name *strings* across shared-library boundaries, because two copies of a `type_info`
for the same type may not be pointer-equal. The cost depends on hierarchy depth, and it is opaque to the
optimizer.

`dyn_cast<NumberExprAST>(E)` compiles to: load one enum field, compare it to one constant. That is it.
It inlines, it branch-predicts well, and in a compiler this happens *millions of times per translation
unit* — every instruction visit in every pass is a downcast.

**3. It expresses things the type system can't.** Because `classof` is just a predicate, LLVM keeps the
kinds of a subtree contiguous and writes:

```cpp
static bool classof(const Instruction *I) {
  return I->getOpcode() >= FirstBinaryOp && I->getOpcode() <= LastBinaryOp;
}
```

"Is this any kind of binary operator?" becomes two integer compares, no matter how many concrete
subclasses exist. `dynamic_cast` cannot do a range query; it would need one attempt per candidate type.
`classof` can also key off something other than the C++ type — `ConstantInt` vs. a `Constant` holding a
particular value, for instance — so the "RTTI" hierarchy does not have to be the class hierarchy.

**4. It works on non-polymorphic types.** `dynamic_cast` requires a vtable. LLVM's `Value` hierarchy and
clang's `Type` hierarchy include classes with no virtual functions at all (clang's AST nodes are
bump-allocated in an `ASTContext` and never individually destructed, so they don't even pay for a
virtual destructor). `classof` needs nothing but a field.

The cost of the idiom is honest bookkeeping: adding a node type means adding an enumerator and a
`classof`, and a hierarchy with non-contiguous kind ranges will produce subtly wrong `classof` results
that the compiler cannot catch. That is the trade LLVM accepts.

## The `cast<>` vs. `dyn_cast<>` contract

This is the part interviewers actually probe, because misusing it is the most common source of
`nullptr` crashes in LLVM-adjacent code.

| | `isa<T>(P)` | `cast<T>(P)` | `dyn_cast<T>(P)` |
|---|---|---|---|
| Question or claim? | question | **claim** | question |
| Returns | `bool` | `T *`, never null | `T *` or `nullptr` |
| Wrong type | `false` | **assert / UB** | `nullptr` |
| Null input | assert | assert | assert |
| Null-tolerant variant | `isa_and_present<T>` | `cast_if_present<T>` | `dyn_cast_if_present<T>` |
| Release-build cost | 1 load + 1 compare | **zero** (bare `static_cast`) | 1 load + 1 compare |

- **`cast<T>` is an assertion, not a check.** You are telling the compiler and the reader "I already know
  this is a `T`." In a debug build a violation trips `assert(isa<To>(Val) && "cast<> argument of
  incompatible type!")`; in a release build (`-DNDEBUG`) the assert vanishes and you get an unchecked
  `static_cast` — undefined behaviour, usually a silent misread of adjacent memory. So `if (cast<T>(P))`
  is always a code smell: it can never be null, and the check tells the reader you weren't sure.
- **`dyn_cast<T>` is the query form**, and null is a normal, expected answer. Its idiomatic use is the
  declaration-in-condition:
  ```cpp
  if (const auto *Bin = dyn_cast<BinaryExprAST>(E)) { ... }
  ```
  which also scopes `Bin` to the branch where it is valid.
- **Both assert on a null *input*.** This surprises people. Passing null means the caller lost track of
  whether it had an object at all, which LLVM treats as a bug rather than a question — hence the separate
  `*_if_present` family (historically `dyn_cast_or_null`) for the cases where null is legitimately
  possible.
- **Const propagates.** `dyn_cast<T>(const U *)` returns `const T *`. Module 2's `CastReturnType` trait
  is a small version of LLVM's `cast_retty`, and `tests/Tests.cpp` pins the behaviour down with
  `static_assert`, so the cast can't be used to launder const away.

Real `llvm/Support/Casting.h` generalizes this further through `CastInfo`/`simplify_type` so the same
`dyn_cast` works on references, `std::unique_ptr`, and `Optional`-like wrappers. The control flow is the
one implemented here.

## Build and run

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug   # Debug keeps cast<> assertions live
cmake --build build
./build/module2-demo
ctest --test-dir build --output-on-failure
```

## What I'd explain in an interview

- "LLVM replaces language RTTI with a `Kind` field in the base class plus a static `classof` predicate
  per subclass, and `isa`/`cast`/`dyn_cast` are just templates over `classof`. A downcast becomes one
  load and one integer compare that inlines — versus `__dynamic_cast` walking an inheritance graph and
  potentially comparing mangled type-name strings."
- "The reason isn't only speed. `-fno-rtti` drops the `type_info` and type-name data for every
  polymorphic class in the binary, `classof` works on classes with no vtable at all — clang's AST nodes
  are arena-allocated and have no virtual destructor — and because it's just a predicate you can answer
  range queries like 'is this any kind of binary operator' in two compares."
- "The contract difference matters more than the mechanism: `cast<>` is an assertion that returns a
  never-null pointer and degrades to a raw `static_cast` under `-DNDEBUG`, so it's only legal when the
  type is already known. `dyn_cast<>` is the query and null is a valid answer. Both assert on a null
  input, which is why the `_if_present` variants exist."
- "I kept const-propagation in my implementation via a `cast_retty`-style trait and pinned it with
  `static_assert`, because a downcast that silently drops const is how a 'read-only' analysis pass ends
  up mutating IR."
