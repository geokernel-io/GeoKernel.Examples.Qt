cmake_minimum_required(VERSION 3.22)
if(POLICY CMP0144)
    cmake_policy(SET CMP0144 NEW)
endif()
project(LandCoverInference VERSION 1.0.0 LANGUAGES CXX)
if(WIN32 AND NOT MSVC)
    message(FATAL_ERROR "The GeoKernel Windows SDK is built with MSVC. Configure this example with the Qt MSVC kit, not MinGW/GCC.")
endif()
if(WIN32)
    enable_language(RC)
endif()

# The distributed GeoKernel SDK and Qt runtime are release binaries. Building
# the example with the MSVC Debug ABI while linking those binaries corrupts Qt
# value types (for example QVariantMap) when they cross the DLL boundary.
# RelWithDebInfo keeps source-level debugging available without mixing ABIs.
if(MSVC AND CMAKE_BUILD_TYPE STREQUAL "Debug")
    message(STATUS "GeoKernel SDK is release-only; using RelWithDebInfo for ABI-safe debugging.")
    set(CMAKE_BUILD_TYPE "RelWithDebInfo" CACHE STRING "Build type" FORCE)
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
find_package(GeoKernel CONFIG REQUIRED)
if(NOT TARGET GeoKernel::AI)
    message(FATAL_ERROR "The selected GeoKernel SDK does not contain GeoKernel::AI.")
endif()

set(_qt_targets Qt6::Core Qt6::Gui Qt6::Widgets Qt6::OpenGL Qt6::OpenGLWidgets Qt6::Network Qt6::Sql Qt6::Svg)
foreach(_target IN LISTS _qt_targets)
    set_target_properties(${_target} PROPERTIES
        MAP_IMPORTED_CONFIG_DEBUG Release
        MAP_IMPORTED_CONFIG_RELWITHDEBINFO Release)
endforeach()

add_executable(LandCoverInference WIN32 main.cpp resources/icons.qrc)
if(WIN32)
    target_sources(LandCoverInference PRIVATE resources/LandCoverInference.rc)
endif()
target_compile_definitions(LandCoverInference PRIVATE QT_NO_DEPRECATED_WARNINGS QT_SQL_LIB)
target_include_directories(LandCoverInference SYSTEM PRIVATE
    "${GEOKERNEL_ROOT}/include/GeoKernel.Core"
    "${GEOKERNEL_ROOT}/include/GeoKernel.Formats"
    "${GEOKERNEL_ROOT}/include/GeoKernel.AI"
    "${GEOKERNEL_ROOT}/include/GeoKernel.Viewer")
target_link_libraries(LandCoverInference PRIVATE
    GeoKernel::Viewer GeoKernel::Formats GeoKernel::AI GeoKernel::Core ${_qt_targets})
if(MSVC)
    target_compile_options(LandCoverInference PRIVATE /W3 /permissive- /FS /utf-8)
endif()

if(WIN32)
    set(_platform_output "windows")
    set(_geokernel_runtime_dir "${GEOKERNEL_ROOT}/bin")
else()
    set(_platform_output "linux")
    set(_geokernel_runtime_dir "${GEOKERNEL_ROOT}/lib")
endif()
set(_output_dir "${CMAKE_CURRENT_SOURCE_DIR}/outputs/${_platform_output}/$<CONFIG>/bin")
set_target_properties(LandCoverInference PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${_output_dir}"
    VS_DEBUGGER_WORKING_DIRECTORY "$<TARGET_FILE_DIR:LandCoverInference>")
add_custom_command(TARGET LandCoverInference POST_BUILD COMMAND ${CMAKE_COMMAND}
    -D "GEOKERNEL_RUNTIME_DIR=${_geokernel_runtime_dir}"
    -D "QT_BIN_DIR=$<TARGET_FILE_DIR:Qt6::Core>"
    -D "QT_RUNTIME_FILES=$<TARGET_FILE:Qt6::Core>;$<TARGET_FILE:Qt6::Gui>;$<TARGET_FILE:Qt6::Widgets>;$<TARGET_FILE:Qt6::OpenGL>;$<TARGET_FILE:Qt6::OpenGLWidgets>;$<TARGET_FILE:Qt6::Network>;$<TARGET_FILE:Qt6::Sql>;$<TARGET_FILE:Qt6::Svg>"
    -D "DESTINATION=${_output_dir}"
    -P "${CMAKE_CURRENT_SOURCE_DIR}/cmake/CopyRuntime.cmake")
