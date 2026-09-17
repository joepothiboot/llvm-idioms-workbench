cmake_minimum_required(VERSION 3.16)
project(module2-custom-rtti LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# -fno-rtti is load-bearing in this module, not decoration: it means the
# compiler will reject `dynamic_cast` and `typeid` outright, so the claim
# "this traversal uses no language RTTI" is mechanically verified.
add_library(module2-conventions INTERFACE)
target_compile_options(module2-conventions INTERFACE
  -fno-exceptions
  -fno-rtti
  -Wall
  -Wextra)
target_include_directories(module2-conventions INTERFACE include)

add_library(module2-ast STATIC src/Visitor.cpp)
target_link_libraries(module2-ast PUBLIC module2-conventions)

add_executable(module2-demo src/main.cpp)
target_link_libraries(module2-demo PRIVATE module2-ast)

add_executable(module2-tests tests/Tests.cpp)
target_link_libraries(module2-tests PRIVATE module2-ast)
target_include_directories(module2-tests PRIVATE tests)

enable_testing()
add_test(NAME module2-tests COMMAND module2-tests)
