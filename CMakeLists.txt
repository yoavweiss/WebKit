# -----------------------------------------------------------------------------
# Determine CMake version and build type.
# -----------------------------------------------------------------------------
# NOTE: cmake_minimum_required() and project() *MUST* be the two first commands
# used, see https://cmake.org/cmake/help/v3.3/command/project.html -- the
# latter in particular handles loading a bunch of shared CMake definitions
# and loading the cross-compilation settings from CMAKE_TOOLCHAIN_FILE.

cmake_minimum_required(VERSION 3.20)

if (PORT STREQUAL "IOS")
    set(CMAKE_SYSTEM_NAME iOS)
    if (NOT CMAKE_OSX_SYSROOT)
        if (DEFINED ENV{SDKROOT} AND EXISTS "$ENV{SDKROOT}")
            set(CMAKE_OSX_SYSROOT "$ENV{SDKROOT}" CACHE PATH "iOS SDK path" FORCE)
        else ()
            if (CMAKE_IOS_SIMULATOR)
                set(_sdk_name "iphonesimulator.internal")
                set(_sdk_name_fallback "iphonesimulator")
            else ()
                set(_sdk_name "iphoneos.internal")
                set(_sdk_name_fallback "iphoneos")
            endif ()
            execute_process(COMMAND xcrun --sdk ${_sdk_name} --show-sdk-path
                OUTPUT_VARIABLE _ios_sysroot
                OUTPUT_STRIP_TRAILING_WHITESPACE
                RESULT_VARIABLE _internal_sdk_result
                ERROR_QUIET)
            if (NOT _internal_sdk_result EQUAL 0 OR NOT _ios_sysroot)
                execute_process(COMMAND xcrun --sdk ${_sdk_name_fallback} --show-sdk-path
                    OUTPUT_VARIABLE _ios_sysroot
                    OUTPUT_STRIP_TRAILING_WHITESPACE
                    ERROR_QUIET)
            endif ()
            if (_ios_sysroot)
                set(CMAKE_OSX_SYSROOT "${_ios_sysroot}" CACHE PATH "iOS SDK path" FORCE)
            endif ()
        endif ()
    endif ()

    if (NOT CMAKE_OSX_ARCHITECTURES)
        if (CMAKE_OSX_SYSROOT MATCHES "\\.Internal\\.sdk$" AND NOT CMAKE_OSX_SYSROOT MATCHES "[Ss]imulator")
            execute_process(COMMAND uname -m
                OUTPUT_VARIABLE _host_arch
                OUTPUT_STRIP_TRAILING_WHITESPACE)
            if (_host_arch STREQUAL "arm64")
                set(CMAKE_OSX_ARCHITECTURES "arm64e" CACHE STRING "Target architecture" FORCE)
                message(STATUS "iOS: arm64e enabled (internal device SDK detected)")
            endif ()
        else ()
            set(CMAKE_OSX_ARCHITECTURES "arm64" CACHE STRING "Target architecture" FORCE)
        endif ()
    endif ()

    if (NOT CMAKE_OSX_DEPLOYMENT_TARGET)
        set(CMAKE_OSX_DEPLOYMENT_TARGET "18.0" CACHE STRING "Minimum iOS version" FORCE)
    endif ()

    if (NOT CMAKE_SYSTEM_PROCESSOR)
        set(CMAKE_SYSTEM_PROCESSOR "aarch64" CACHE STRING "Target processor" FORCE)
    endif ()
endif ()

if (CMAKE_HOST_SYSTEM_NAME STREQUAL "Darwin" AND NOT CMAKE_SYSTEM_NAME STREQUAL "iOS" AND NOT CMAKE_OSX_SYSROOT)
    # Set CMAKE_OSX_SYSROOT before the project() call below. project() runs ABI
    # detection tests whose results are cached; having the wrong sysroot there
    # would cause those cached results to refer to a different SDK than actual
    # builds use. OptionsMac.cmake (which sets this via WEBKIT_XCRUN) is loaded
    # only after project() via include(WebKitCommon), so it is too late.
    # WEBKIT_XCRUN is also unavailable here because WebKitXcrun.cmake is
    # included by OptionsMac.cmake.
    execute_process(COMMAND xcrun --sdk macosx --show-sdk-path
        OUTPUT_VARIABLE _macos_sysroot
        OUTPUT_STRIP_TRAILING_WHITESPACE
        RESULT_VARIABLE _xcrun_result
        ERROR_QUIET)
    if (_xcrun_result EQUAL 0 AND _macos_sysroot)
        set(CMAKE_OSX_SYSROOT "${_macos_sysroot}" CACHE PATH "macOS SDK path" FORCE)
    endif ()
    unset(_macos_sysroot)
    unset(_xcrun_result)
