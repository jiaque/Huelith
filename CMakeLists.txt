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
find_package(OpenCV CONFIG REQUIRED COMPONENTS core imgproc features2d calib3d stitching)
find_path(LENSFUN_INCLUDE_DIR lensfun/lensfun.h REQUIRED)
find_library(LENSFUN_LIBRARY_RELEASE NAMES lensfun PATHS "${VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}/lib" NO_DEFAULT_PATH REQUIRED)
find_library(LENSFUN_LIBRARY_DEBUG NAMES lensfun PATHS "${VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}/debug/lib" NO_DEFAULT_PATH REQUIRED)
add_library(huelith_lensfun UNKNOWN IMPORTED)
set_target_properties(huelith_lensfun PROPERTIES IMPORTED_LOCATION_RELEASE "${LENSFUN_LIBRARY_RELEASE}"
    IMPORTED_LOCATION_DEBUG "${LENSFUN_LIBRARY_DEBUG}" INTERFACE_INCLUDE_DIRECTORIES "${LENSFUN_INCLUDE_DIR}")
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
target_sources(huelith_editing PRIVATE src/editing/raw_adjustments.cpp src/editing/raw_recipe.cpp)
target_sources(huelith_editing PRIVATE src/editing/raw_profile.cpp)
target_sources(huelith_editing PRIVATE src/editing/raw_sidecar.cpp)
target_sources(huelith_editing PRIVATE src/editing/effects.cpp)
target_sources(huelith_editing PRIVATE src/editing/calibration.cpp)
target_sources(huelith_editing PRIVATE src/editing/demosaic.cpp)
target_sources(huelith_editing PRIVATE src/editing/detail.cpp)
target_sources(huelith_editing PRIVATE src/editing/semantic.cpp)
target_sources(huelith_editing PRIVATE src/editing/instance_segmentation.cpp src/editing/portrait.cpp)
target_sources(huelith_editing PRIVATE src/editing/portrait_effects.cpp)
target_sources(huelith_editing PRIVATE src/editing/portrait_contour.cpp)
target_sources(huelith_editing PRIVATE src/editing/portrait_obstacles.cpp)
target_sources(huelith_editing PRIVATE src/editing/portrait_skin.cpp)
target_sources(huelith_editing PRIVATE src/editing/portrait_repair.cpp)
target_sources(huelith_editing PRIVATE src/editing/portrait_style.cpp src/editing/portrait_decals.cpp src/editing/portrait_paint.cpp src/editing/portrait_nails.cpp src/editing/portrait_nail_detection.cpp src/editing/portrait_tattoo.cpp src/editing/portrait_volume.cpp)
target_sources(huelith_editing PRIVATE src/editing/portrait_presets.cpp)
foreach(portrait_model lm_model3_opt.onnx mnv3_detection_opt.onnx)
    configure_file("${CMAKE_CURRENT_SOURCE_DIR}/research/m3/openseeface/${portrait_model}" "${CMAKE_CURRENT_BINARY_DIR}/models/${portrait_model}" COPYONLY)
endforeach()
configure_file("${CMAKE_CURRENT_SOURCE_DIR}/research/m3/mediapipe-pose/pose_estimation_mediapipe_2023mar.onnx" "${CMAKE_CURRENT_BINARY_DIR}/models/pose_estimation_mediapipe_2023mar.onnx" COPYONLY)
configure_file("${CMAKE_CURRENT_SOURCE_DIR}/research/m3/mediapipe-person/person_detection_mediapipe_2023mar.onnx" "${CMAKE_CURRENT_BINARY_DIR}/models/person_detection_mediapipe_2023mar.onnx" COPYONLY)
configure_file("${CMAKE_CURRENT_SOURCE_DIR}/research/m3/selfie-multiclass/selfie_multiclass.onnx" "${CMAKE_CURRENT_BINARY_DIR}/models/selfie_multiclass.onnx" COPYONLY)
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/research/m3/face-landmarks/face_landmarks_478.onnx")
    configure_file("${CMAKE_CURRENT_SOURCE_DIR}/research/m3/face-landmarks/face_landmarks_478.onnx" "${CMAKE_CURRENT_BINARY_DIR}/models/face_landmarks_478.onnx" COPYONLY)
endif()
target_sources(huelith_editing PRIVATE src/editing/ai_worker.cpp src/editing/enhancement.cpp)
target_sources(huelith_editing PRIVATE src/editing/photo_merge.cpp src/editing/image_storage.cpp src/editing/project_archive.cpp)
target_sources(huelith_editing PRIVATE src/editing/dng.cpp)
target_sources(huelith_editing PRIVATE src/editing/metadata.cpp)
target_sources(huelith_editing PRIVATE src/editing/embedded_preview.cpp)
target_include_directories(huelith_editing SYSTEM PRIVATE ${OpenCV_INCLUDE_DIRS})
target_link_libraries(huelith_editing PRIVATE ${OpenCV_LIBS})
foreach(enhancement_model realesr-general-x4v3.onnx realesrgan-x4plus.onnx nafnet-sidd.onnx midas-small.onnx)
    if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/research/m2/enhancement/${enhancement_model}")
        configure_file("${CMAKE_CURRENT_SOURCE_DIR}/research/m2/enhancement/${enhancement_model}"
            "${CMAKE_CURRENT_BINARY_DIR}/models/${enhancement_model}" COPYONLY)
    endif()
