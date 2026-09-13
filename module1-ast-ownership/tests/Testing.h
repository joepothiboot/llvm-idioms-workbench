//===- Testing.h - 30-line assert-based test harness --------------------===//
//
// Lightweight on purpose: no Catch2, no gtest, no download step. Enough to
// report which check failed and to make CTest fail the build.
//
//===---------------------------------------------------------------------===//

#ifndef WORKBENCH_TESTING_H
#define WORKBENCH_TESTING_H

#include <cstdio>

namespace workbench {
namespace testing {

inline unsigned &FailureCount() {
  static unsigned Failures = 0;
  return Failures;
}

inline void Report(bool Ok, const char *Expr, const char *File, int Line) {
  if (Ok) {
    std::printf("  ok   %s\n", Expr);
    return;
  }
  ++FailureCount();
  std::printf("  FAIL %s (%s:%d)\n", Expr, File, Line);
}

inline int Summary(const char *Suite) {
  const unsigned Failures = FailureCount();
  std::printf("%s: %s\n", Suite, Failures == 0 ? "PASSED" : "FAILED");
  return Failures == 0 ? 0 : 1;
}

} // namespace testing
} // namespace workbench

#define CHECK(Expr)                                                            \
  ::workbench::testing::Report((Expr), #Expr, __FILE__, __LINE__)

#endif // WORKBENCH_TESTING_H
