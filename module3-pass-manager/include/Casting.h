//===- Casting.h - Minimal llvm/Support/Casting.h ------------------------===//
//
// isa<>, cast<>, dyn_cast<> built on a `Kind` discriminator and a static
// `classof` hook, with no language RTTI involved. This is a teaching-sized
// version of llvm/Support/Casting.h; the contracts match.
//
//   isa<T>(P)      -> bool. Is the object pointed to by P a T?
//   cast<T>(P)     -> T*.   ASSERTS that it is. Never returns null.
//   dyn_cast<T>(P) -> T*.   Returns null if it is not.
//
// The names are lower_case to match LLVM exactly: these are intended to read
// like language-level casts at the use site.
//
//===---------------------------------------------------------------------===//

#ifndef WORKBENCH_CASTING_H
#define WORKBENCH_CASTING_H

#include <cassert>
#include <type_traits>

namespace workbench {

namespace detail {

/// Propagates const-ness from the source pointee to the result, so that
/// `dyn_cast<NumberExprAST>(const ExprAST *)` yields `const NumberExprAST *`
/// and cannot be used to launder away const. LLVM calls this `cast_retty`.
template <typename To, typename From> struct CastReturnType {
  using type = To *;
};

template <typename To, typename From> struct CastReturnType<To, const From> {
  using type = const To *;
};

} // namespace detail

template <typename To, typename From>
using CastReturnTypeT = typename detail::CastReturnType<To, From>::type;

/// Is \p Val a \p To? Requires a non-null pointer: asking about nothing is a
/// programming error, not a "no". Use isa_and_present<> when null is a
/// legitimate input.
template <typename To, typename From> inline bool isa(const From *Val) {
  assert(Val && "isa<> used on a null pointer");
  return To::classof(Val);
}

/// isa<>, but a null pointer answers false instead of asserting.
template <typename To, typename From>
inline bool isa_and_present(const From *Val) {
  return Val && To::classof(Val);
}

/// Checked downcast. The caller is claiming to already know the dynamic type;
/// being wrong is a bug, so it asserts in debug builds and compiles down to a
/// bare static_cast in release builds. It NEVER returns null.
template <typename To, typename From>
inline CastReturnTypeT<To, From> cast(From *Val) {
  assert(Val && "cast<> used on a null pointer");
  assert(isa<To>(Val) && "cast<> argument of incompatible type!");
  return static_cast<CastReturnTypeT<To, From>>(Val);
}

/// cast<>, but a null input passes straight through as null.
template <typename To, typename From>
inline CastReturnTypeT<To, From> cast_if_present(From *Val) {
  if (!Val)
    return nullptr;
  return cast<To>(Val);
}

/// Queried downcast: returns null when \p Val is not a \p To. This is the one
/// to use when the type is genuinely unknown, e.g. while walking a tree.
template <typename To, typename From>
inline CastReturnTypeT<To, From> dyn_cast(From *Val) {
  assert(Val && "dyn_cast<> used on a null pointer");
  if (!To::classof(Val))
    return nullptr;
  return static_cast<CastReturnTypeT<To, From>>(Val);
}

/// dyn_cast<>, but tolerates a null input (returns null).
template <typename To, typename From>
inline CastReturnTypeT<To, From> dyn_cast_if_present(From *Val) {
  if (!Val || !To::classof(Val))
    return nullptr;
  return static_cast<CastReturnTypeT<To, From>>(Val);
}

} // namespace workbench

#endif // WORKBENCH_CASTING_H
