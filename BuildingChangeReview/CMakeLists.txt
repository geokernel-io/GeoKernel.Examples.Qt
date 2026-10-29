cmake_minimum_required(VERSION 3.22)
if(POLICY CMP0144)
    cmake_policy(SET CMP0144 NEW)
endif()


project(BuildingChangeReview VERSION 1.0.0 LANGUAGES CXX)
if(WIN32)
    enable_language(RC)
endif()

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)

set(GEOKERNEL_ROOT "" CACHE PATH "Optional extracted GeoKernel SDK root")
set(GEOKERNEL_SDK_VERSION "latest" CACHE STRING "GeoKernel SDK version or latest")
if(DEFINED ENV{QTDIR})
    list(PREPEND CMAKE_PREFIX_PATH "$ENV{QTDIR}")
endif()
if(NOT GEOKERNEL_ROOT)
    include("${CMAKE_CURRENT_SOURCE_DIR}/cmake/AcquireGeoKernelSdk.cmake")
    geokernel_acquire_sdk(GEOKERNEL_ROOT)
endif()
get_filename_component(GEOKERNEL_ROOT "${GEOKERNEL_ROOT}" ABSOLUTE)
list(PREPEND CMAKE_PREFIX_PATH "${GEOKERNEL_ROOT}")

find_package(Qt6 REQUIRED COMPONENTS Core Gui Widgets OpenGL OpenGLWidgets Network Sql Svg)
find_package(GeoKernel 1.5.39 CONFIG REQUIRED)
if(NOT TARGET GeoKernel::AI)
    message(FATAL_ERROR "The selected GeoKernel SDK does not contain GeoKernel::AI.")
endif()
if(NOT TARGET GeoKernel::Analysis)
    message(FATAL_ERROR "The selected GeoKernel SDK does not contain GeoKernel::Analysis.")
endif()

set(_qt_targets Qt6::Core Qt6::Gui Qt6::Widgets Qt6::OpenGL Qt6::OpenGLWidgets Qt6::Network Qt6::Sql Qt6::Svg)
foreach(_target IN LISTS _qt_targets)
    set_target_properties(${_target} PROPERTIES MAP_IMPORTED_CONFIG_DEBUG Release)
endforeach()

add_executable(BuildingChangeReview WIN32 main.cpp DemoWindow.h ChangeLogic.h ChangeWorkflow.h GeometrySupport.h RasterCrop.h DemoAssets.h FootprintOutput.h SampleSupport.h resources/icons.qrc)
if(WIN32)
    target_sources(BuildingChangeReview PRIVATE resources/BuildingChangeReview.rc)
endif()
target_compile_definitions(BuildingChangeReview PRIVATE QT_NO_DEPRECATED_WARNINGS QT_SQL_LIB)
target_include_directories(BuildingChangeReview SYSTEM PRIVATE
    "${GEOKERNEL_ROOT}/include/GeoKernel.Core"
    "${GEOKERNEL_ROOT}/include/GeoKernel.Formats"
    "${GEOKERNEL_ROOT}/include/GeoKernel.AI"
    "${GEOKERNEL_ROOT}/include/GeoKernel.Analysis"
    "${GEOKERNEL_ROOT}/include/GeoKernel.Viewer")
target_link_libraries(BuildingChangeReview PRIVATE
    GeoKernel::Viewer GeoKernel::Formats GeoKernel::AI GeoKernel::Analysis GeoKernel::Core ${_qt_targets})
if(MSVC)
    target_compile_options(BuildingChangeReview PRIVATE /W3 /permissive- /FS /utf-8 /Zi)
    target_link_options(BuildingChangeReview PRIVATE /DEBUG:FULL)
endif()

if(WIN32)
    set(_platform_output "windows")
    set(_geokernel_runtime_dir "${GEOKERNEL_ROOT}/bin")
else()
    set(_platform_output "linux")
    set(_geokernel_runtime_dir "${GEOKERNEL_ROOT}/lib")
endif()
set(_output_dir "${CMAKE_CURRENT_SOURCE_DIR}/outputs/${_platform_output}/$<CONFIG>/bin")
set_target_properties(BuildingChangeReview PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${_output_dir}"
    VS_DEBUGGER_WORKING_DIRECTORY "$<TARGET_FILE_DIR:BuildingChangeReview>")
