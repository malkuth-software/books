cmake_minimum_required(VERSION 3.25)
set(GAMES_BASE_VERSION "0.2.0")
set(GAMES_RELEASE_VERSION "$ENV{GAMES_RELEASE_VERSION}" CACHE STRING "Release version from the tested publication workflow")
if(GAMES_RELEASE_VERSION STREQUAL "")
  set(GAMES_RELEASE_VERSION "${GAMES_BASE_VERSION}")
endif()
if(NOT GAMES_RELEASE_VERSION MATCHES "^[0-9]+\\.[0-9]+\\.[0-9]+$")
  message(FATAL_ERROR "GAMES_RELEASE_VERSION must be a numeric major.minor.patch")
endif()
project(PocketLibrary VERSION ${GAMES_RELEASE_VERSION} LANGUAGES C CXX)
# Every program in one folder on Windows (GUI.Forms is one module and ships no DLL; any
# other DLL a build makes is found beside the programs).
if(WIN32)
    set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}")
endif()
set(GAMES_RUNTIME_ASSET_DIR "" CACHE PATH "Complete verified portable runtime assets")
option(GAMES_BUILD_APPLICATION "Build the complete graphical library and UI tests" ON)
# The capsule title loads Barlow Condensed through GUI.Forms' app-font API (claude/app-fonts,
# 2558e326c); turn it off only to build against a GUI.Forms without that API.
option(GAMES_GUI_FORMS_APP_FONTS "Load the capsule title's face through GUI.Forms' app-font API" ON)
enable_testing()
add_executable(dogfood_tests tests/dogfood_tests.cpp src/dogfood.cpp)
target_include_directories(dogfood_tests PRIVATE src)
target_compile_features(dogfood_tests PRIVATE cxx_std_20)
add_test(NAME dogfood COMMAND dogfood_tests)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
  set(CMAKE_POSITION_INDEPENDENT_CODE ON)
endif()
# GUI.Forms on Linux builds against LLVM's libc++ 22 and libc++abi (cp33; owner, 2026-11-15),
# and an application that links it must use the same C++ library: gui_forms/cmake/llvm22.cmake
# passes -stdlib=libc++. A tree that would compile against libstdc++ is refused here, as
# GUI.Forms' own configure refuses it, rather than at the first link or library difference.
# A shipped program also has no native library dependency (owner, 2026-11-15): the toolchain
# links libc++, libc++abi and libunwind statically (-nostdlib++ and the archives last on every
# C++ link line, GUI.Forms 39d2ea6b0), and a tree whose link line would take them shared is
# refused too. tools/package_linux.py checks the result with readelf and ldd.
if(GAMES_BUILD_APPLICATION AND CMAKE_SYSTEM_NAME STREQUAL "Linux")
  include(CheckCXXSourceCompiles)
  check_cxx_source_compiles([=[
#include <version>
#if !defined(_LIBCPP_VERSION) || _LIBCPP_VERSION < 220000 || _LIBCPP_VERSION >= 230000
#error "not libc++ 22"
#endif
int main() { return 0; }
]=] GAMES_LINUX_LIBCXX_22)
  string(FIND "${CMAKE_CXX_STANDARD_LIBRARIES}" "-Wl,-Bstatic -lc++ -lc++abi -lunwind" GAMES_STATIC_CXX_RUNTIME)
  if(GAMES_STATIC_CXX_RUNTIME EQUAL -1)
    message(FATAL_ERROR "Pocket Library on Linux links libc++, libc++abi and libunwind statically: configure "
                        "with gui_forms/cmake/llvm22.cmake from a GUI.Forms that carries the static link "
                        "(claude/cp33-libcxx-static) and install libunwind-22-dev")
  endif()
  if(NOT GAMES_LINUX_LIBCXX_22)
    # Not cached as a failure: the next configure checks again.
    unset(GAMES_LINUX_LIBCXX_22 CACHE)
    message(FATAL_ERROR "Pocket Library on Linux builds against libc++ 22 and libc++abi, as GUI.Forms does: "
                        "configure with gui_forms/cmake/llvm22.cmake and install libc++-22-dev and "
                        "libc++abi-22-dev")
  endif()
endif()
if(GAMES_BUILD_APPLICATION AND GUIForms_DIR)
  # An installed GUI.Forms built with every feature (tools/build_sdk.py
  # --features all, as the local lockstep install is) replaces the source
  # build: the text masks and the loop transport are its components. The page
  # compositor sets type with STXlib's glyph engine (stxlib::algorithms), which
  # the package finds beside itself (find_dependency(stxlib)); GUI.Forms has
  # no FreeType or HarfBuzz since 2026-11-16.
  find_package(GUIForms 0.6 CONFIG REQUIRED
    COMPONENTS Application Audio AudioLoopTransport TextMasks)