endforeach()
target_sources(huelith_editing PRIVATE src/editing/selection_refine.cpp)
set(HUELITH_INSTANCE_MODEL "${CMAKE_CURRENT_SOURCE_DIR}/research/m2/instances/maskrcnn-r50-v2.onnx")
if(EXISTS "${HUELITH_INSTANCE_MODEL}")
    configure_file("${HUELITH_INSTANCE_MODEL}" "${CMAKE_CURRENT_BINARY_DIR}/models/maskrcnn-r50-v2.onnx" COPYONLY)
endif()
target_sources(huelith_editing PRIVATE src/editing/white_balance.cpp)
target_sources(huelith_editing PRIVATE src/editing/design.cpp)
target_sources(huelith_editing PRIVATE src/editing/layer_style.cpp)
target_sources(huelith_editing PRIVATE src/editing/layout_template.cpp)
target_sources(huelith_editing PRIVATE src/editing/healing.cpp)
target_sources(huelith_editing PRIVATE src/editing/brush.cpp)
target_sources(huelith_editing PRIVATE src/editing/content_fill.cpp src/editing/liquify.cpp)
set(HUELITH_SEMANTIC_MODEL "${CMAKE_CURRENT_SOURCE_DIR}/research/m2/semantic/pp-mobileseg/pp-mobileseg-base-labels.onnx")
if(EXISTS "${HUELITH_SEMANTIC_MODEL}")
    configure_file("${HUELITH_SEMANTIC_MODEL}" "${CMAKE_CURRENT_BINARY_DIR}/models/pp-mobileseg-base.onnx" COPYONLY)
endif()
target_sources(huelith_editing PRIVATE src/editing/point_color.cpp)
target_sources(huelith_editing PRIVATE src/editing/optics.cpp)
target_sources(huelith_editing PRIVATE src/editing/raw_geometry.cpp)
target_link_libraries(huelith_editing PUBLIC huelith_build_options Qt6::Gui PRIVATE libraw::raw_r TIFF::TIFF huelith_ort huelith_lensfun)
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
    apps/desktop/canvas_render.cpp
    apps/desktop/canvas_navigation.cpp
    apps/desktop/canvas_design.cpp
    apps/desktop/design_workflow.cpp
    apps/desktop/layer_style_workflow.cpp
    apps/desktop/template_workflow.cpp
    apps/desktop/enhancement_workflow.cpp
    apps/desktop/photo_merge_workflow.cpp
    apps/desktop/text_edit_view.cpp
    apps/desktop/canvas_preferences.cpp
    apps/desktop/canvas_photo.cpp
    apps/desktop/canvas_objects.cpp
    apps/desktop/photo_tools.cpp
    apps/desktop/raw_dialog.cpp
    apps/desktop/raw_filmstrip.cpp
    apps/desktop/raw_profiles.cpp
    apps/desktop/raw_healing.cpp
    apps/desktop/raw_crop.cpp
    apps/desktop/raw_settings.cpp
    apps/desktop/raw_masks.cpp
    apps/desktop/selection_refine_dialog.cpp
    apps/desktop/selection_refine_workflow.cpp
    apps/desktop/raw_preview_view.cpp
    apps/desktop/workspace.cpp
    apps/desktop/brush_workflow.cpp
    apps/desktop/content_fill_workflow.cpp
    apps/desktop/liquify_workflow.cpp
    apps/desktop/raw_inspection.cpp
    apps/desktop/tone_panel.cpp
    apps/desktop/curve_panel.cpp
    apps/desktop/point_color_panel.cpp
    apps/desktop/raw_optics_panel.cpp
    apps/desktop/raw_geometry_panel.cpp
    apps/desktop/semantic_dialog.cpp
    apps/desktop/semantic_workflow.cpp
    apps/desktop/raw_semantic.cpp
    apps/desktop/raw_white_balance_panel.cpp
    apps/desktop/raw_white_balance.cpp
    apps/desktop/tone_workflow.cpp
    apps/desktop/effects_workflow.cpp
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
    BASE apps/desktop/assets FILES apps/desktop/assets/huelith.svg
    apps/desktop/assets/portrait/volume.svg
    apps/desktop/assets/portrait/volume-approved.png
    apps/desktop/assets/portrait/lift-approved.png
    apps/desktop/assets/portrait/decals/bow.png
    apps/desktop/assets/portrait/decals/blue-flower.png
    apps/desktop/assets/portrait/decals/white-flower.png
    apps/desktop/assets/portrait/decals/botanical-tattoo.png
    apps/desktop/assets/portrait/body-lines-approved.png
    apps/desktop/assets/portrait/hair-volume-approved.png
    apps/desktop/assets/portrait/hair-quality-approved.png
    apps/desktop/assets/portrait/slim.svg
    apps/desktop/assets/portrait/erase.svg
    apps/desktop/assets/portrait/smallhead.svg
    apps/desktop/assets/portrait/contour.svg
    apps/desktop/assets/portrait/face.svg
    apps/desktop/assets/portrait/body.svg
    apps/desktop/assets/portrait/liquify.svg
    apps/desktop/assets/portrait/skin.svg
    apps/desktop/assets/portrait/repair.svg
    apps/desktop/assets/portrait/makeup.svg
    apps/desktop/assets/portrait/hair.svg
    apps/desktop/assets/portrait/light.svg
    apps/desktop/assets/portrait/presets.svg
    apps/desktop/assets/portrait/teeth.svg
    apps/desktop/assets/portrait/eye.svg
    apps/desktop/assets/portrait/oil.svg
    apps/desktop/assets/portrait/wrinkle.svg
    apps/desktop/assets/portrait/blemish.svg
)