add_custom_command(TARGET BuildingChangeReview POST_BUILD COMMAND ${CMAKE_COMMAND}
    -D "GEOKERNEL_RUNTIME_DIR=${_geokernel_runtime_dir}"
    -D "QT_BIN_DIR=$<TARGET_FILE_DIR:Qt6::Core>"
    -D "QT_RUNTIME_FILES=$<TARGET_FILE:Qt6::Core>;$<TARGET_FILE:Qt6::Gui>;$<TARGET_FILE:Qt6::Widgets>;$<TARGET_FILE:Qt6::OpenGL>;$<TARGET_FILE:Qt6::OpenGLWidgets>;$<TARGET_FILE:Qt6::Network>;$<TARGET_FILE:Qt6::Sql>;$<TARGET_FILE:Qt6::Svg>"
    -D "DESTINATION=${_output_dir}"
    -P "${CMAKE_CURRENT_SOURCE_DIR}/cmake/CopyRuntime.cmake")

option(BUILD_TESTING "Build the building change comparison tests" OFF)
if(BUILD_TESTING)
    enable_testing()
    add_executable(BuildingChangeReview.Tests tests/ChangeLogicTests.cpp)
    target_include_directories(BuildingChangeReview.Tests PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}" "${GEOKERNEL_ROOT}/include/GeoKernel.AI" "${GEOKERNEL_ROOT}/include/GeoKernel.Core")
    target_link_libraries(BuildingChangeReview.Tests PRIVATE GeoKernel::AI GeoKernel::Core Qt6::Core Qt6::Gui)
    set_target_properties(BuildingChangeReview.Tests PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${_output_dir}")
    if(MSVC)
        target_compile_options(BuildingChangeReview.Tests PRIVATE /utf-8 /permissive-)
    endif()
    add_test(NAME BuildingChangeReview.Logic COMMAND BuildingChangeReview.Tests)
    add_executable(BuildingChangeReview.PublicationTests tests/PublicationTests.cpp)
    target_include_directories(BuildingChangeReview.PublicationTests PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}"
        "${GEOKERNEL_ROOT}/include/GeoKernel.Core" "${GEOKERNEL_ROOT}/include/GeoKernel.Formats"
        "${GEOKERNEL_ROOT}/include/GeoKernel.AI" "${GEOKERNEL_ROOT}/include/GeoKernel.Analysis"
        "${GEOKERNEL_ROOT}/include/GeoKernel.Viewer")
    target_link_libraries(BuildingChangeReview.PublicationTests PRIVATE GeoKernel::Viewer GeoKernel::AI
        GeoKernel::Analysis GeoKernel::Formats GeoKernel::Core ${_qt_targets})
    set_target_properties(BuildingChangeReview.PublicationTests PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${_output_dir}")
    if(MSVC)
        target_compile_options(BuildingChangeReview.PublicationTests PRIVATE /utf-8 /permissive- /Zi)
        target_link_options(BuildingChangeReview.PublicationTests PRIVATE /DEBUG:FULL)
    endif()
    add_test(NAME BuildingChangeReview.Publication COMMAND BuildingChangeReview.PublicationTests -platform offscreen)
endif()

option(BUILD_INFERENCE_TESTS "Build the opt-in real-image/model workflow test" OFF)
if(BUILD_TESTING AND BUILD_INFERENCE_TESTS)
    add_executable(BuildingChangeReview.InferenceTests tests/InferenceWorkflowTests.cpp)
    target_include_directories(BuildingChangeReview.InferenceTests PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}"
        "${GEOKERNEL_ROOT}/include/GeoKernel.Core" "${GEOKERNEL_ROOT}/include/GeoKernel.Formats"
        "${GEOKERNEL_ROOT}/include/GeoKernel.AI" "${GEOKERNEL_ROOT}/include/GeoKernel.Analysis"
        "${GEOKERNEL_ROOT}/include/GeoKernel.Viewer")
    target_link_libraries(BuildingChangeReview.InferenceTests PRIVATE GeoKernel::Viewer GeoKernel::AI GeoKernel::Analysis GeoKernel::Formats GeoKernel::Core ${_qt_targets})
    set_target_properties(BuildingChangeReview.InferenceTests PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${_output_dir}")
    if(MSVC)
        target_compile_options(BuildingChangeReview.InferenceTests PRIVATE /utf-8 /permissive-)
    endif()
    add_test(NAME BuildingChangeReview.Inference COMMAND BuildingChangeReview.InferenceTests
        "${_output_dir}/data" "${CMAKE_CURRENT_SOURCE_DIR}/outputs/validation")
    set_tests_properties(BuildingChangeReview.Inference PROPERTIES TIMEOUT 300 RUN_SERIAL TRUE)
endif()
