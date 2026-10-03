cmake_minimum_required(VERSION 3.22)

if(POLICY CMP0144)
    cmake_policy(SET CMP0144 NEW)
endif()

project(LayerStylingAndFiltering VERSION 1.0.0 LANGUAGES CXX)

if(WIN32)
    enable_language(RC)
endif()

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)

set(GEOKERNEL_ROOT "" CACHE PATH "Optional extracted GeoKernel SDK root")
# Public examples always follow the newest public GeoKernel SDK release.
# Keep this out of the CMake cache so an old configure cannot pin the project.
set(GEOKERNEL_SDK_VERSION "latest")

if(DEFINED ENV{QTDIR})
    list(PREPEND CMAKE_PREFIX_PATH "$ENV{QTDIR}")
endif()

if(NOT GEOKERNEL_ROOT)
    include("${CMAKE_CURRENT_SOURCE_DIR}/cmake/AcquireGeoKernelSdk.cmake")
    geokernel_acquire_sdk(GEOKERNEL_ROOT)
endif()
get_filename_component(GEOKERNEL_ROOT "${GEOKERNEL_ROOT}" ABSOLUTE)

list(PREPEND CMAKE_PREFIX_PATH "${GEOKERNEL_ROOT}")
find_package(Qt6 REQUIRED COMPONENTS Core Gui Widgets Network Sql OpenGL OpenGLWidgets Svg)
# Resolve only the selected/downloaded SDK, ignoring a previous package cache.
unset(GeoKernel_DIR CACHE)
unset(GeoKernel_DIR)
find_package(GeoKernel CONFIG REQUIRED PATHS "${GEOKERNEL_ROOT}" NO_DEFAULT_PATH)

set(_qt_targets
    Qt6::Core Qt6::Gui Qt6::Widgets Qt6::Network Qt6::Sql Qt6::OpenGL Qt6::OpenGLWidgets Qt6::Svg)
foreach(_target IN LISTS _qt_targets)
    set_target_properties(${_target} PROPERTIES MAP_IMPORTED_CONFIG_DEBUG Release)
endforeach()

add_executable(LayerStylingAndFiltering WIN32 main.cpp StylingPanel.h SampleSupport.h resources/icons.qrc)
if(WIN32)
    target_sources(LayerStylingAndFiltering PRIVATE resources/LayerStylingAndFiltering.rc)
endif()
target_compile_definitions(LayerStylingAndFiltering PRIVATE QT_NO_DEPRECATED_WARNINGS)
if(NOT TARGET GeoKernel::Viewer3D)
    message(FATAL_ERROR "GeoKernel SDK at ${GEOKERNEL_ROOT} does not export GeoKernel::Viewer3D. A published SDK containing the 3D module is required; this example cannot use a 2D-only release.")
endif()
target_link_libraries(LayerStylingAndFiltering PRIVATE GeoKernel::Viewer3D ${_qt_targets})

if(MSVC)
    target_compile_options(LayerStylingAndFiltering PRIVATE /W3 /permissive- /FS /utf-8)
endif()

if(WIN32)
    set(_platform_output "windows")
    set(_geokernel_runtime_dir "${GEOKERNEL_ROOT}/bin")
else()
    set(_platform_output "linux")
    set(_geokernel_runtime_dir "${GEOKERNEL_ROOT}/lib")
endif()
if(GEOKERNEL_RESOLVED_SDK_VERSION)
    message(STATUS "GeoKernel SDK version: ${GEOKERNEL_RESOLVED_SDK_VERSION}")
endif()
set(_output_dir "${CMAKE_CURRENT_SOURCE_DIR}/outputs/${_platform_output}/$<CONFIG>/bin")
set_target_properties(LayerStylingAndFiltering PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${_output_dir}"
    VS_DEBUGGER_WORKING_DIRECTORY "$<TARGET_FILE_DIR:LayerStylingAndFiltering>")

if(UNIX AND NOT APPLE)
    set_target_properties(LayerStylingAndFiltering PROPERTIES
        BUILD_RPATH "$ORIGIN"
        INSTALL_RPATH "$ORIGIN")
    target_link_options(LayerStylingAndFiltering PRIVATE "LINKER:--disable-new-dtags")
endif()

if(WIN32)
    add_custom_command(TARGET LayerStylingAndFiltering POST_BUILD
        COMMAND ${CMAKE_COMMAND}
            -D "GEOKERNEL_RUNTIME_DIR=${_geokernel_runtime_dir}"
            -D "QT_BIN_DIR=$<TARGET_FILE_DIR:Qt6::Core>"
            -D "QT_RUNTIME_FILES=$<TARGET_FILE:Qt6::Core>;$<TARGET_FILE:Qt6::Gui>;$<TARGET_FILE:Qt6::Widgets>;$<TARGET_FILE:Qt6::Network>;$<TARGET_FILE:Qt6::Sql>;$<TARGET_FILE:Qt6::OpenGL>;$<TARGET_FILE:Qt6::OpenGLWidgets>;$<TARGET_FILE:Qt6::Svg>"
            -D "DESTINATION=${_output_dir}"
            -P "${CMAKE_CURRENT_SOURCE_DIR}/cmake/CopyRuntime.cmake"
        COMMENT "Copying GeoKernel and Qt runtime files")
else()
    add_custom_command(TARGET LayerStylingAndFiltering POST_BUILD
        COMMAND ${CMAKE_COMMAND}
            -D "GEOKERNEL_RUNTIME_DIR=${_geokernel_runtime_dir}"
            -D "DESTINATION=${_output_dir}"
            -P "${CMAKE_CURRENT_SOURCE_DIR}/cmake/CopyRuntime.cmake"
        COMMENT "Copying GeoKernel runtime files")
endif()
