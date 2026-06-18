# FIXME: re-enable for WPE/GTK once forwarded headers land. https://bugs.webkit.org/show_bug.cgi?id=180063
if (COMPILER_IS_CLANG AND NOT COMPILER_IS_CLANG_CL AND NOT PORT STREQUAL "WPE" AND NOT PORT STREQUAL "GTK")
    set(_USE_HEADER_MAPS_DEFAULT ON)
else ()
    set(_USE_HEADER_MAPS_DEFAULT OFF)
endif ()
option(USE_HEADER_MAPS "Collapse per-target include directories into a Clang header map" ${_USE_HEADER_MAPS_DEFAULT})

if (USE_HEADER_MAPS)
    # One flat directory holds every framework's .hmap and its .hmap.manifest.
    # Cleared once per configure so entries for removed targets don't linger.
    file(REMOVE_RECURSE "${CMAKE_BINARY_DIR}/HeaderMaps")
    file(MAKE_DIRECTORY "${CMAKE_BINARY_DIR}/HeaderMaps")
endif ()

function(WEBKIT_MAKE_HEADER_MAP _target _source_root _dirs_var)
    set(_hmap_dirs)
    set(_result)
    set(_placeholder "@HMAP@")
    cmake_path(SET _root NORMALIZE "${_source_root}")
    cmake_path(SET _build_root NORMALIZE "${CMAKE_BINARY_DIR}")
    foreach (_dir IN LISTS ${_dirs_var})
        cmake_path(SET _dir_n NORMALIZE "${_dir}")
        cmake_path(IS_PREFIX _root "${_dir_n}" _under_source)
        cmake_path(IS_PREFIX _build_root "${_dir_n}" _under_build)
        # Source-tree dirs collapse fully into the hmap so basename + framework-style
        # lookups short-circuit -I scans. Build-tree dirs (e.g. DerivedSources) must
        # stay on -I because unified sources do `#include "inspector/Foo.cpp"` style
        # subpath references that the basename hmap cannot resolve. Headers in those
        # build-tree dirs still get scanned into the hmap so `<Framework/X.h>` works.
        if (_under_source AND NOT "${_dir_n}" STREQUAL "${_root}")
            if (IS_DIRECTORY "${_dir}")
                if (NOT _hmap_dirs)
                    list(APPEND _result "${_placeholder}")
                endif ()
                list(APPEND _hmap_dirs "${_dir}")
            endif ()
        else ()
            list(APPEND _result "${_dir}")
            if (_under_build AND IS_DIRECTORY "${_dir}")
                if (NOT _hmap_dirs)
                    list(APPEND _result "${_placeholder}")
                endif ()
                list(APPEND _hmap_dirs "${_dir}")
            endif ()
        endif ()
    endforeach ()

    if (NOT _hmap_dirs)
        return()
    endif ()

    set(_hmap_file "${CMAKE_BINARY_DIR}/HeaderMaps/${_target}.hmap")
    list(JOIN _hmap_dirs "\n" _dirs_content)
    # Write a self-describing manifest (output path, framework, then dirs). The
    # .hmap files are generated in one batched python pass (WEBKIT_GENERATE_HEADER_MAPS)
    # after all targets are configured. The .hmap path is deterministic, so wire
    # it into this target's include directories now.
    file(WRITE "${CMAKE_BINARY_DIR}/HeaderMaps/${_target}.hmap.manifest"
        "${_hmap_file}\n${_target}\n${_dirs_content}\n")

    list(TRANSFORM _result REPLACE "^${_placeholder}$" "${_hmap_file}")
    set(${_dirs_var} "${_result}" PARENT_SCOPE)
endfunction()

# Generate every framework's .hmap in a single python invocation, reading the
# manifests left in ${CMAKE_BINARY_DIR}/HeaderMaps. Call once after all targets
# are configured. One interpreter launch replaces ~2 per framework.
function(WEBKIT_GENERATE_HEADER_MAPS)
    if (NOT USE_HEADER_MAPS)
        return()
    endif ()
    execute_process(
        COMMAND "${PYTHON_EXECUTABLE}" "${TOOLS_DIR}/Scripts/generate-header-map"
                --header-maps-dir "${CMAKE_BINARY_DIR}/HeaderMaps"
                --root "${CMAKE_SOURCE_DIR}"
                --root "${CMAKE_BINARY_DIR}"
        RESULT_VARIABLE _result
    )
    if (NOT _result EQUAL 0)
        message(WARNING "generate-header-map (batch) failed; targets fall back to -I directories")
    endif ()
endfunction()
