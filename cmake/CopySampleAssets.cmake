if(NOT DEFINED SOURCE_DATA_DIR OR NOT DEFINED SOURCE_IMAGES_DIR OR NOT DEFINED DESTINATION_ASSETS_DIR)
    message(FATAL_ERROR "CopySampleAssets.cmake requires SOURCE_DATA_DIR, SOURCE_IMAGES_DIR, and DESTINATION_ASSETS_DIR.")
endif()

set(_destination_data_dir "${DESTINATION_ASSETS_DIR}/data")
set(_destination_images_dir "${DESTINATION_ASSETS_DIR}/images")

file(REMOVE_RECURSE "${_destination_images_dir}")
file(MAKE_DIRECTORY "${_destination_data_dir}")
file(MAKE_DIRECTORY "${_destination_images_dir}")

function(_remove_empty_directories root_dir)
    if(NOT EXISTS "${root_dir}")
        return()
    endif()

    file(GLOB_RECURSE _directories
        LIST_DIRECTORIES true
        "${root_dir}/*")
    list(REVERSE _directories)

    foreach(_directory IN LISTS _directories)
        if(IS_DIRECTORY "${_directory}")
            file(GLOB _children
                LIST_DIRECTORIES true
                "${_directory}/*")
            if(NOT _children)
                file(REMOVE_RECURSE "${_directory}")
            endif()
        endif()
    endforeach()
endfunction()

function(_pgidx_has_matching_source source_dir index_relative_path result_var)
    get_filename_component(_index_directory "${index_relative_path}" DIRECTORY)
    get_filename_component(_index_name "${index_relative_path}" NAME)
    string(REGEX REPLACE "\\.pgidx$" "" _index_base "${_index_name}")

    if(_index_directory STREQUAL "")
        set(_source_pattern "${source_dir}/${_index_base}.*")
    else()
        set(_source_pattern "${source_dir}/${_index_directory}/${_index_base}.*")
    endif()

    file(GLOB _matching_sources
        LIST_DIRECTORIES false
        "${_source_pattern}")

    set(_has_source false)
    foreach(_matching_source IN LISTS _matching_sources)
        string(TOLOWER "${_matching_source}" _matching_source_lower)
        if(NOT _matching_source_lower MATCHES "\\.pgidx$")
            set(_has_source true)
            break()
        endif()
    endforeach()

    set(${result_var} "${_has_source}" PARENT_SCOPE)
endfunction()

function(_prune_data_tree source_dir destination_dir)
    if(NOT EXISTS "${destination_dir}")
        return()
    endif()

    file(GLOB_RECURSE _destination_files
        LIST_DIRECTORIES false
        RELATIVE "${destination_dir}"
        "${destination_dir}/*")

    foreach(_destination_file IN LISTS _destination_files)
        string(TOLOWER "${_destination_file}" _destination_file_lower)
        set(_destination_path "${destination_dir}/${_destination_file}")

        if(_destination_file_lower MATCHES "\\.pgidx$")
            _pgidx_has_matching_source("${source_dir}" "${_destination_file}" _has_matching_source)
            if(NOT _has_matching_source)
                file(REMOVE "${_destination_path}")
            endif()
            continue()
        endif()

        if(NOT EXISTS "${source_dir}/${_destination_file}")
            file(REMOVE "${_destination_path}")
        endif()
    endforeach()

    _remove_empty_directories("${destination_dir}")
endfunction()

function(_copy_tree source_dir destination_dir exclude_pgidx)
    if(NOT EXISTS "${source_dir}")
        return()
    endif()

    file(GLOB_RECURSE _asset_files
        LIST_DIRECTORIES false
        RELATIVE "${source_dir}"
        "${source_dir}/*")

    foreach(_asset_file IN LISTS _asset_files)
        set(_source_file "${source_dir}/${_asset_file}")
        string(TOLOWER "${_asset_file}" _asset_file_lower)
        if(exclude_pgidx AND _asset_file_lower MATCHES "\\.pgidx$")
            continue()
        endif()

        set(_destination_file "${destination_dir}/${_asset_file}")
        get_filename_component(_destination_parent "${_destination_file}" DIRECTORY)
        file(MAKE_DIRECTORY "${_destination_parent}")
        file(COPY_FILE "${_source_file}" "${_destination_file}" ONLY_IF_DIFFERENT)
    endforeach()
endfunction()

_prune_data_tree("${SOURCE_DATA_DIR}" "${_destination_data_dir}")
_copy_tree("${SOURCE_DATA_DIR}" "${_destination_data_dir}" TRUE)
_copy_tree("${SOURCE_IMAGES_DIR}" "${_destination_images_dir}" FALSE)
_copy_tree("${SOURCE_IMAGES_DIR}/png" "${_destination_images_dir}" FALSE)
_copy_tree("${SOURCE_IMAGES_DIR}/svg" "${_destination_images_dir}" FALSE)
_copy_tree("${SOURCE_IMAGES_DIR}/ico" "${_destination_images_dir}" FALSE)
