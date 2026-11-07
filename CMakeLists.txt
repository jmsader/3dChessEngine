cmake_minimum_required(VERSION 3.14)
project(raumschach CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

option(RAUMSCHACH_SANITIZE "Build with address + undefined-behaviour sanitizers (GCC/Clang)" OFF)

# ---------------------------------------------------------------- core library
add_library(raumschach_core STATIC
  src/geometry.cpp
  src/zobrist.cpp
  src/position.cpp
  src/movegen.cpp
  src/notation.cpp
  src/rules.cpp
)
target_include_directories(raumschach_core PUBLIC include)

if(MSVC)
  target_compile_options(raumschach_core PUBLIC /W4 /permissive-)
else()
  target_compile_options(raumschach_core PUBLIC -Wall -Wextra -Wpedantic -Wshadow -Wconversion -Wno-sign-conversion)
  if(RAUMSCHACH_SANITIZE)
    target_compile_options(raumschach_core PUBLIC -fsanitize=address,undefined -fno-omit-frame-pointer)
    target_link_options(raumschach_core PUBLIC -fsanitize=address,undefined)
  endif()
endif()

# ---------------------------------------------------------------- tools
add_executable(perft tools/perft.cpp)
target_link_libraries(perft PRIVATE raumschach_core)

add_executable(selfplay tools/selfplay.cpp)
target_link_libraries(selfplay PRIVATE raumschach_core)

# ---------------------------------------------------------------- tests
enable_testing()
add_executable(unit_tests tests/unit_tests.cpp)
target_link_libraries(unit_tests PRIVATE raumschach_core)
add_test(NAME unit_tests COMMAND unit_tests)
add_test(NAME selfplay_smoke COMMAND selfplay --games 20 --seed 1 --check-all)