endif ()

project(WebKit LANGUAGES C CXX)

# -----------------------------------------------------------------------------
# Common configuration
#------------------------------------------------------------------------------
set(CMAKE_MODULE_PATH "${CMAKE_SOURCE_DIR}/Source/cmake")
include(WebKitCommon)

# -----------------------------------------------------------------------------
# Enable Swift - we can't do this until we've checked our configuration,
# which in turn depends on clang-related variables set by the initial
# "project()" line
#------------------------------------------------------------------------------
if (SWIFT_REQUIRED)
    cmake_policy(SET CMP0157 NEW)

    # CMake omits -target for swiftc during iOS cross-compilation.
    if (PORT STREQUAL "IOS" AND NOT CMAKE_Swift_COMPILER_TARGET)
        if (CMAKE_OSX_SYSROOT MATCHES "[Ss]imulator")
            set(CMAKE_Swift_COMPILER_TARGET "${CMAKE_OSX_ARCHITECTURES}-apple-ios${CMAKE_OSX_DEPLOYMENT_TARGET}-simulator" CACHE STRING "Swift target triple" FORCE)
        else ()
            set(CMAKE_Swift_COMPILER_TARGET "${CMAKE_OSX_ARCHITECTURES}-apple-ios${CMAKE_OSX_DEPLOYMENT_TARGET}" CACHE STRING "Swift target triple" FORCE)
        endif ()
    endif ()

    if (PORT STREQUAL "IOS")
        execute_process(COMMAND xcrun --show-sdk-platform-path
            OUTPUT_VARIABLE _ios_platform_path
            OUTPUT_STRIP_TRAILING_WHITESPACE
            ERROR_QUIET)
        if (_ios_platform_path)
            execute_process(COMMAND find "${_ios_platform_path}" -name swiftc -path "*/xctoolchain/*" -type f -print -quit
                OUTPUT_VARIABLE _ios_swiftc
                OUTPUT_STRIP_TRAILING_WHITESPACE
                ERROR_QUIET)
            if (_ios_swiftc AND NOT _ios_swiftc STREQUAL CMAKE_Swift_COMPILER)
                set(CMAKE_Swift_COMPILER "${_ios_swiftc}" CACHE FILEPATH "Swift compiler" FORCE)
                message(STATUS "iOS: using platform-specific Swift compiler: ${_ios_swiftc}")
            endif ()
        endif ()
    endif ()

    enable_language(Swift)

    # -F causes Swift to find C++23 framework headers in implicit module builds.
    if (PORT STREQUAL "IOS")
        set(CMAKE_Swift_FRAMEWORK_SEARCH_FLAG "-Xcc -iquote")
    endif ()

    # Override Swift compiler to a wrapper script to work around the
    # fact CMake feeds incorrect flags to swiftc.
    set(_swift_wrapper "${CMAKE_SOURCE_DIR}/Tools/Scripts/swift/swiftc-wrapper.sh")
    if (CMAKE_Swift_COMPILER STREQUAL _swift_wrapper)
        find_program(ORIGINAL_Swift_COMPILER NAMES swiftc REQUIRED)
    else ()
        set(ORIGINAL_Swift_COMPILER "${CMAKE_Swift_COMPILER}" CACHE FILEPATH "Original Swift compiler" FORCE)
    endif ()
    set(CMAKE_Swift_COMPILER "${_swift_wrapper}")
    add_compile_options($<$<COMPILE_LANGUAGE:Swift>:--original-swift-compiler=${ORIGINAL_Swift_COMPILER}>)
    unset(_swift_wrapper)
endif ()

# -----------------------------------------------------------------------------
# Enable API unit tests and create a target for the test runner
# -----------------------------------------------------------------------------
if (ENABLE_API_TESTS)
    enable_testing()
endif ()

# -----------------------------------------------------------------------------
# Add module directories
# -----------------------------------------------------------------------------
add_subdirectory(Source)

# -----------------------------------------------------------------------------
# Add tools
# -----------------------------------------------------------------------------
if (ENABLE_TOOLS)
    add_subdirectory(Tools)
endif ()

if (DEVELOPER_MODE)
    if (EXISTS "${CMAKE_SOURCE_DIR}/PerformanceTests")
        add_subdirectory(PerformanceTests)
    endif ()
endif ()

# -----------------------------------------------------------------------------
# Print the features list last, for maximum visibility.
# -----------------------------------------------------------------------------
PRINT_WEBKIT_OPTIONS()
