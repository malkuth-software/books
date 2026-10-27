# The game's own build: the library without pixels and its tests, with nothing but a
# C++20 compiler. The pages and the window are built by the repository's application
# build (cmake/Application.cmake), which links the toolkit's HarfBuzz and FreeType.
#
#   cmake -S . -B build && cmake --build build -j 2 && ctest --test-dir build --output-on-failure
cmake_minimum_required(VERSION 3.25)
project(PocketLibrary VERSION 0.1.0 LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()

# The kit lives in the repository's new-games folder. In the template that is two
# levels up; in games/<game> it is ../../new-games/kit.
set(PL_KIT_DIR "" CACHE PATH "The repository's new-games/kit directory")
if(NOT PL_KIT_DIR)
  foreach(candidate "${CMAKE_CURRENT_SOURCE_DIR}/../kit" "${CMAKE_CURRENT_SOURCE_DIR}/../../new-games/kit")
    if(EXISTS "${candidate}/headless_text.hpp")
      get_filename_component(PL_KIT_DIR "${candidate}" ABSOLUTE)
      break()
    endif()
  endforeach()
endif()
if(NOT EXISTS "${PL_KIT_DIR}/headless_text.hpp")
  message(FATAL_ERROR "Set PL_KIT_DIR to the repository's new-games/kit directory")
endif()

# PlaySuite's game API for saves and assets (src/game_data.hpp), which a game uses
# instead of opening files itself.
get_filename_component(PL_PLAYSUITE_SRC "${PL_KIT_DIR}/../../src" ABSOLUTE)
if(NOT TARGET game_paths)
  add_library(game_paths STATIC "${PL_PLAYSUITE_SRC}/runtime_paths.cpp" "${PL_PLAYSUITE_SRC}/game_data.cpp")
  target_include_directories(game_paths PUBLIC "${PL_PLAYSUITE_SRC}")
endif()

# The library without pixels: the book model, the library file and its coder, the
# shelf and the bookmarks. This is what PlaySuite's portable-core profile builds; the
# pages are set and printed only in the application (build.cmake, pl_page).
add_library(pl_core STATIC src/book.cpp src/container.cpp src/library.cpp src/lz_coder.cpp src/sha256.cpp src/text_pack.cpp)
target_include_directories(pl_core PUBLIC src)
target_link_libraries(pl_core PUBLIC game_paths)
if(NOT MSVC)
  target_compile_options(pl_core PRIVATE -Wall -Wextra -ffp-contract=off)
endif()

enable_testing()
add_executable(pl_library_tests tests/library_tests.cpp)
target_link_libraries(pl_library_tests PRIVATE pl_core)
add_test(NAME pl_library COMMAND pl_library_tests)
