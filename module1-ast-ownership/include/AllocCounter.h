//===- AllocCounter.h - Global allocation instrumentation ---------------===//
//
// AllocCounter.cpp replaces the global operator new / operator delete so the
// demo can *prove* that a 2-element argument list costs zero heap
// allocations, instead of asserting it in a comment.
//
// Single-threaded on purpose: plain counters, no atomics, no locks.
//
//===---------------------------------------------------------------------===//

#ifndef WORKBENCH_ALLOCCOUNTER_H
#define WORKBENCH_ALLOCCOUNTER_H

#include <cstddef>
#include <ostream>

namespace workbench {

struct AllocStats {
  unsigned long Allocations = 0;
  unsigned long Deallocations = 0;
  unsigned long long BytesAllocated = 0;
};

/// Process-wide totals so far.
AllocStats GetAllocStats();

/// Measures the heap traffic of a region of code and reports it on scope exit.
///
/// Usage:
///   {
///     AllocScope Scope(std::cout, "argument list");
///     Args.push_back(std::move(Arg));
///   } // prints "0 heap allocation(s)"
class AllocScope {
public:
  AllocScope(std::ostream &OS, const char *Label);
  ~AllocScope();

  AllocScope(const AllocScope &) = delete;
  AllocScope &operator=(const AllocScope &) = delete;

  /// Allocations observed since the scope was entered.
  unsigned long AllocationsSoFar() const;
  unsigned long long BytesSoFar() const;

private:
  std::ostream &OS;
  const char *Label;
  AllocStats Entry;
};

} // namespace workbench

#endif // WORKBENCH_ALLOCCOUNTER_H
