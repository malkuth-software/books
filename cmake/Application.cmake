set(GAMES_RUNTIME_ASSET_DIR "" CACHE PATH "Complete verified portable runtime assets")
if(NOT EXISTS "${GAMES_RUNTIME_ASSET_DIR}/audio/portable_manifest.json")
    message(FATAL_ERROR "Prepare portable assets and set GAMES_RUNTIME_ASSET_DIR to their directory")
endif()
add_library(game_audio_adapters STATIC ${GAMES_MODULE_AUDIO}
    src/audio_loader.cpp src/pcm_player.cpp src/scene_audio.cpp src/audio.cpp)
target_include_directories(game_audio_adapters PUBLIC src)
target_link_libraries(game_audio_adapters PUBLIC GUIForms::Audio game_paths)
add_library(game_text_frames STATIC src/text_requests.cpp src/game_text.cpp)
target_include_directories(game_text_frames PUBLIC src)
target_link_libraries(game_text_frames PUBLIC GUIForms::TextMasks)
# The bookcase's wood (src/wood.cpp), the shell's own material.
add_library(game_wood STATIC src/wood.cpp)
target_include_directories(game_wood PUBLIC src)
target_link_libraries(game_wood PUBLIC GUIForms::Application)
if(GAMES_MODULE_UI)
    add_library(vendor_game_ui STATIC ${GAMES_MODULE_UI})
    set(module_link_scope PUBLIC)
else()
    add_library(vendor_game_ui INTERFACE)
    set(module_link_scope INTERFACE)
endif()
target_link_libraries(vendor_game_ui ${module_link_scope} ${GAMES_MODULE_LIBRARIES}
    game_audio_adapters game_text_frames game_wood GUIForms::Application)
target_compile_definitions(vendor_game_ui INTERFACE _USE_MATH_DEFINES)
add_library(game_ui ${GAMES_MODULE_SOURCES} "${GAMES_CATALOG_DIR}/game_registry.cpp" src/presentation.cpp
    src/collection.cpp src/suite.cpp src/text_sprites.cpp src/capsule.cpp src/help_book.cpp src/help_content.cpp
    src/settings_sheet.cpp src/suite_model.cpp src/paper_sheet.cpp)
target_include_directories(game_ui PUBLIC src)
target_link_libraries(game_ui PUBLIC game_rules vendor_game_ui paper_core game_wood)
if(NOT MSVC)
    target_compile_options(game_ui PRIVATE -Wall -Wextra)
endif()
add_executable(pocketlibrary MACOSX_BUNDLE src/main.cpp src/dogfood.cpp)
if(WIN32)
    set_target_properties(pocketlibrary PROPERTIES WIN32_EXECUTABLE TRUE)
    if(MSVC)
        target_link_options(pocketlibrary PRIVATE /ENTRY:mainCRTStartup)
    endif()
endif()
target_link_libraries(pocketlibrary PRIVATE game_ui)
set_target_properties(pocketlibrary PROPERTIES
    MACOSX_BUNDLE_BUNDLE_NAME "Pocket Library"
    MACOSX_BUNDLE_GUI_IDENTIFIER "org.rainstar.pocketlibrary"
    MACOSX_BUNDLE_BUNDLE_VERSION "${PROJECT_VERSION}"
    MACOSX_BUNDLE_SHORT_VERSION_STRING "${PROJECT_VERSION}")
if(APPLE)
    set(library_resources "$<TARGET_BUNDLE_DIR:pocketlibrary>/Contents/Resources")
    set(library_frameworks "$<TARGET_BUNDLE_DIR:pocketlibrary>/Contents/Frameworks")
    set_target_properties(pocketlibrary PROPERTIES BUILD_RPATH "@executable_path/../Frameworks")
    set_source_files_properties(assets/PocketLibrary.icns PROPERTIES MACOSX_PACKAGE_LOCATION "Resources")
    target_sources(pocketlibrary PRIVATE assets/PocketLibrary.icns)
    set_target_properties(pocketlibrary PROPERTIES MACOSX_BUNDLE_ICON_FILE "PocketLibrary.icns")
else()
    set(library_resources "$<TARGET_FILE_DIR:pocketlibrary>")
    set(library_frameworks "$<TARGET_FILE_DIR:pocketlibrary>")
    if(UNIX)
        set_target_properties(pocketlibrary PROPERTIES BUILD_RPATH "$ORIGIN")
    endif()
endif()
add_custom_command(TARGET pocketlibrary POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory "${library_frameworks}"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different "$<TARGET_FILE:GUIForms::Application>" "${library_frameworks}"
    COMMAND ${CMAKE_COMMAND} -E copy_directory "${GAMES_RUNTIME_ASSET_DIR}" "${library_resources}/assets"
    COMMAND ${CMAKE_COMMAND} -E copy_directory "${GUIForms_FONT_DIR}" "${library_resources}/fonts"
    VERBATIM)
if(UNIX)
    add_custom_command(TARGET pocketlibrary POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different "$<TARGET_FILE:GUIForms::Application>"
            "${library_frameworks}/$<TARGET_SONAME_FILE_NAME:GUIForms::Application>"
        VERBATIM)
endif()
add_executable(audio_policy_tests tests/audio_policy_tests.cpp)
target_link_libraries(audio_policy_tests PRIVATE game_audio_adapters)
add_test(NAME audio_policy COMMAND audio_policy_tests)
