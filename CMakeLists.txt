cmake_minimum_required(VERSION 3.25)
project(Huelith VERSION 0.1.0 LANGUAGES CXX)

include(CTest)
option(HUELITH_ENABLE_ASAN "Instrument project targets with AddressSanitizer" OFF)
find_package(Qt6 6.10.3 EXACT REQUIRED COMPONENTS Widgets Svg)

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

find_package(libraw CONFIG REQUIRED)
set(TIFF_INCLUDE_DIR "${VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}/include" CACHE PATH "Pinned TIFF headers" FORCE)
set(TIFF_LIBRARY_RELEASE "${VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}/lib/tiff.lib" CACHE FILEPATH "Pinned TIFF library" FORCE)
set(TIFF_LIBRARY_DEBUG "${VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}/debug/lib/tiffd.lib" CACHE FILEPATH "Pinned TIFF debug library" FORCE)
set(CMAKE_FIND_PACKAGE_PREFER_CONFIG FALSE)
set(CMAKE_DISABLE_FIND_PACKAGE_Tiff TRUE)
find_package(TIFF MODULE REQUIRED)
set(HUELITH_ORT_DIR "${CMAKE_CURRENT_SOURCE_DIR}/build/dependencies/onnxruntime-directml" CACHE PATH "Pinned ONNX Runtime DirectML native SDK")
add_library(huelith_ort SHARED IMPORTED)
set_target_properties(huelith_ort PROPERTIES
    IMPORTED_IMPLIB "${HUELITH_ORT_DIR}/onnxruntime.lib"
    IMPORTED_LOCATION "${HUELITH_ORT_DIR}/onnxruntime.dll"
    INTERFACE_INCLUDE_DIRECTORIES "${HUELITH_ORT_DIR}")
add_library(huelith_editing src/editing/project.cpp src/editing/project_io.cpp src/editing/blend.cpp src/editing/photo_operations.cpp src/editing/raw_develop.cpp src/editing/raw_cache.cpp src/editing/tiff_io.cpp src/editing/tone.cpp src/editing/export_queue.cpp)
target_include_directories(huelith_editing PUBLIC include)
target_sources(huelith_editing PRIVATE src/editing/raw_adjustments.cpp)
target_link_libraries(huelith_editing PUBLIC huelith_build_options Qt6::Gui PRIVATE libraw::raw_r TIFF::TIFF huelith_ort)
target_compile_definitions(huelith_editing PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)

# M0 isolated research prototypes. These do not add M1 product editing features.
add_library(huelith_validation src/validation/state.cpp src/validation/image_math.cpp src/validation/artistic_blend.cpp)
target_include_directories(huelith_validation PUBLIC include)
target_link_libraries(huelith_validation PUBLIC huelith_build_options Qt6::Gui)
if(WIN32)
    target_sources(huelith_validation PRIVATE src/validation/gpu_probe.cpp)
    target_link_libraries(huelith_validation PRIVATE d3d12 dxgi d3dcompiler)
    add_executable(huelith_m0_environment EXCLUDE_FROM_ALL tools/m0_environment.cpp)
    target_link_libraries(huelith_m0_environment PRIVATE huelith_build_options Qt6::Gui gdi32)
endif()

add_library(huelith_desktop
    apps/desktop/application_icon.cpp
    apps/desktop/window_header.cpp
    apps/desktop/canvas_view.cpp
    apps/desktop/canvas_photo.cpp
    apps/desktop/photo_tools.cpp
    apps/desktop/raw_dialog.cpp
    apps/desktop/tone_panel.cpp
    apps/desktop/tone_workflow.cpp
    apps/desktop/editor_controller.cpp
    apps/desktop/editor_workflows.cpp
    apps/desktop/export_dialog.cpp
    apps/desktop/new_document_dialog.cpp
    apps/desktop/validation_session.cpp
    apps/desktop/main_window.cpp)
target_include_directories(huelith_desktop PUBLIC apps/desktop)
target_link_libraries(huelith_desktop PUBLIC huelith_document huelith_editing huelith_validation Qt6::Widgets Qt6::Svg)
if(WIN32)
    target_link_libraries(huelith_desktop PRIVATE gdi32)
endif()
qt_add_resources(huelith_desktop huelith_assets PREFIX "/huelith"
    BASE apps/desktop/assets FILES apps/desktop/assets/huelith.svg)

