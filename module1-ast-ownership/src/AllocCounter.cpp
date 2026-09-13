//===- AllocCounter.cpp - Replacement global operator new/delete --------===//
//
// A replacement global operator new must live in exactly one translation unit
// of the program. Linking this file in is what makes the counters authoritative
// for *every* allocation, including the ones std::string and iostreams make.
//
// LLVM convention, no exceptions: a failed allocation aborts instead of
// throwing std::bad_alloc. LLVM does the same thing (report_bad_alloc_error),
// because the whole codebase is compiled with -fno-exceptions.
//
//===---------------------------------------------------------------------===//

#include "AllocCounter.h"

#include <cstdlib>
#include <new>

namespace {

unsigned long NumAllocations = 0;
unsigned long NumDeallocations = 0;
unsigned long long BytesAllocated = 0;

} // namespace

namespace workbench {

AllocStats GetAllocStats() {
  AllocStats Stats;
  Stats.Allocations = NumAllocations;
  Stats.Deallocations = NumDeallocations;
  Stats.BytesAllocated = BytesAllocated;
  return Stats;
}

AllocScope::AllocScope(std::ostream &OS, const char *Label)
    : OS(OS), Label(Label), Entry(GetAllocStats()) {}

unsigned long AllocScope::AllocationsSoFar() const {
  return NumAllocations - Entry.Allocations;
}

unsigned long long AllocScope::BytesSoFar() const {
  return BytesAllocated - Entry.BytesAllocated;
}

AllocScope::~AllocScope() {
  // Snapshot before printing: the stream itself may allocate.
  const unsigned long Allocations = AllocationsSoFar();
  const unsigned long long Bytes = BytesSoFar();
  OS << "  [heap] " << Label << ": " << Allocations << " allocation"
     << (Allocations == 1 ? "" : "s") << ", " << Bytes << " byte"
     << (Bytes == 1 ? "" : "s") << "\n";
}

} // namespace workbench

void *operator new(std::size_t Size) {
  if (Size == 0)
    Size = 1; // Distinct pointers for zero-sized requests.
  void *Ptr = std::malloc(Size);
  if (!Ptr)
    std::abort();
  ++NumAllocations;
  BytesAllocated += Size;
  return Ptr;
}

void operator delete(void *Ptr) noexcept {
  if (!Ptr)
    return;
  ++NumDeallocations;
  std::free(Ptr);
}

// Sized and array forms must be replaced together with the scalar forms,
// otherwise a default operator delete could hand our malloc'd pointer to a
// different deallocator.
void operator delete(void *Ptr, std::size_t) noexcept { ::operator delete(Ptr); }
void *operator new[](std::size_t Size) { return ::operator new(Size); }
void operator delete[](void *Ptr) noexcept { ::operator delete(Ptr); }
void operator delete[](void *Ptr, std::size_t) noexcept {
  ::operator delete(Ptr);
}
void *operator new(std::size_t Size, const std::nothrow_t &) noexcept {
  void *Ptr = std::malloc(Size ? Size : 1);
  if (!Ptr)
    return nullptr;
  ++NumAllocations;
  BytesAllocated += Size;
  return Ptr;
}

void *operator new[](std::size_t Size, const std::nothrow_t &Tag) noexcept {
  return ::operator new(Size, Tag);
}

void operator delete(void *Ptr, const std::nothrow_t &) noexcept {
  ::operator delete(Ptr);
}

void operator delete[](void *Ptr, const std::nothrow_t &) noexcept {
  ::operator delete(Ptr);
}
