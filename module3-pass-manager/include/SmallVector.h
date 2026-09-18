//===- SmallVector.h - Minimal educational llvm::SmallVector -------------===//
//
// A deliberately small re-implementation of the idiom behind
// llvm/ADT/SmallVector.h: N elements live in storage embedded *inside* the
// container object, and the heap is only touched if the container outgrows N.
//
// Production code should use llvm::SmallVector directly. This file exists so
// that Module 1 builds with zero external dependencies, and so the growth
// policy is visible instead of hidden behind 700 lines of LLVM template
// machinery.
//
// Container member functions use STL-style lower_case names on purpose:
// llvm::SmallVector is a drop-in std::vector replacement, and LLVM's coding
// standard keeps STL-compatible container APIs spelled the STL way.
//
//===---------------------------------------------------------------------===//

#ifndef WORKBENCH_SMALLVECTOR_H
#define WORKBENCH_SMALLVECTOR_H

#include <cassert>
#include <cstddef>
#include <new>
#include <type_traits>
#include <utility>

namespace workbench {

/// A vector with inline storage for up to \p N elements.
///
/// Invariant: `Begin` points either at the inline buffer (`IsSmall()`) or at a
/// heap block owned by this object. Nothing else may own that block, which is
/// why the container is move-only.
template <typename T, unsigned N> class SmallVector {
  static_assert(N > 0, "inline capacity must be non-zero");
  static_assert(alignof(T) <= alignof(std::max_align_t),
                "over-aligned element types are out of scope for this demo");

public:
  using value_type = T;
  using iterator = T *;
  using const_iterator = const T *;

  SmallVector() : Begin(InlineStorage()), Size(0), Capacity(N) {}

  // Copying an AST node list would mean copying unique_ptrs, so the container
  // is move-only. This is also true of llvm::SmallVector<std::unique_ptr<T>>.
  SmallVector(const SmallVector &) = delete;
  SmallVector &operator=(const SmallVector &) = delete;

  SmallVector(SmallVector &&RHS) noexcept
      : Begin(InlineStorage()), Size(0), Capacity(N) {
    *this = std::move(RHS);
  }

  SmallVector &operator=(SmallVector &&RHS) noexcept {
    if (this == &RHS)
      return *this;

    DestroyRange(begin(), end());
    Size = 0;

    if (!RHS.IsSmall()) {
      // RHS owns a heap block: steal the pointer. No element moves, no
      // allocation. This is the cheap case and the reason SmallVector is
      // still fine to return by value from a builder function.
      if (!IsSmall())
        ::operator delete(Begin);
      Begin = RHS.Begin;
      Size = RHS.Size;
      Capacity = RHS.Capacity;
      RHS.Begin = RHS.InlineStorage();
      RHS.Size = 0;
      RHS.Capacity = N;
      return *this;
    }

    // RHS is inline: its storage is part of the RHS *object*, so it cannot be
    // stolen. Move the elements one by one.
    reserve(RHS.Size);
    for (size_t I = 0; I != RHS.Size; ++I)
      new (Begin + I) T(std::move(RHS.Begin[I]));
    Size = RHS.Size;
    RHS.clear();
    return *this;
  }

  ~SmallVector() {
    DestroyRange(begin(), end());
    if (!IsSmall())
      ::operator delete(Begin);
  }

  // --- Capacity -----------------------------------------------------------

  bool empty() const { return Size == 0; }
  size_t size() const { return Size; }
  size_t capacity() const { return Capacity; }

  /// True while the elements still live inside this object (no heap traffic).
  bool IsSmall() const { return Begin == InlineStorage(); }

  void reserve(size_t MinCapacity) {
    if (MinCapacity > Capacity)
      Grow(MinCapacity);
  }

  // --- Element access -----------------------------------------------------

  T *data() { return Begin; }
  const T *data() const { return Begin; }
  iterator begin() { return Begin; }
  iterator end() { return Begin + Size; }
  const_iterator begin() const { return Begin; }
  const_iterator end() const { return Begin + Size; }

  T &operator[](size_t I) {
    assert(I < Size && "index out of range");
    return Begin[I];
  }
  const T &operator[](size_t I) const {
    assert(I < Size && "index out of range");
    return Begin[I];
  }

  T &back() {
    assert(!empty() && "back() on empty vector");
    return Begin[Size - 1];
  }
  const T &back() const {
    assert(!empty() && "back() on empty vector");
    return Begin[Size - 1];
  }

  // --- Mutation -----------------------------------------------------------

  void push_back(const T &Elt) {
    if (Size == Capacity)
      Grow(Size + 1);
    new (Begin + Size) T(Elt);
    ++Size;
  }

  void push_back(T &&Elt) {
    if (Size == Capacity)
      Grow(Size + 1);
    new (Begin + Size) T(std::move(Elt));
    ++Size;
  }

  template <typename... ArgTypes> T &emplace_back(ArgTypes &&...Args) {
    if (Size == Capacity)
      Grow(Size + 1);
    new (Begin + Size) T(std::forward<ArgTypes>(Args)...);
    return Begin[Size++];
  }

  void pop_back() {
    assert(!empty() && "pop_back() on empty vector");
    --Size;
    Begin[Size].~T();
  }

  /// Drop every element from index \p NewSize onwards, destroying them.
  /// llvm::SmallVector spells this the same way; Module 3's dead-code pass
  /// uses it to discard statements after a `return`.
  void truncate(size_t NewSize) {
    assert(NewSize <= Size && "truncate() cannot grow the vector");
    DestroyRange(Begin + NewSize, end());
    Size = NewSize;
  }

  void clear() { truncate(0); }

private:
  /// Reallocate to at least \p MinCapacity elements, doubling to keep
  /// push_back amortized O(1).
  void Grow(size_t MinCapacity) {
    size_t NewCapacity = Capacity * 2;
    if (NewCapacity < MinCapacity)
      NewCapacity = MinCapacity;

    // Raw allocation: the elements are constructed by hand below, so we must
    // not use `new T[]` (that would value-initialize every slot).
    // ::operator new is used instead of malloc so the instrumented allocator
    // in AllocCounter.cpp observes the spill.
    T *NewBegin = static_cast<T *>(::operator new(NewCapacity * sizeof(T)));
    for (size_t I = 0; I != Size; ++I)
      new (NewBegin + I) T(std::move(Begin[I]));
    DestroyRange(Begin, Begin + Size);
    if (!IsSmall())
      ::operator delete(Begin);

    Begin = NewBegin;
    Capacity = NewCapacity;
  }

  static void DestroyRange(T *First, T *Last) {
    // Trivially destructible types (int, pointers) compile this loop away.
    if (std::is_trivially_destructible<T>::value)
      return;
    while (First != Last)
      (--Last)->~T();
  }

  T *InlineStorage() { return reinterpret_cast<T *>(Buffer); }
  const T *InlineStorage() const { return reinterpret_cast<const T *>(Buffer); }

  T *Begin;
  size_t Size;
  size_t Capacity;
  /// The whole point: sizeof(T) * N bytes of storage inside the object.
  alignas(T) char Buffer[sizeof(T) * N];
};

} // namespace workbench

#endif // WORKBENCH_SMALLVECTOR_H