qt_add_executable(Huelith WIN32 apps/desktop/main.cpp)
target_link_libraries(Huelith PRIVATE huelith_desktop)
qt_add_executable(HuelithM0 EXCLUDE_FROM_ALL WIN32 apps/desktop/m0_workbench.cpp)
target_link_libraries(HuelithM0 PRIVATE huelith_desktop)
if(WIN32)
    enable_language(RC)
    configure_file(apps/desktop/assets/huelith.rc.in "${CMAKE_CURRENT_BINARY_DIR}/huelith.rc" @ONLY)
    target_sources(Huelith PRIVATE "${CMAKE_CURRENT_BINARY_DIR}/huelith.rc")
    target_sources(HuelithM0 PRIVATE "${CMAKE_CURRENT_BINARY_DIR}/huelith.rc")
endif()
add_executable(huelith_icon_export EXCLUDE_FROM_ALL tools/export_windows_icon.cpp)
target_link_libraries(huelith_icon_export PRIVATE huelith_build_options Qt6::Svg Qt6::Gui)

option(HUELITH_BUILD_LEGACY_CPP_TESTS "Historical M0 C++ test executables (Python is the default validation runner)" OFF)
if(BUILD_TESTING AND HUELITH_BUILD_LEGACY_CPP_TESTS)
    add_test(NAME app_startup COMMAND Huelith --smoke-test)
    set_tests_properties(app_startup PROPERTIES LABELS integration TIMEOUT 30
        ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
        ENVIRONMENT_MODIFICATION "PATH=path_list_prepend:$<TARGET_FILE_DIR:Qt6::Core>")
    find_package(GTest CONFIG REQUIRED)
    include(GoogleTest)
    add_executable(huelith_m0_tests tests/integration/m0_state_test.cpp tests/integration/m0_image_test.cpp tests/integration/m0_blend_test.cpp)
    target_link_libraries(huelith_m0_tests PRIVATE huelith_validation GTest::gtest_main)
    gtest_discover_tests(huelith_m0_tests DISCOVERY_MODE PRE_TEST PROPERTIES LABELS m0
        ENVIRONMENT_MODIFICATION "PATH=path_list_prepend:$<TARGET_FILE_DIR:Qt6::Core>" TIMEOUT 30)
    if(WIN32)
        add_executable(huelith_m0_gpu_tests tests/integration/m0_gpu_test.cpp)
        target_link_libraries(huelith_m0_gpu_tests PRIVATE huelith_validation GTest::gtest_main)
        gtest_discover_tests(huelith_m0_gpu_tests DISCOVERY_MODE PRE_TEST PROPERTIES LABELS gpu
            ENVIRONMENT_MODIFICATION "PATH=path_list_prepend:$<TARGET_FILE_DIR:Qt6::Core>" TIMEOUT 120)
    endif()
    add_executable(huelith_unit_tests tests/unit/canvas_spec_test.cpp)
    target_link_libraries(huelith_unit_tests PRIVATE huelith_document GTest::gtest_main)
    gtest_discover_tests(huelith_unit_tests PROPERTIES LABELS unit)
    add_executable(huelith_gui_tests tests/gui/main_window_test.cpp tests/gui/m0_session_test.cpp tests/gui/m0_display_test.cpp)
    target_link_libraries(huelith_gui_tests PRIVATE huelith_desktop GTest::gtest)
    gtest_discover_tests(huelith_gui_tests PROPERTIES LABELS gui
        ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
        ENVIRONMENT_MODIFICATION "PATH=path_list_prepend:$<TARGET_FILE_DIR:Qt6::Core>"
        TIMEOUT 30)
endif()

include(GNUInstallDirs)
install(TARGETS Huelith RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
install(TARGETS HuelithM0 RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
qt_generate_deploy_app_script(TARGET Huelith OUTPUT_SCRIPT deploy_script NO_UNSUPPORTED_PLATFORM_ERROR)
install(SCRIPT ${deploy_script})

# Python drives production C++ code in-process. No additional test executable.
option(HUELITH_PYTHON_DRIVER "Build the local Python validation bridge" ON)
if(HUELITH_PYTHON_DRIVER)
    add_library(huelith_python_driver SHARED tools/python_driver.cpp)
    target_link_libraries(huelith_python_driver PRIVATE huelith_desktop)
endif()

foreach(raw_runtime onnxruntime.dll onnxruntime_providers_shared.dll DirectML.dll)
    configure_file("${HUELITH_ORT_DIR}/${raw_runtime}" "${CMAKE_CURRENT_BINARY_DIR}/${raw_runtime}" COPYONLY)
endforeach()