elseif(GAMES_BUILD_APPLICATION)
  set(GAMES_TOOLKIT_SOURCE_DIR "" CACHE PATH "Reviewed GUI.Forms source for development application builds")
  if(NOT EXISTS "${GAMES_TOOLKIT_SOURCE_DIR}/include/gui_forms/text_mask.hpp")
    message(FATAL_ERROR "Set GAMES_TOOLKIT_SOURCE_DIR to the reviewed GUI.Forms source, or GUIForms_DIR to the package of a GUI.Forms installed with --features all; see docs/BUILDING.md.")
  endif()
  set(GUI_FORMS_BUILD_GALLERY OFF CACHE BOOL "" FORCE)
  set(GUI_FORMS_BUILD_TESTS OFF CACHE BOOL "" FORCE)
  set(GUI_FORMS_BUILD_AUDIO ON CACHE BOOL "" FORCE)
  set(GUI_FORMS_BUILD_AUDIO_LOOP_TRANSPORT ON CACHE BOOL "" FORCE)
  # GUI.Forms' own Vorbis decoder; forced so an older build folder cannot keep stb.
  set(GUI_FORMS_AUDIO_VORBIS_BACKEND stx CACHE STRING "" FORCE)
  set(GUI_FORMS_BUILD_TEXT_MASKS ON CACHE BOOL "" FORCE)
  # controls/command-capsule.control is compiled at build time (gui_forms_add_control).
  set(GUI_FORMS_BUILD_FORM_COMPILER ON CACHE BOOL "" FORCE)
  # Every host draws with GUI.Forms' CPU Skia (built from the pinned stx); the Win32
  # host requires it since stage SK3.
  set(GUI_FORMS_ENABLE_SKIA ON CACHE BOOL "" FORCE)
  if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    set(GUI_FORMS_ENABLE_LINUX_HOST ON CACHE BOOL "" FORCE)
  endif()
  add_subdirectory("${GAMES_TOOLKIT_SOURCE_DIR}" toolkit EXCLUDE_FROM_ALL)
  # gui_forms_add_theme reads GUI_FORMS_THEME_EMBED_SCRIPT, which the toolkit sets only in
  # its own directory scope (and its module will not run twice), so it is set here too.
  set(GUI_FORMS_THEME_EMBED_SCRIPT "${GAMES_TOOLKIT_SOURCE_DIR}/cmake/GUIFormsThemeEmbed.cmake")
  set(GUIForms_FONT_DIR "${GAMES_TOOLKIT_SOURCE_DIR}/assets/fonts")
  # The toolkit imports STXlib from its stx package in its own directory scope,
  # where imported targets stay; the page compositor links stxlib::algorithms, so
  # the same package is found again here, from the prefix the toolkit chose.
  get_directory_property(GAMES_STX_PREFIX_DIR DIRECTORY "${GAMES_TOOLKIT_SOURCE_DIR}"
    DEFINITION GUI_FORMS_STX_PREFIX_DIR)
  if(NOT TARGET stxlib::algorithms)
    find_package(stxlib CONFIG REQUIRED NO_DEFAULT_PATH PATHS "${GAMES_STX_PREFIX_DIR}")
  endif()
endif()
add_library(game_paths src/runtime_paths.cpp src/game_data.cpp)
target_include_directories(game_paths PUBLIC src)
# The masters (src/suite_settings.*), without pixels.
add_library(game_rules src/suite_settings.cpp)
target_include_directories(game_rules PUBLIC src)
target_link_libraries(game_rules PUBLIC game_paths)
find_package(Threads REQUIRED)
find_package(Python3 REQUIRED COMPONENTS Interpreter)
# The library's module (books/<id>/GAME.json) and the shared engines it declares
# (shared/<id>/ENGINE.json), discovered and checked by tools/game_catalog.py.
set(GAMES_EXTRA_GAME_DIRS "" CACHE STRING "Additional module folders to test before copying into books/")
file(GLOB module_manifests CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/books/*/GAME.json"
  "${CMAKE_CURRENT_SOURCE_DIR}/shared/*/ENGINE.json")
set(catalog_arguments)
foreach(directory IN LISTS GAMES_EXTRA_GAME_DIRS)
  list(APPEND catalog_arguments --extra "${directory}")
  set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${directory}/GAME.json")
endforeach()
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS ${module_manifests} "${CMAKE_CURRENT_SOURCE_DIR}/tools/game_catalog.py")
set(GAMES_CATALOG_DIR "${CMAKE_CURRENT_BINARY_DIR}/catalog")
execute_process(COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/tools/game_catalog.py"
  --root "${CMAKE_CURRENT_SOURCE_DIR}" --generate "${GAMES_CATALOG_DIR}" ${catalog_arguments}
  RESULT_VARIABLE catalog_result OUTPUT_VARIABLE catalog_output ERROR_VARIABLE catalog_error)
if(NOT catalog_result EQUAL 0)
  message(FATAL_ERROR "Module catalog: ${catalog_error}")
endif()
include_directories("${GAMES_CATALOG_DIR}")
include("${GAMES_CATALOG_DIR}/game_modules.cmake")
if(GAMES_BUILD_APPLICATION)
  include(cmake/Application.cmake)
endif()

# Keep assertions active in every registered test.
get_property(all_targets DIRECTORY PROPERTY BUILDSYSTEM_TARGETS)
foreach(test_target IN LISTS all_targets)
  if(test_target MATCHES "_tests$")
    if(MSVC)
      target_compile_options(${test_target} PRIVATE /UNDEBUG)
    else()
      target_compile_options(${test_target} PRIVATE -UNDEBUG)
    endif()
  endif()
endforeach()
