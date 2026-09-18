cmake_minimum_required(VERSION 3.16)
project(module3-pass-manager LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_library(module3-conventions INTERFACE)
target_compile_options(module3-conventions INTERFACE
  -fno-exceptions
  -fno-rtti
  -Wall
  -Wextra)
target_include_directories(module3-conventions INTERFACE include)

# AllocCounter.cpp replaces the global operator new/delete, so it is compiled
# directly into each executable rather than going through a static library.
set(MODULE3_SOURCES
  src/ASTPrinter.cpp
  src/AllocCounter.cpp
  src/ConstantFoldingPass.cpp
  src/DeadCodeEliminationPass.cpp
  src/PassManager.cpp)

add_executable(module3-demo ${MODULE3_SOURCES} src/main.cpp)
target_link_libraries(module3-demo PRIVATE module3-conventions)

add_executable(module3-tests ${MODULE3_SOURCES} tests/Tests.cpp)
target_link_libraries(module3-tests PRIVATE module3-conventions)
target_include_directories(module3-tests PRIVATE tests)

enable_testing()
add_test(NAME module3-tests COMMAND module3-tests)