target_sources(huelith_desktop PRIVATE apps/desktop/portrait_dialog.cpp apps/desktop/portrait_workflow.cpp)
target_sources(huelith_desktop PRIVATE apps/desktop/portrait_repair_ui.cpp)
target_sources(huelith_desktop PRIVATE apps/desktop/portrait_style_ui.cpp apps/desktop/portrait_makeup_ui.cpp)
target_sources(huelith_desktop PRIVATE apps/desktop/portrait_preset_ui.cpp)
qt_add_executable(Huelith WIN32 apps/desktop/main.cpp)
target_link_libraries(Huelith PRIVATE huelith_desktop)
if(WIN32)
    # The build output is also used for local delivery. It must launch from
    # Explorer without the developer's Qt PATH or plugin environment.
    find_program(HUELITH_WINDEPLOYQT windeployqt
        HINTS "${Qt6Core_DIR}/../../../bin" REQUIRED)
    add_custom_command(TARGET Huelith POST_BUILD
        COMMAND "${HUELITH_WINDEPLOYQT}" "$<IF:$<CONFIG:Debug>,--debug,--release>" --no-translations --no-compiler-runtime
            "$<TARGET_FILE:Huelith>"
        COMMENT "Deploying local Qt runtime and platform plugins" VERBATIM)
    if(MSVC)
        set(CMAKE_INSTALL_SYSTEM_RUNTIME_LIBS_SKIP TRUE)
        include(InstallRequiredSystemLibraries)
        if(CMAKE_INSTALL_SYSTEM_RUNTIME_LIBS)
            add_custom_command(TARGET Huelith POST_BUILD
                COMMAND ${CMAKE_COMMAND} -E copy_if_different
                    ${CMAKE_INSTALL_SYSTEM_RUNTIME_LIBS} "$<TARGET_FILE_DIR:Huelith>"
                COMMENT "Deploying MSVC runtime" VERBATIM)
        endif()
    endif()
endif()
set(EXIFTOOL_RUNTIME "${CMAKE_CURRENT_SOURCE_DIR}/research/m2/metadata/runtime/exiftool-13.59_64")
if(NOT EXISTS "${EXIFTOOL_RUNTIME}/exiftool(-k).exe")
    message(FATAL_ERROR "Pinned ExifTool runtime is required: research/m2/metadata/runtime/exiftool-13.59_64")
endif()
add_custom_command(TARGET Huelith POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory "${EXIFTOOL_RUNTIME}/exiftool_files" "$<TARGET_FILE_DIR:Huelith>/tools/exiftool/exiftool_files"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different "${EXIFTOOL_RUNTIME}/exiftool(-k).exe" "$<TARGET_FILE_DIR:Huelith>/tools/exiftool/exiftool.exe")
add_custom_command(TARGET Huelith POST_BUILD COMMAND ${CMAKE_COMMAND} -E copy_directory
    "${VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}/share/lensfun/version_1"
    "$<TARGET_FILE_DIR:Huelith>/lensfun")
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
    add_library(huelith_python_driver SHARED tools/python_driver.cpp tools/design_driver.cpp tools/brush_driver.cpp tools/refine_driver.cpp tools/object_driver.cpp tools/filmstrip_driver.cpp tools/profile_driver.cpp tools/composite_driver.cpp)
    target_link_libraries(huelith_python_driver PRIVATE huelith_desktop)
    target_sources(huelith_python_driver PRIVATE tools/enhancement_driver.cpp tools/large_image_driver.cpp tools/portrait_driver.cpp)
    target_compile_definitions(huelith_python_driver PRIVATE HUELITH_TEST_WORKER_PATH="$<TARGET_FILE:Huelith>")
endif()

foreach(raw_runtime onnxruntime.dll onnxruntime_providers_shared.dll DirectML.dll)
    configure_file("${HUELITH_ORT_DIR}/${raw_runtime}" "${CMAKE_CURRENT_BINARY_DIR}/${raw_runtime}" COPYONLY)
endforeach()
