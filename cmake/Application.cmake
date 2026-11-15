set(GAMES_RUNTIME_ASSET_DIR "" CACHE PATH "Complete verified portable runtime assets")
if(NOT EXISTS "${GAMES_RUNTIME_ASSET_DIR}/audio/portable_manifest.json")
    message(FATAL_ERROR "Prepare portable assets and set GAMES_RUNTIME_ASSET_DIR to their directory")
endif()
add_library(game_audio_adapters STATIC ${GAMES_MODULE_AUDIO}
    src/audio_loader.cpp src/pcm_player.cpp src/scene_audio.cpp src/audio.cpp)
target_include_directories(game_audio_adapters PUBLIC src)
target_link_libraries(game_audio_adapters PUBLIC GUIForms::Audio game_paths)
# An installed GUI.Forms ships the loop transport (src/pcm_player.cpp) as its
# own component; a source build compiles it into GUIForms::Audio.
if(TARGET GUIForms::AudioLoopTransport)
    target_link_libraries(game_audio_adapters PUBLIC GUIForms::AudioLoopTransport)
endif()
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
    src/collection.cpp src/suite.cpp src/capsule.cpp src/capsule_title_face.cpp src/help_book.cpp src/help_content.cpp
    src/settings_sheet.cpp src/suite_model.cpp src/paper_sheet.cpp)
target_include_directories(game_ui PUBLIC src)
# The capsule is a GUI.Forms composite, as PlaySuite's: its class derives from the one the form
# compiler generates from controls/command-capsule.control. capsule.hpp includes the generated
# header, so the directory is public for every target that includes it.
gui_forms_add_control(game_ui controls/command-capsule.control CLASS CommandCapsuleControl NAMESPACE games)
target_include_directories(game_ui PUBLIC "${CMAKE_CURRENT_BINARY_DIR}/gui_forms_controls")
# The capsule title's app-loaded face (src/capsule_title_face.cpp) uses GUI.Forms' app-font
# API; public, so the tests see the same choice.
if(GAMES_GUI_FORMS_APP_FONTS)
    target_compile_definitions(game_ui PUBLIC GAMES_GUI_FORMS_APP_FONTS=1)
endif()
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
# The look is PlaySuite's theme package themes/playsuite (shared with PlaySuite, written by
# tools/make_theme.py there and copied here as the shell is).
gui_forms_add_theme(pocketlibrary themes/playsuite FUNCTION playsuite_theme NAMESPACE games)
set_target_properties(pocketlibrary PROPERTIES
    MACOSX_BUNDLE_BUNDLE_NAME "Pocket Library"
    # Earlier releases' identifier, kept so macOS treats a new version as the
    # same application (and tools/package_macos.py's pkg identifier with it);
    # the vendor shown is Malkuth Software.
    MACOSX_BUNDLE_GUI_IDENTIFIER "org.rainstar.pocketlibrary"
    MACOSX_BUNDLE_COPYRIGHT "Malkuth Software"
    MACOSX_BUNDLE_BUNDLE_VERSION "${PROJECT_VERSION}"
    MACOSX_BUNDLE_SHORT_VERSION_STRING "${PROJECT_VERSION}")
if(APPLE)
    set(library_resources "$<TARGET_BUNDLE_DIR:pocketlibrary>/Contents/Resources")
    set_source_files_properties(assets/PocketLibrary.icns PROPERTIES MACOSX_PACKAGE_LOCATION "Resources")
    target_sources(pocketlibrary PRIVATE assets/PocketLibrary.icns)
    set_target_properties(pocketlibrary PROPERTIES MACOSX_BUNDLE_ICON_FILE "PocketLibrary.icns")
else()
    set(library_resources "$<TARGET_FILE_DIR:pocketlibrary>")
endif()
# GUI.Forms is one module (static archives linked into the program): no GUI.Forms library is
# copied beside the executable and no rpath points at one.
add_custom_command(TARGET pocketlibrary POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory "${GAMES_RUNTIME_ASSET_DIR}" "${library_resources}/assets"
    COMMAND ${CMAKE_COMMAND} -E copy_directory "${GUIForms_FONT_DIR}" "${library_resources}/fonts"
    VERBATIM)
add_executable(audio_policy_tests tests/audio_policy_tests.cpp)
target_link_libraries(audio_policy_tests PRIVATE game_audio_adapters)
add_test(NAME audio_policy COMMAND audio_policy_tests)
# PlaySuite's theme package compiles over Windows Professional with no finding but W201
# and W402; the shell it then builds reads the runtime assets (fonts, help text).
add_executable(playsuite_theme_tests tests/playsuite_theme_tests.cpp)
target_link_libraries(playsuite_theme_tests PRIVATE game_ui)
gui_forms_add_theme(playsuite_theme_tests themes/playsuite FUNCTION playsuite_theme NAMESPACE games)
add_test(NAME playsuite_theme COMMAND playsuite_theme_tests)
set_tests_properties(playsuite_theme PROPERTIES ENVIRONMENT "GAMES_ASSET_DIR=${GAMES_RUNTIME_ASSET_DIR}")
# The view's routed handlers over GUI.Forms' interaction raisers (src/routed_input.hpp).
add_executable(routed_input_tests tests/routed_input_tests.cpp)
target_link_libraries(routed_input_tests PRIVATE game_ui)
add_test(NAME routed_input COMMAND routed_input_tests)
