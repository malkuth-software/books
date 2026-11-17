# Included by the generated registry, with GAME_MODULE_DIR set to this folder.
#
# pl_core is the library without pixels: the book model, the library file and its
# coder, the shelf and the bookmarks. It builds anywhere, with the portable core.
add_library(pl_core STATIC
  "${GAME_MODULE_DIR}/src/book.cpp"
  "${GAME_MODULE_DIR}/src/container.cpp"
  "${GAME_MODULE_DIR}/src/library.cpp"
  "${GAME_MODULE_DIR}/src/lz_coder.cpp"
  "${GAME_MODULE_DIR}/src/sha256.cpp"
  "${GAME_MODULE_DIR}/src/text_pack.cpp")
target_include_directories(pl_core PUBLIC "${GAME_MODULE_DIR}/src")
# Saves and assets come through PlaySuite (src/game_data.hpp).
target_link_libraries(pl_core PUBLIC game_paths)
if(NOT MSVC)
  target_compile_options(pl_core PRIVATE -Wall -Wextra -ffp-contract=off)
endif()
add_executable(pocketlibrary_library_tests "${GAME_MODULE_DIR}/tests/library_tests.cpp")
target_link_libraries(pocketlibrary_library_tests PRIVATE pl_core)
add_test(NAME pocketlibrary_library COMMAND pocketlibrary_library_tests)

if(GAMES_BUILD_APPLICATION)
  # pl_page sets and prints the pages: the Pocket Bible's compositor on STXlib's
  # glyph engine (Typeface, Face and Shaper of stxlib::glyphs, in
  # stxlib::algorithms: the text engine GUI.Forms itself draws with), and the
  # paper engine's India paper. No FreeType or HarfBuzz.
  add_library(pl_page STATIC
    "${GAME_MODULE_DIR}/src/page/font_set.cpp"
    "${GAME_MODULE_DIR}/src/page/text_layout.cpp"
    "${GAME_MODULE_DIR}/src/page/page_raster.cpp"
    "${GAME_MODULE_DIR}/src/page/hit_map.cpp"
    "${GAME_MODULE_DIR}/src/book_layout.cpp")
  target_include_directories(pl_page PUBLIC "${GAME_MODULE_DIR}/src")
  target_link_libraries(pl_page PUBLIC pl_core paper_core)
  # Both an installed GUI.Forms (its package finds STXlib) and a source build
  # (cmake/Stx.cmake imports the stx package's STXlib) define stxlib::algorithms.
  if(NOT TARGET stxlib::algorithms)
    message(FATAL_ERROR "pl_page needs STXlib's stxlib::algorithms, which GUI.Forms brings: "
                        "configure with GUIForms_DIR or GAMES_TOOLKIT_SOURCE_DIR")
  endif()
  target_link_libraries(pl_page PRIVATE stxlib::algorithms)
  if(NOT MSVC)
    target_compile_options(pl_page PRIVATE -Wall -Wextra -ffp-contract=off)
  endif()

  # The faces on STXlib's glyph engine, read from the library's own fonts.
  add_executable(pocketlibrary_font_set_tests "${GAME_MODULE_DIR}/tests/font_set_tests.cpp")
  target_link_libraries(pocketlibrary_font_set_tests PRIVATE pl_page)
  target_compile_definitions(pocketlibrary_font_set_tests PRIVATE
    PL_FONT_DIR="${GAME_MODULE_DIR}/assets/fonts")
  add_test(NAME pocketlibrary_font_set COMMAND pocketlibrary_font_set_tests)

  add_executable(pocketlibrary_contract_tests "${GAME_MODULE_DIR}/tests/view_contract_tests.cpp")
  target_include_directories(pocketlibrary_contract_tests PRIVATE "${CMAKE_SOURCE_DIR}/new-games/kit")
  target_link_libraries(pocketlibrary_contract_tests PRIVATE vendor_game_ui)
  add_test(NAME pocketlibrary_view_contract COMMAND pocketlibrary_contract_tests)
  set_tests_properties(pocketlibrary_view_contract PROPERTIES TIMEOUT 90
    ENVIRONMENT "GAMES_ASSET_DIR=${GAMES_RUNTIME_ASSET_DIR}")
endif()
