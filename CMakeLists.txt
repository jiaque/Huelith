cmake_minimum_required(VERSION 3.25)
project(Huelith VERSION 0.1.0 LANGUAGES CXX)

include(CTest)
option(HUELITH_ENABLE_ASAN "Instrument project targets with AddressSanitizer" OFF)
find_package(Qt6 6.10.3 EXACT REQUIRED COMPONENTS Widgets)

add_library(huelith_build_options INTERFACE)
target_compile_features(huelith_build_options INTERFACE cxx_std_20)
if(MSVC)
    target_compile_options(huelith_build_options INTERFACE /W4 /WX /permissive- /utf-8 /Zc:__cplusplus)
    if(HUELITH_ENABLE_ASAN)
        target_compile_options(huelith_build_options INTERFACE /fsanitize=address /Zi)
        target_link_options(huelith_build_options INTERFACE /INCREMENTAL:NO)
        get_filename_component(compiler_runtime_dir "${CMAKE_CXX_COMPILER}" DIRECTORY)
        find_file(asan_runtime NAMES clang_rt.asan_dynamic-x86_64.dll
            PATHS "${compiler_runtime_dir}" NO_DEFAULT_PATH REQUIRED)
        configure_file("${asan_runtime}" "${CMAKE_CURRENT_BINARY_DIR}/clang_rt.asan_dynamic-x86_64.dll" COPYONLY)
    endif()
else()
    target_compile_options(huelith_build_options INTERFACE -Wall -Wextra -Wpedantic -Werror)
    if(HUELITH_ENABLE_ASAN)
        target_compile_options(huelith_build_options INTERFACE -fsanitize=address -fno-omit-frame-pointer)
        target_link_options(huelith_build_options INTERFACE -fsanitize=address)
    endif()
endif()
set(CMAKE_CXX_EXTENSIONS OFF)

add_library(huelith_document src/document/canvas_spec.cpp)
target_include_directories(huelith_document PUBLIC include)
target_link_libraries(huelith_document PUBLIC huelith_build_options)

add_library(huelith_desktop
    apps/desktop/window_header.cpp
    apps/desktop/canvas_view.cpp
    apps/desktop/new_document_dialog.cpp
    apps/desktop/main_window.cpp)
target_include_directories(huelith_desktop PUBLIC apps/desktop)
target_link_libraries(huelith_desktop PUBLIC huelith_document Qt6::Widgets)

qt_add_executable(Huelith WIN32 apps/desktop/main.cpp)
target_link_libraries(Huelith PRIVATE huelith_desktop)

if(BUILD_TESTING)
    add_test(NAME app_startup COMMAND Huelith --smoke-test)
    set_tests_properties(app_startup PROPERTIES LABELS integration TIMEOUT 30
        ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
        ENVIRONMENT_MODIFICATION "PATH=path_list_prepend:$<TARGET_FILE_DIR:Qt6::Core>")
    find_package(GTest CONFIG REQUIRED)
    include(GoogleTest)
    add_executable(huelith_unit_tests tests/unit/canvas_spec_test.cpp)
    target_link_libraries(huelith_unit_tests PRIVATE huelith_document GTest::gtest_main)
    gtest_discover_tests(huelith_unit_tests PROPERTIES LABELS unit)
    add_executable(huelith_gui_tests tests/gui/main_window_test.cpp)
    target_link_libraries(huelith_gui_tests PRIVATE huelith_desktop GTest::gtest)
    gtest_discover_tests(huelith_gui_tests PROPERTIES LABELS gui
        ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
        ENVIRONMENT_MODIFICATION "PATH=path_list_prepend:$<TARGET_FILE_DIR:Qt6::Core>"
        TIMEOUT 30)
endif()

include(GNUInstallDirs)
install(TARGETS Huelith RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
qt_generate_deploy_app_script(TARGET Huelith OUTPUT_SCRIPT deploy_script NO_UNSUPPORTED_PLATFORM_ERROR)
install(SCRIPT ${deploy_script})
