cmake_minimum_required(VERSION 3.16)
project(module1-ast-ownership LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# LLVM builds with exceptions and RTTI disabled. Turning them off here is what
# turns "we follow LLVM conventions" from a claim into a compiler-enforced
# property: any `throw`, `catch`, `typeid` or `dynamic_cast` becomes a build
# error rather than a review comment.
add_library(module1-conventions INTERFACE)
target_compile_options(module1-conventions INTERFACE
  -fno-exceptions
  -fno-rtti
  -Wall
  -Wextra)
target_include_directories(module1-conventions INTERFACE include)

# AllocCounter.cpp replaces the global operator new/delete. Replacement
# operators are compiled straight into each executable instead of going
# through a static library, so there is no chance of the linker preferring
# the libc++ definition over ours.
set(MODULE1_SOURCES
  src/AST.cpp
  src/AllocCounter.cpp)

add_executable(module1-demo ${MODULE1_SOURCES} src/main.cpp)
target_link_libraries(module1-demo PRIVATE module1-conventions)

add_executable(module1-tests ${MODULE1_SOURCES} tests/Tests.cpp)
target_link_libraries(module1-tests PRIVATE module1-conventions)
target_include_directories(module1-tests PRIVATE tests)

enable_testing()
add_test(NAME module1-tests COMMAND module1-tests)
