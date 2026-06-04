# Helpers for Apple SDK resolution via xcrun.
#
# Always pass --sdk to xcrun explicitly -- otherwise, toolchain and SDK
# are not guaranteed to match.

# Sets WEBKIT_SDK and _sdk_version in the caller's scope.
function(WEBKIT_RESOLVE_SDK)
    foreach (_sdk IN LISTS ARGN)
        execute_process(COMMAND xcrun --sdk ${_sdk} --show-sdk-version
            OUTPUT_VARIABLE _sdk_version
            RESULT_VARIABLE _sdk_result
            OUTPUT_STRIP_TRAILING_WHITESPACE
            ERROR_QUIET)
        if (_sdk_result EQUAL 0 AND _sdk_version)
            set(WEBKIT_SDK "${_sdk}" PARENT_SCOPE)
            set(_sdk_version "${_sdk_version}" PARENT_SCOPE)
            if (_sdk MATCHES "\\.[Ii]nternal$")
                set(USE_APPLE_INTERNAL_SDK ON PARENT_SCOPE)
            else ()
                set(USE_APPLE_INTERNAL_SDK OFF PARENT_SCOPE)
            endif ()
            return ()
        endif ()
    endforeach ()
    message(FATAL_ERROR "xcrun could not locate any SDK in: ${ARGN}")
endfunction()

# Runs `xcrun --sdk ${WEBKIT_SDK} <args>` and capture stdout in OUTPUT_VAR
# (in the caller's scope).
function(WEBKIT_XCRUN OUTPUT_VAR)
    if (NOT WEBKIT_SDK)
        message(FATAL_ERROR "WEBKIT_XCRUN called before WEBKIT_RESOLVE_SDK")
    endif ()
    execute_process(COMMAND xcrun --sdk ${WEBKIT_SDK} ${ARGN}
        OUTPUT_VARIABLE _out
        OUTPUT_STRIP_TRAILING_WHITESPACE
        ERROR_QUIET)
    set(${OUTPUT_VAR} "${_out}" PARENT_SCOPE)
endfunction()
