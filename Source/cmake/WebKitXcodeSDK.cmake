# Helpers and initialization logic for building with Apple SDKs.
#
# Always pass --sdk to xcrun explicitly -- otherwise, toolchain and SDK
# are not guaranteed to match.

# Sets CMAKE_OSX_SYSROOT and _sdk_version in the caller's scope.
function(WEBKIT_RESOLVE_SDK)
    foreach (_sdk IN LISTS ARGN)
        execute_process(COMMAND xcrun --sdk ${_sdk} --show-sdk-path
            OUTPUT_VARIABLE _sdk_path
            RESULT_VARIABLE _sdk_result
            OUTPUT_STRIP_TRAILING_WHITESPACE
            ERROR_QUIET)
        if (_sdk_result EQUAL 0 AND _sdk_path)
            file(READ "${_sdk_path}/SDKSettings.json" _sdk_settings)
            string(JSON _sdk_version GET ${_sdk_settings} Version)
            set(_sdk_version "${_sdk_version}" PARENT_SCOPE)
            string(JSON _sdk_canonical_name GET ${_sdk_settings} CanonicalName)

            message(STATUS "Xcode SDK: ${_sdk_canonical_name} at ${_sdk_path}")
            set(CMAKE_OSX_SYSROOT "${_sdk_path}" CACHE PATH "" FORCE)
            if (_sdk_path MATCHES "\\.[Ii]nternal.sdk$")
                set(USE_APPLE_INTERNAL_SDK ON PARENT_SCOPE)
            else ()
                set(USE_APPLE_INTERNAL_SDK OFF PARENT_SCOPE)
            endif ()
            return()
        endif ()
    endforeach ()
    message(FATAL_ERROR "xcrun could not locate any SDK in: ${ARGN}")
endfunction()

# Runs `xcrun and capture stdout in OUTPUT_VAR
# (in the caller's scope).
function(WEBKIT_XCRUN OUTPUT_VAR)
    if (NOT CMAKE_OSX_SYSROOT)
        message(FATAL_ERROR "WEBKIT_XCRUN called before WEBKIT_RESOLVE_SDK")
    endif ()
    execute_process(COMMAND xcrun ${ARGN}
        OUTPUT_VARIABLE _out
        OUTPUT_STRIP_TRAILING_WHITESPACE
        ERROR_QUIET)
    set(${OUTPUT_VAR} "${_out}" PARENT_SCOPE)
endfunction()

function(WEBKIT_RESOLVE_TOOL OUTPUT_VAR _tool)
    if (DEFINED ${OUTPUT_VAR})
        return()
    endif ()
    WEBKIT_XCRUN(_path -f ${_tool})
    if (NOT EXISTS "${_path}")
        message(SEND_ERROR "Cannot find ${_tool} in the active SDK and "
            "toolchain (${CMAKE_OSX_SYSROOT})")
    else ()
        set(${OUTPUT_VAR} "${_path}" CACHE STRING "" FORCE)
    endif ()
endfunction()

# Run Source/${PROJECT}/Scripts/process-entitlements.sh against an executable
# target and ad-hoc codesign it with the resulting entitlements. Mirrors the
# Xcode build's "Process Entitlements" build phase plus the signing step.
#
# Usage:
#   WEBKIT_PROCESS_ENTITLEMENTS(target PROJECT name [PRODUCT_NAME name])
#
# PROJECT names the directory under Source/ containing Scripts/process-entitlements.sh
# (e.g. JavaScriptCore, WebKit). PRODUCT_NAME defaults to the CMake target name;
# set it explicitly when the script's dispatcher keys off a different name.
function(WEBKIT_PROCESS_ENTITLEMENTS _target)
    cmake_parse_arguments(_arg "" "PROJECT;PRODUCT_NAME" "" ${ARGN})
    if (NOT _arg_PROJECT)
        message(FATAL_ERROR "WEBKIT_PROCESS_ENTITLEMENTS(${_target}): PROJECT is required")
    endif ()
    if (NOT _arg_PRODUCT_NAME)
        set(_arg_PRODUCT_NAME ${_target})
    endif ()
    set(_xcent ${CMAKE_CURRENT_BINARY_DIR}/${_target}.xcent)
    add_custom_command(TARGET ${_target} POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E env
            WK_PROCESSED_XCENT_FILE=${_xcent}
            WK_PLATFORM_NAME=${WEBKIT_SDK_NAME}
            PRODUCT_NAME=${_arg_PRODUCT_NAME}
            ${CMAKE_SOURCE_DIR}/Source/${_arg_PROJECT}/Scripts/process-entitlements.sh
        COMMAND codesign --force --sign - --entitlements ${_xcent} $<TARGET_FILE:${_target}>
        VERBATIM
    )
endfunction()


# ----------------------------------------------------------------------------
# Initialization logic. Sets CMAKE_OSX_SYSROOT if needed, and pins compiler
# tools from the SDK.
# ----------------------------------------------------------------------------

if (CMAKE_OSX_SYSROOT)
    WEBKIT_RESOLVE_SDK(${CMAKE_OSX_SYSROOT})
elseif (PORT STREQUAL "Mac" OR PORT STREQUAL "JSCOnly" OR NOT PORT)
    # FIXME: Support macosx.internal.
    WEBKIT_RESOLVE_SDK(macosx)
elseif (PORT STREQUAL "IOS" AND CMAKE_IOS_SIMULATOR)
    WEBKIT_RESOLVE_SDK(iphonesimulator.internal iphonesimulator)
elseif (PORT STREQUAL "IOS" AND NOT CMAKE_IOS_SIMULATOR)
    WEBKIT_RESOLVE_SDK(iphoneos.internal iphoneos)
else ()
    message(FATAL_ERROR "Building for an Apple platform without an SDK "
        "directory (CMAKE_OSX_SYSROOT) or supported PORT variable.")
endif ()

# Subsequent use of `xcrun` or any of the system Xcode and BSD tools will
# use the selected SDK and toolchain.
set(ENV{SDKROOT} ${CMAKE_OSX_SYSROOT})

WEBKIT_RESOLVE_TOOL(CMAKE_C_COMPILER "clang")
WEBKIT_RESOLVE_TOOL(CMAKE_ASM_COMPILER "clang")
WEBKIT_RESOLVE_TOOL(CMAKE_CXX_COMPILER "clang++")
WEBKIT_RESOLVE_TOOL(CMAKE_OBJC_COMPILER "clang")
WEBKIT_RESOLVE_TOOL(CMAKE_OBJCXX_COMPILER "clang++")
WEBKIT_RESOLVE_TOOL(CMAKE_Swift_COMPILER "swiftc")
WEBKIT_RESOLVE_TOOL(CMAKE_INSTALL_NAME_TOOL "install_name_tool")
WEBKIT_RESOLVE_TOOL(CMAKE_LINKER "ld")
# FIXME: Move these to proper find modules, as they are not part of any CMake
# language.
WEBKIT_RESOLVE_TOOL(GPERF_EXECUTABLE "gperf")
WEBKIT_RESOLVE_TOOL(Mig_EXECUTABLE "mig")
