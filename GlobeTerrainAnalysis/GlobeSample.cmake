include_guard(GLOBAL)
if(POLICY CMP0144)
    cmake_policy(SET CMP0144 NEW)
endif()
set(GEOKERNEL_ROOT "" CACHE PATH "Optional extracted GeoKernel SDK root")
set(GEOKERNEL_SDK_VERSION "latest")
if(DEFINED ENV{QTDIR})
    list(PREPEND CMAKE_PREFIX_PATH "$ENV{QTDIR}")
endif()
if(NOT WIN32 OR NOT MSVC OR NOT CMAKE_SIZEOF_VOID_P EQUAL 8)
    message(FATAL_ERROR "Globe examples currently require Windows x64 MSVC and a published SDK exporting GeoKernel::Globe.")
endif()
if(NOT GEOKERNEL_ROOT)
    include("${CMAKE_CURRENT_LIST_DIR}/cmake/AcquireGeoKernelSdk.cmake")
    geokernel_acquire_sdk(GEOKERNEL_ROOT)
endif()
get_filename_component(GEOKERNEL_ROOT "${GEOKERNEL_ROOT}" ABSOLUTE)
unset(GeoKernel_DIR CACHE)
unset(GeoKernel_DIR)
find_package(GeoKernel CONFIG REQUIRED PATHS "${GEOKERNEL_ROOT}" NO_DEFAULT_PATH)
if(NOT TARGET GeoKernel::Globe)
    message(FATAL_ERROR "GeoKernel SDK at ${GEOKERNEL_ROOT} does not export GeoKernel::Globe. Publish a Globe-enabled SDK and reconfigure. A 2D/Viewer3D-only SDK cannot build this example.")
endif()
find_package(Qt6 6.11.0 EXACT REQUIRED COMPONENTS Gui)
find_package(Vulkan REQUIRED)
file(WRITE "${CMAKE_BINARY_DIR}/sdk-selection.txt"
    "SDK=${GEOKERNEL_ROOT}\nResolvedRelease=${GEOKERNEL_RESOLVED_SDK_VERSION}\nQt=${Qt6_VERSION}\nCompiler=${CMAKE_CXX_COMPILER_ID} ${CMAKE_CXX_COMPILER_VERSION}\n")
find_program(_windeployqt windeployqt HINTS "${Qt6_DIR}/../../../bin" REQUIRED NO_DEFAULT_PATH)

function(geokernel_add_globe_sample name)
    add_executable(${name} ${ARGN})
    target_compile_features(${name} PRIVATE cxx_std_17)
    set_target_properties(${name} PROPERTIES CXX_EXTENSIONS OFF
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/outputs/windows/$<CONFIG>/bin"
        VS_DEBUGGER_WORKING_DIRECTORY "$<TARGET_FILE_DIR:${name}>")
    target_compile_definitions(${name} PRIVATE _USE_MATH_DEFINES)
    target_compile_options(${name} PRIVATE /utf-8 /permissive- /FS)
    target_include_directories(${name} PRIVATE "${CMAKE_CURRENT_FUNCTION_LIST_DIR}")
    target_link_libraries(${name} PRIVATE GeoKernel::Globe Qt6::Gui Vulkan::Vulkan)
    # Keep runtime libraries, plugins and geospatial support data from the selected SDK.
    add_custom_command(TARGET ${name} POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_directory "${GEOKERNEL_ROOT}/bin" "$<TARGET_FILE_DIR:${name}>"
        COMMAND "${_windeployqt}" --release --no-translations "$<TARGET_FILE:${name}>"
        VERBATIM)
    if(EXISTS "${GEOKERNEL_ROOT}/licenses")
        add_custom_command(TARGET ${name} POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_directory "${GEOKERNEL_ROOT}/licenses" "$<TARGET_FILE_DIR:${name}>/licenses/sdk"
            VERBATIM)
    endif()
endfunction()
