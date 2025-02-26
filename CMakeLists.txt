# Daemon BSD Source Code
# Copyright (c) 2013-2016, Daemon Developers
# All rights reserved.
#
# Redistribution and use in source and binary forms, with or without
# modification, are permitted provided that the following conditions are met:
#  * Redistributions of source code must retain the above copyright
#    notice, this list of conditions and the following disclaimer.
#  * Redistributions in binary form must reproduce the above copyright
#    notice, this list of conditions and the following disclaimer in the
#    documentation and/or other materials provided with the distribution.
#  * Neither the name of the <organization> nor the
#    names of its contributors may be used to endorse or promote products
#    derived from this software without specific prior written permission.
#
# THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND
# ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
# WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
# DISCLAIMED. IN NO EVENT SHALL <COPYRIGHT HOLDER> BE LIABLE FOR ANY
# DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
# (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
# LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
# ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
# (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
# SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

cmake_minimum_required (VERSION 3.12)

# Choose Python versions by location instead of getting whatever is in PYTHON_EXECUTABLE
# Can be removed once we require CMake 3.15
if (POLICY CMP0094)
    cmake_policy(SET CMP0094 NEW)
endif()

if (POLICY CMP0072)
    cmake_policy(SET CMP0072 NEW)
endif()

if(NOT DAEMON_DIR)
    set(DAEMON_DIR "${CMAKE_CURRENT_SOURCE_DIR}")
endif()

set(CMAKE_MODULE_PATH ${CMAKE_CURRENT_SOURCE_DIR}/cmake ${CMAKE_MODULE_PATH})
get_directory_property(DAEMON_PARENT_SCOPE_DIR PARENT_DIRECTORY)
if (DAEMON_PARENT_SCOPE_DIR)
    # Append Daemon module path to parent project's one only when Daemon is subproject.
    # See https://stackoverflow.com/questions/25199677/how-to-detect-if-current-scope-has-a-parent-in-cmake
    set(CMAKE_MODULE_PATH ${CMAKE_MODULE_PATH} ${CMAKE_CURRENT_SOURCE_DIR}/cmake PARENT_SCOPE)
endif()

include(CMakeDependentOption)

# This line has to appear before 'PROJECT' in order to be able to disable incremental linking
set(MSVC_INCREMENTAL_DEFAULT ON)
# Enable the creation of project folders for Visual Studio projects
set_property(GLOBAL PROPERTY USE_FOLDERS ON)

project(Daemon C CXX)

# Default to Release builds. To use system CFLAGS only (for distro builds), set CMAKE_BUILD_TYPE to None
if (NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE "Release" CACHE STRING "Choose the type of build, options are: None(CMAKE_CXX_FLAGS or CMAKE_C_FLAGS used) Debug Release RelWithDebInfo MinSizeRel." FORCE)
endif()

# On Mac, default to x86_64 unless user requests something else
if (NOT CMAKE_OSX_ARCHITECTURES)
    set(CMAKE_OSX_ARCHITECTURES x86_64)
endif()

# FIXME: this reports an unused architecture if only vms are built.
# We may not be able to avoid this for now without doing a huge rewrite
# of this file.
# FIXME: this reports an unused compiler if only vms are built.
# We cannot avoid this for now without doing a huge rewrite of this files
# since we also set useless flags using informations provided by this.
if (NOT (BUILD_CLIENT OR BUILD_TTY_CLIENT OR BUILD_SERVER OR BUILD_DUMMY_APP))
    message(NOTICE "You can safely ignore the following reported architecture, it is not used.")
    message(NOTICE "You can safely ignore the following reported compilers, they are unused.")
endif()

if (Daemon_OUT)
    set(CMAKE_CURRENT_BINARY_DIR ${Daemon_OUT})
    set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${Daemon_OUT})
    set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${Daemon_OUT})
    set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${Daemon_OUT})
endif()

include(DaemonBuildInfo)
include(DaemonPlatform)

################################################################################
# Configuration options
################################################################################

if(NOT DAEMON_EXTERNAL_APP)
    option(BUILD_CLIENT "Engine client executable, required to play" ON)
    option(BUILD_SERVER "Engine server executable, required to host servers" ON)
    option(BUILD_TTY_CLIENT "Engine client with no graphical display" ON)
    option(BUILD_DUMMY_APP "Stripped-down engine executable, mostly used to ease incremental porting and debugging" OFF)
    mark_as_advanced(BUILD_DUMMY_APP)

    set(NACL_RUNTIME_PATH "" CACHE STRING "Directory containing the NaCl binaries")

    if (WIN32)
        # The alternative code is based on non-curses unix terminal functions.
        set(USE_CURSES ON)
    else()
        if (NOT APPLE)
            # Not supported on macOS because the included version is too old.
            option(USE_CURSES_NCURSES "Use ncurses instead of PDCursesMod" ON)
        endif()

        option(USE_CURSES "Enable fancy colors in terminal's output" ON)
    endif()

    if (BUILD_CLIENT OR BUILD_TTY_CLIENT)
        option(USE_MUMBLE "Build Daemon with mumblelink sumpport" ON)
    endif()

    cmake_dependent_option(USE_SMP "Compile with support for running the renderer in a separate thread" 1 BUILD_CLIENT 0)
    option(USE_BREAKPAD "Generate Daemon crash dumps (which require Breakpad tools to read)" OFF)
endif()

option(BUILD_TESTS "Build unit test applications" OFF)

option(USE_LTO "Use link-time optimization for release builds" OFF)
cmake_dependent_option(USE_SLIM_LTO "Generate slim LTO objects, improves build times" 1 "USE_LTO AND \"${CMAKE_CXX_COMPILER_ID}\" STREQUAL GNU" 0)
option(USE_HARDENING "Use stack protection and other hardening flags" OFF)
option(USE_WERROR "Tell the compiler to make the build fail when warnings are present" OFF)
option(USE_PEDANTIC "Tell the compiler to be pedantic" OFF)
option(USE_DEBUG_OPTIMIZE "Try to optimize the debug build" ON)

if (CMAKE_GENERATOR MATCHES "^Ninja")
    # It isn't currently supported with Ninja yet:
    #
    # > Note that the IMPLICIT_DEPENDS option is currently supported only for
    # > Makefile generators and will be ignored by other generators. Note that
    # > the IMPLICIT_DEPENDS option is currently supported only for Makefile
    # > generators and will be ignored by other generators.
    # > -- https://cmake.org/cmake/help/latest/command/add_custom_command.html
    #
    # It would be better to be able to know if IMPLICIT_DEPENDS is supported
    # by the current generator instead of comparing generator name.
    option(USE_PRECOMPILED_HEADER "Improve build times by using a precompiled header" OFF)
else()
    option(USE_PRECOMPILED_HEADER "Improve build times by using a precompiled header" ON)
endif()
option(USE_ADDRESS_SANITIZER "Try to use the address sanitizer" OFF)
option(BE_VERBOSE "Tell the compiler to report all warnings" OFF)
option(USE_STATIC_LIBS "Tries to use static libs where possible. Only works for Linux" OFF)

if (NOT DAEMON_PARENT_SCOPE_DIR)
    option(BUILD_DUMMY_GAMELOGIC "Build dummy cgame" OFF)
endif()

# Game VM modules are built with a recursive invocation of CMake, by which all the configuration
# options are lost, except ones we explicitly choose to pass.
set(DEFAULT_NACL_VM_INHERITED_OPTIONS
    BE_VERBOSE
    BUILD_CGAME
    BUILD_SGAME
    CMAKE_BUILD_TYPE
    DAEMON_CBSE_PYTHON_PATH
    USE_ARCH_INTRINSICS
    USE_COMPILER_CUSTOMIZATION
    USE_COMPILER_INTRINSICS
    USE_DEBUG_OPTIMIZE
    USE_HARDENING
    USE_PEDANTIC
    USE_PRECOMPILED_HEADER
    USE_RECOMMENDED_C_STANDARD
    USE_RECOMMENDED_CXX_STANDARD
    USE_WERROR
)
set(NACL_VM_INHERITED_OPTIONS "${DEFAULT_NACL_VM_INHERITED_OPTIONS}" CACHE STRING
    "Semicolon-separated list of options for which NaCl game VMs should use the same value as the other binaries")
mark_as_advanced(NACL_VM_INHERITED_OPTIONS)

################################################################################
# Directories
################################################################################

set(MOUNT_DIR ${CMAKE_CURRENT_SOURCE_DIR}/src)
if (DAEMON_PARENT_SCOPE_DIR)
    set(MOUNT_DIR ${MOUNT_DIR} PARENT_SCOPE)
endif()
include_directories(${MOUNT_DIR})
set(OBJ_DIR ${CMAKE_BINARY_DIR}${CMAKE_FILES_DIRECTORY})
if (DAEMON_PARENT_SCOPE_DIR)
    set(OBJ_DIR ${OBJ_DIR} PARENT_SCOPE)
endif()
set(ENGINE_DIR ${MOUNT_DIR}/engine)
if (DAEMON_PARENT_SCOPE_DIR)
    set(ENGINE_DIR ${ENGINE_DIR} PARENT_SCOPE)
endif()
set(COMMON_DIR ${MOUNT_DIR}/common)
if (DAEMON_PARENT_SCOPE_DIR)
    set(COMMON_DIR ${COMMON_DIR} PARENT_SCOPE)
endif()
set(LIB_DIR ${CMAKE_CURRENT_SOURCE_DIR}/libs)
if (DAEMON_PARENT_SCOPE_DIR)
    set(LIB_DIR ${LIB_DIR} PARENT_SCOPE)
endif()
include_directories(${LIB_DIR})
if (CMAKE_CFG_INTDIR STREQUAL ".")
    set(FULL_OUTPUT_DIR ${CMAKE_BINARY_DIR})
else()
    set(FULL_OUTPUT_DIR ${CMAKE_BINARY_DIR}/${CMAKE_CFG_INTDIR})
endif()

# Dependencies version, this must match the number in external_deps/build.sh
set(DEPS_VERSION 10)

option(USE_EXTERNAL_DEPS "Download or reuse dependencies from EXTERNAL_DEPS_DIR (mandatory for building and running NaCl .nexe binaries)." ON)

if (USE_EXTERNAL_DEPS AND NOT NACL)
    set(EXTERNAL_DEPS_DIR "${CMAKE_CURRENT_SOURCE_DIR}/external_deps" CACHE STRING "Directory in which to store the downloaded dependencies.")

    set(DEPS_EXT ".tar.xz")

    if (WIN32)
        set(DEPS_SYSTEM windows)
        set(SUPPORTED_${DEPS_SYSTEM}_ARCH amd64 i686)
    elseif (APPLE)
        set(DEPS_SYSTEM macos)
        set(SUPPORTED_${DEPS_SYSTEM}_ARCH amd64)
    elseif (LINUX OR FREEBSD)
        set(DEPS_SYSTEM linux)
        set(SUPPORTED_${DEPS_SYSTEM}_ARCH amd64 i686 arm64 armhf)
    endif()

    if (MSVC)
        set(DEPS_COMPILER msvc)
    elseif (WIN32)
        set(DEPS_COMPILER mingw)
    else()
        set(DEPS_COMPILER default)
    endif()

    if (DEPS_SYSTEM AND ARCH IN_LIST SUPPORTED_${DEPS_SYSTEM}_ARCH)
        set(DEPS_DIR ${EXTERNAL_DEPS_DIR}/${DEPS_SYSTEM}-${ARCH}-${DEPS_COMPILER}_${DEPS_VERSION})

        if (DAEMON_PARENT_SCOPE_DIR)
            set(DEPS_DIR ${DEPS_DIR} PARENT_SCOPE)
        endif()
    else()
        message(WARNING "No dependencies are provided for this system, Native Client is likely unsupported.")
    endif()
endif()

################################################################################
# Compile and link flags
################################################################################

# FIXME: This sets unused flags for an unused compiler if only nacl vms
# are built and we are not yet running the chainloaded build. We cannot
# avoid this for now or we would have to do a large rewrite of that file.
# FIXME: This defines the try_flag function that is used in
# Unvanquished CMakeLists.txt even if the engine is not built,
# meaning we have to detect useless engine compiler to set
# useless engine flags when only building vms.
include(DaemonFlags)

# FIXME: Once engine flags would be only set when building the engine
# this would be likely unused by vms as they are built by DaemonGame.
# Warning options (for Daemon only)
# Note the different scopes used for warning options:
# * set_c_cxx_flag(xxx) or try_c_cxx_flag(xxx) sets it for all code including dependencies
# * try_flag(WARNINGS xxx) in DaemonFlags.cmake sets it for Daemon and Unvanquished but not deps
# * try_flag(WARNINGS xxx) below sets it for Daemon only

try_flag(WARNINGS           "-Wshadow=local")
try_flag(WARNINGS           "-Wno-pragmas")
try_flag(WARNINGS           "-Wno-unknown-pragmas")
try_flag(WARNINGS           "-W${WARNMODE}old-style-cast")
try_flag(WARNINGS           "-Woverloaded-virtual")
try_flag(WARNINGS           "-Wstrict-null-sentinel")
try_flag(WARNINGS           "-W${WARNMODE}sign-compare")

# MSVC /wd = warning disable
try_flag(WARNINGS "/wd4127")  # conditional expression is constant
try_flag(WARNINGS "/wd4324")  # 'XXX': structure was padded due to alignment specifier
try_flag(WARNINGS "/wd4458")  # declaration of 'XXX' hides class member
try_flag(WARNINGS "/wd4459")  # declaration of 'XXX' hides global declaration
try_flag(WARNINGS "/wd4701")  # potentially uninitialized local variable 'XXX' used
try_flag(WARNINGS "/wd26495") # Variable 'XXX' is uninitialized. Always initialize a member variable.

################################################################################
# Group the sources by folder to have folder show in Visual Studio
################################################################################

macro(group_dir_sources dir)
    file(GLOB_RECURSE files ${dir}/*)
    string(LENGTH ${dir}/ dir_length)
    foreach(file ${files})
        if (NOT IS_DIRECTORY ${dir}/${file})
            get_filename_component(group_name ${file} DIRECTORY)
            string(SUBSTRING ${group_name} ${dir_length} -1 group_name)
            string(REPLACE "/" "\\" group_name ${group_name})
            source_group(${group_name} FILES ${file})
        endif()
    endforeach()
endmacro()

if (MSVC)
    group_dir_sources(${MOUNT_DIR})
endif()

################################################################################
# Support for precompiled headers
################################################################################

# MSVC requires that an extra file be added to a project
if (USE_PRECOMPILED_HEADER AND MSVC)
    file(WRITE ${OBJ_DIR}/PrecompiledHeader.cpp "")
    set(PCH_FILE ${OBJ_DIR}/PrecompiledHeader.cpp)
    if (DAEMON_PARENT_SCOPE_DIR)
        set(PCH_FILE ${PCH_FILE} PARENT_SCOPE)
    endif()
endif()

function(ADD_PRECOMPILED_HEADER Target)
    if (NOT USE_PRECOMPILED_HEADER OR CMAKE_VERSION VERSION_LESS 2.8.10)
        return()
    endif()

    # Get the common compile flags
    set(Flags ${CMAKE_CXX_FLAGS} ${WARNINGS})
    get_target_property(Type ${Target} TYPE)
    if (Type STREQUAL MODULE_LIBRARY)
        set(Flags ${Flags} ${CMAKE_SHARED_MODULE_CXX_FLAGS})
    endif()
    separate_arguments(Flags)

    # Get the per-configuration compile flags
    foreach(Config DEBUG RELEASE RELWITHDEBINFO MINSIZEREL)
        set(ConfigFlags ${CMAKE_CXX_FLAGS_${Config}})
        separate_arguments(ConfigFlags)
        foreach(Flag ${ConfigFlags})
            set(Flags ${Flags} $<${${Config}_GENEXP_COND}:${Flag}>)
        endforeach()
    endforeach()

    # Get preprocessor options for the target and directory (global)
    get_directory_property(DirCompileDefs COMPILE_DEFINITIONS)
    get_directory_property(DirIncludeDirs INCLUDE_DIRECTORIES)
    get_target_property(TargetCompileDefs ${Target} COMPILE_DEFINITIONS)
    get_target_property(TargetIncludeDirs ${Target} INCLUDE_DIRECTORIES)
    set(Defs)
    foreach(Def ${TargetCompileDefs})
        set(Defs ${Defs} -D${Def})
    endforeach()
    foreach(Def ${DirCompileDefs})
        set(Defs ${Defs} -D${Def})
    endforeach()
    foreach(Def ${DirIncludeDirs})
        set(Defs ${Defs} -I${Def})
    endforeach()
    foreach(Def ${TargetIncludeDirs})
        set(Defs ${Defs} -I${Def})
    endforeach()

    # Specify minimum OSX version
    if (APPLE AND CMAKE_OSX_DEPLOYMENT_TARGET)
        set(Flags ${Flags} -mmacosx-version-min=${CMAKE_OSX_DEPLOYMENT_TARGET})
    endif()

    set(Header ${COMMON_DIR}/Common.h)

    # Compiler-specific PCH support
    if ((DAEMON_CXX_COMPILER_GCC_COMPATIBILITY OR DAEMON_CXX_COMPILER_Clang_COMPATIBILITY)
    AND NOT DAEMON_CXX_COMPILER_ICC)
        # CMAKE_CXX_COMPILER_ARG1 is used with compilers using subcommands.
        # For example when doing: cmake -D'CMAKE_CXX_COMPILER'='zig;c++'
        # CMAKE_CXX_COMPILER will be "zig",
        # CMAKE_CXX_COMPILER_ARG1 will be "c++".
        add_custom_command(OUTPUT "${OBJ_DIR}/${Target}.h.gch"
            COMMAND ${PNACLPYTHON_PREFIX2} ${CMAKE_CXX_COMPILER} ${CMAKE_CXX_COMPILER_ARG1}
                ${Defs} ${Flags} -x c++-header ${Header} -o "${OBJ_DIR}/${Target}.h.gch"
            DEPENDS ${Header}
            IMPLICIT_DEPENDS CXX ${Header}
        )

        add_custom_target(${Target}-pch DEPENDS "${OBJ_DIR}/${Target}.h.gch")
        add_dependencies(${Target} ${Target}-pch)

        # PNaCl clang doesn't support -include-pch properly
        if (DAEMON_CXX_COMPILER_Clang_COMPATIBILITY
        AND NOT DAEMON_CXX_COMPILER_PNaCl)
            set_property(TARGET ${Target} APPEND PROPERTY COMPILE_OPTIONS "-include-pch;${OBJ_DIR}/${Target}.h.gch")
        else()
            set_property(TARGET ${Target} APPEND PROPERTY COMPILE_OPTIONS "-include;${OBJ_DIR}/${Target}.h;-Winvalid-pch")
        endif()
    elseif (MSVC)
        # /Fp sets the PCH path used by either of the /Yc and /Yu options.
        # /Yc overrides /Yu.
        set_source_files_properties(${PCH_FILE} PROPERTIES COMPILE_FLAGS "/Yc${Header}")
        target_compile_options(${Target} PRIVATE "/Yu${Header}" "/Fp${OBJ_DIR}/${Target}_$<CONFIG>.pch" "/FI${Header}")
    endif()
endfunction()

################################################################################
# File lists
################################################################################

include(srclibs.cmake)
include(src.cmake)

# tinyformat is a header-only library
set(COMMONLIST ${COMMONLIST} ${TINYFORMATLIST})

################################################################################
# Libraries
################################################################################

# Import external dependencies
if (DEPS_DIR)
    # Warn on old version of deps
    get_cmake_property(cacheVariableNames CACHE_VARIABLES)
    foreach(cacheVariableName ${cacheVariableNames})
        set(OLD_VERSION "NOTFOUND")
        set(cacheVariableValue "${${cacheVariableName}}")

        if (cacheVariableValue MATCHES "/(linux64|macosx64|mingw32|mingw64|msvc32|msvc64)-([0-9]+)/")
            if (NOT ${CMAKE_MATCH_2} EQUAL ${DEPS_VERSION})
                set(OLD_VERSION ${CMAKE_MATCH_2})
            endif()
        endif()

        if (cacheVariableValue MATCHES "/(linux|windows|macos)-(i686|amd64)-(default|mingw|msvc)_([0-9]+)/")
            if (NOT ${CMAKE_MATCH_4} EQUAL ${DEPS_VERSION})
                set(OLD_VERSION ${CMAKE_MATCH_4})
            endif()
        endif()

        if(OLD_VERSION)
            message(WARNING "You seem to be using a dependency from version ${OLD_VERSION}"
                            " of the external_deps package, but the latest version is"
                            " ${DEPS_VERSION}. Please delete the build directory (${CMAKE_BINARY_DIR})"
                            " and reconfigure to upgrade to the latest dependencies. (Based on"
                            " ${cacheVariableName}=${cacheVariableValue})")
            break()
        endif()
    endforeach()

    # Download them if they not available
    if (NOT EXISTS ${DEPS_DIR})
        file(MAKE_DIRECTORY ${EXTERNAL_DEPS_DIR})
        get_filename_component(BASENAME ${DEPS_DIR} NAME)
        set(REMOTE "https://dl.unvanquished.net/deps/${BASENAME}${DEPS_EXT}")
        message(STATUS "Downloading dependencies from '${REMOTE}'")
        file(DOWNLOAD ${REMOTE} ${OBJ_DIR}/${BASENAME}${DEPS_EXT}
            SHOW_PROGRESS
            STATUS DOWNLOAD_RESULT
            LOG DOWNLOAD_LOG
        )
        list(GET DOWNLOAD_RESULT 0 DOWNLOAD_STATUS)
        list(GET DOWNLOAD_RESULT 1 DOWNLOAD_STRING)
        if (NOT DOWNLOAD_STATUS EQUAL 0)
            message(FATAL_ERROR "Error downloading '${REMOTE}':
                Status code: ${DOWNLOAD_STATUS}
                Error string: ${DOWNLOAD_STRING}
                Download log: ${DOWNLOAD_LOG}"
            )
        endif()
        message(STATUS "Download completed successfully")

        # Extract the downloaded archive
        execute_process(
            COMMAND ${CMAKE_COMMAND} -E tar xJf ${OBJ_DIR}/${BASENAME}${DEPS_EXT}
            WORKING_DIRECTORY ${EXTERNAL_DEPS_DIR}
            RESULT_VARIABLE EXTRACT_RESULT
        )
        if (NOT EXTRACT_RESULT EQUAL 0)
            message(FATAL_ERROR "Could not extract ${BASENAME}${DEPS_EXT}")
        endif()
    endif()

    # Add to paths
    set(CMAKE_FIND_ROOT_PATH ${DEPS_DIR} ${CMAKE_FIND_ROOT_PATH})
    set(CMAKE_INCLUDE_PATH ${DEPS_DIR} ${DEPS_DIR}/include ${CMAKE_INCLUDE_PATH})
    set(CMAKE_FRAMEWORK_PATH ${DEPS_DIR} ${CMAKE_FRAMEWORK_PATH})
    set(CMAKE_PREFIX_PATH ${DEPS_DIR} ${CMAKE_PREFIX_PATH})
    if (DAEMON_PARENT_SCOPE_DIR)
        # Also set parent scope so the top level CMakeLists can find precompiled deps
        set(CMAKE_FIND_ROOT_PATH ${DEPS_DIR} ${CMAKE_FIND_ROOT_PATH} PARENT_SCOPE)
        set(CMAKE_INCLUDE_PATH ${DEPS_DIR} ${DEPS_DIR}/include ${CMAKE_INCLUDE_PATH} PARENT_SCOPE)
        set(CMAKE_FRAMEWORK_PATH ${DEPS_DIR} ${CMAKE_FRAMEWORK_PATH} PARENT_SCOPE)
        set(CMAKE_PREFIX_PATH ${DEPS_DIR} ${CMAKE_PREFIX_PATH} PARENT_SCOPE)
    endif()
endif()

# These variables are defined:
# LIBS_BASE, LIBS_ENGINE_BASE LIBS_ENGINE, LIBS_BASECLIENT, LIBS_CLIENT

# Native client
include(DaemonNacl)
if (NACL)
    add_library(srclibs-nacl-module EXCLUDE_FROM_ALL ${NACLLIST_MODULE})
    set_target_properties(srclibs-nacl-module PROPERTIES POSITION_INDEPENDENT_CODE ${GAME_PIE} FOLDER "libs")
    set(LIBS_BASE ${LIBS_BASE} srclibs-nacl-module)
else()
    add_library(srclibs-nacl-native EXCLUDE_FROM_ALL ${NACLLIST_NATIVE})
    set_target_properties(srclibs-nacl-native PROPERTIES POSITION_INDEPENDENT_CODE 1 FOLDER "libs")
    if (APPLE)
        # Do not error for OSAtomic* deprecation notices
        target_compile_options(srclibs-nacl-native PRIVATE "-Wno-error=deprecated-declarations")
    endif()
    set(LIBS_BASE ${LIBS_BASE} srclibs-nacl-native)
endif()

# Base OS libs
if (WIN32)
    set(LIBS_BASE ${LIBS_BASE} winmm ws2_32)
elseif (NACL)
    find_library(NACL_EXCEPTION nacl_exception)
    find_library(NACL_MINIDUMP minidump_generator)

    # HACK: To be removed when Saigo is repackaged properly.
    if (NOT NACL_MINIDUMP
    # NACL_TARGET is specific to Saigo, PNaCl target is le32 and doesn't set NACL_TARGET.
    AND "${NACL_TARGET}" STREQUAL "i686"
    # x86_64-nacl/lib32 is specific to Google build, other builds may provide i686-nacl/lib instead.
    AND EXISTS "${DEPS_DIR}/saigo_newlib/x86_64-nacl/lib32")
        set(NACL_EXCEPTION "${DEPS_DIR}/saigo_newlib/x86_64-nacl/lib32/libnacl_exception.a")
        set(NACL_MINIDUMP "${DEPS_DIR}/saigo_newlib/x86_64-nacl/lib32/libminidump_generator.a")
    endif()

    set(LIBS_BASE ${LIBS_BASE} ${NACL_MINIDUMP} ${NACL_EXCEPTION})
else()
    find_library(LIBM m)
    if (LIBM)
        set(LIBS_BASE ${LIBS_BASE} ${LIBM})
    endif()
    find_library(LIBRT rt)
    if (LIBRT)
        set(LIBS_BASE ${LIBS_BASE} ${LIBRT})
    endif()
    find_library(LIBICONV iconv)
    if (LIBICONV)
        set(LIBS_ENGINE ${LIBS_ENGINE} ${LIBICONV})
    endif()
    mark_as_advanced(LIBM LIBRT LIBICONV)
    set(LIBS_BASE ${LIBS_BASE} ${CMAKE_DL_LIBS})
    find_package(Threads REQUIRED)
    set(LIBS_BASE ${LIBS_BASE} ${CMAKE_THREAD_LIBS_INIT})
    if (APPLE)
        set(LIBS_CLIENT ${LIBS_CLIENT} "-framework Carbon" "-framework IOKit" "-framework Cocoa")
    endif()
endif()

# Minizip
if (BUILD_CLIENT OR BUILD_TTY_CLIENT OR BUILD_SERVER OR BUILD_DUMMY_APP)
    add_library(srclibs-minizip EXCLUDE_FROM_ALL ${MINIZIPLIST})
    set_target_properties(srclibs-minizip PROPERTIES POSITION_INDEPENDENT_CODE 1 FOLDER "libs")
    set(LIBS_BASE ${LIBS_BASE} srclibs-minizip)
    if (MSVC)
        target_compile_options(srclibs-minizip PRIVATE "/wd4100") # disable "unreferenced formal parameter"
    endif()
endif()

# Look for OpenGL here before we potentially switch to looking for static libs.
if (BUILD_CLIENT)
    if (LINUX OR FREEBSD)
        # Set LEGACY OpenGL ABI if the variable is not explictly set.
        # The backward-incompatible GLVND OpenGL ABI should only be used
        # by developers who have a good reason to require it.
        # See: https://github.com/DaemonEngine/Daemon/issues/945#issuecomment-1835091256
        set(OpenGL_GL_PREFERENCE "LEGACY" CACHE STRING "Linux OpenGL ABI (LEGACY or GLVND).")
    endif()

    find_package(OpenGL REQUIRED)
    include_directories(${OPENGL_INCLUDE_DIR})
    set(LIBS_CLIENT ${LIBS_CLIENT} OpenGL::GL)

    # When OpenGL_GL_PREFERENCE is set but neither GLVND nor LEGACY are available,
    # This code is unreachable since it already errored out at find_package time.
    # This code only executes if OpenGL is supported, either because the GLVND ABI
    # is available, either because the LEGACY ABI is available.
    if (LINUX OR FREEBSD)
        if (NOT OPENGL_gl_LIBRARY)
            # When OpenGL_GL_PREFERENCE is supported and GLVND is available
            # OPENGL_gl_LIBRARY is expected to be empty. So we can use the
            # emptyness of OPENGL_gl_LIBRARY to detect that CMake supports
            # OpenGL_GL_PREFERENCE and GLVND is used:
            # > If the GLVND OpenGL and GLX libraries are available, prefer them.
            # > This forces OPENGL_gl_LIBRARY to be empty.
            # -- https://cmake.org/cmake/help/latest/module/FindOpenGL.html
            set(OpenGL_selected_ABI "GLVND")
            set(OpenGL_selected_LIBRARY "${OPENGL_opengl_LIBRARY}")
        else()
            # When OpenGL_GL_PREFERENCE is not supported or GLVND is not available
            # the OPENGL_gl_LIBRARY is always set, so if it is set, it means the ABI
            # is the LEGACY one.
            set(OpenGL_selected_ABI "LEGACY")
            set(OpenGL_selected_LIBRARY "${OPENGL_gl_LIBRARY}")
        endif()

        if (OpenGL_selected_ABI STREQUAL "GLVND")
            message(WARNING "The GLVND OpenGL ABI is backward incompatible with legacy drivers.\nSee https://github.com/DaemonEngine/Daemon/issues/945")
        endif()

        if (OpenGL_GL_PREFERENCE STREQUAL "GLVND")
            if (NOT OPENGL_opengl_LIBRARY)
                message(WARNING "GLVND OpenGL ABI not available but LEGACY is.\nTry OpenGL_GL_PREFERENCE=LEGACY")
                message(FATAL_ERROR "Could NOT find GLVND libOpenGL.so")
            endif()
        else()
            if (NOT OPENGL_gl_LIBRARY)
                message(WARNING "LEGACY OpenGL ABI not available but GLVND is.\nTry OpenGL_GL_PREFERENCE=GLVND")
                message(FATAL_ERROR "Could NOT find LEGACY libGL.so")
            endif()
        endif()

        message(STATUS "Found OpenGL ABI: ${OpenGL_selected_ABI}")
        message(STATUS "Found OpenGL library: ${OpenGL_selected_LIBRARY}")

        daemon_add_buildinfo("char*" "DAEMON_OPENGL_ABI_STRING" "\"${OpenGL_selected_ABI}\"")
        add_definitions("-DDAEMON_OPENGL_ABI_${OpenGL_selected_ABI}")
        add_definitions("-DDAEMON_OPENGL_ABI")
    endif()
endif()

# Prefer static libs
if ( USE_STATIC_LIBS )
    if (LINUX OR FREEBSD)
        set(CMAKE_FIND_LIBRARY_SUFFIXES .a ${CMAKE_FIND_LIBRARY_SUFFIXES})
    endif()
endif()

# zlib
if (BUILD_CLIENT OR BUILD_TTY_CLIENT OR BUILD_SERVER OR BUILD_DUMMY_APP)
    find_package(ZLIB REQUIRED)
    set(LIBS_BASE ${LIBS_BASE} ${ZLIB_LIBRARIES})
    include_directories(${ZLIB_INCLUDE_DIRS})
endif()

if (DAEMON_PARENT_SCOPE_DIR)
    set(LIBS_BASE ${LIBS_BASE} PARENT_SCOPE)
endif()

# Curses, pdcurses on Windows and ncursesw on Unix
if (BUILD_CLIENT OR BUILD_TTY_CLIENT OR BUILD_SERVER OR BUILD_DUMMY_APP)
    if (USE_CURSES)
        if (USE_CURSES_NCURSES)
            # Tells FindCurses that ncurses is required.
            set(CURSES_NEED_NCURSES 1)

            add_definitions(-DUSE_CURSES -DUSE_CURSES_NCURSES)
            find_package(CursesW REQUIRED)
            set(LIBS_ENGINE_BASE ${LIBS_ENGINE_BASE} ${CURSESW_LIBRARIES})
            include_directories(${CURSESW_INCLUDE_DIR})
        else ()
            if (WIN32)
                set(LIBS_ENGINE_BASE ${LIBS_ENGINE_BASE} gdi32 comdlg32)
            endif()

            add_definitions(-DPDC_WIDE -DPDC_FORCE_UTF8 -DPDC_RGB -DUSE_CURSES)
            add_library(srclibs-pdcurses EXCLUDE_FROM_ALL ${PDCURSESLIST})
            set_target_properties(srclibs-pdcurses PROPERTIES POSITION_INDEPENDENT_CODE 1 FOLDER "libs")
            set(LIBS_ENGINE_BASE ${LIBS_ENGINE_BASE} srclibs-pdcurses)
            include_directories(${LIB_DIR}/pdcursesmod)
        endif()
    endif()
endif()

if (BUILD_CLIENT OR BUILD_TTY_CLIENT OR BUILD_SERVER OR BUILD_DUMMY_APP)
    if (NACL_RUNTIME_PATH)
        daemon_add_buildinfo("char*" "DAEMON_NACL_RUNTIME_PATH_STRING" "\"${NACL_RUNTIME_PATH}\"")
        add_definitions("-DDAEMON_NACL_RUNTIME_PATH")
    endif()

    # FindLocale
    add_library(srclibs-findlocale EXCLUDE_FROM_ALL ${FINDLOCALELIST})
    set_target_properties(srclibs-findlocale PROPERTIES POSITION_INDEPENDENT_CODE 1 FOLDER "libs")
    set(LIBS_ENGINE ${LIBS_ENGINE} srclibs-findlocale)

    # Nettle
    find_package(Nettle REQUIRED)
    include_directories(${NETTLE_INCLUDE_DIR})
    set(LIBS_ENGINE ${LIBS_ENGINE} ${NETTLE_LIBRARIES})

    # GMP
    find_package(GMP REQUIRED)
    include_directories(${GMP_INCLUDE_DIR})
    set(LIBS_ENGINE ${LIBS_ENGINE} ${GMP_LIBRARIES})
endif()

if (BUILD_CLIENT OR BUILD_TTY_CLIENT)
    # CURL
    find_package(CURL REQUIRED)
    include_directories(${CURL_INCLUDE_DIRS})
    set(LIBS_CLIENTBASE ${LIBS_CLIENTBASE} ${CURL_LIBRARIES})

    if (USE_MUMBLE)
        # Mumble link
        add_library(srclibs-mumblelink EXCLUDE_FROM_ALL ${MUMBLELINKLIST})
        set_target_properties(srclibs-mumblelink PROPERTIES POSITION_INDEPENDENT_CODE 1 FOLDER "libs")
        set(LIBS_CLIENTBASE ${LIBS_CLIENTBASE} srclibs-mumblelink)
    endif()
endif()

# SDL, required for all targets on win32 because of iconv and SDL_SetHint(SDL_TIMER_RESOLUTION, 0)
if (BUILD_CLIENT OR WIN32)
    find_package(SDL2 CONFIG)

    if (SDL2_SDL2main_FOUND)
        mark_as_advanced(SDL2_DIR)
    else()
        # We cannot use REQUIRED because it would look
        # for OldSDL2_FOUND instead of SDL2_FOUND.
        find_package(OldSDL2)

        if (NOT SDL2_FOUND)
            message(FATAL_ERROR "Could NOT find SDL2")
        endif()

        include_directories(${SDL2_INCLUDE_DIR})

        mark_as_advanced(SDL2MAIN_LIBRARY SDL2_LIBRARY SDL2_INCLUDE_DIR)
    endif ()

    if (WIN32)
        set(LIBS_ENGINE_BASE ${LIBS_ENGINE_BASE}
            $<$<TARGET_EXISTS:SDL2::SDL2main>:SDL2::SDL2main> SDL2::SDL2)
    else()
        set(LIBS_CLIENT ${LIBS_CLIENT}
            $<$<TARGET_EXISTS:SDL2::SDL2main>:SDL2::SDL2main> SDL2::SDL2)
    endif()
endif()

# Breakpad
if (USE_BREAKPAD)
    add_definitions(-DUSE_BREAKPAD)
    include_directories(${LIB_DIR}/breakpad/src)

    if (WIN32)
        add_library(srclibs-breakpad-common EXCLUDE_FROM_ALL ${BREAKPAD_COMMON_LIST})
        add_library(srclibs-breakpad-crash_generation_server EXCLUDE_FROM_ALL ${BREAKPAD_CRASHGENERATIONSERVER_LIST})
        add_library(srclibs-breakpad-exception_handler EXCLUDE_FROM_ALL ${BREAKPAD_EXCEPTIONHANDLER_LIST})
        add_library(srclibs-breakpad-crash_generation_client EXCLUDE_FROM_ALL ${BREAKPAD_CRASHGENERATIONCLIENT_LIST})

        set(BREAKPAD_LIBRARIES
            srclibs-breakpad-common
            srclibs-breakpad-crash_generation_server
            srclibs-breakpad-exception_handler
            srclibs-breakpad-crash_generation_client
        )
        foreach(breaklib ${BREAKPAD_LIBRARIES})
            set_target_properties(${breaklib} PROPERTIES POSITION_INDEPENDENT_CODE 1 FOLDER "libs")

            # Breakpad library assumes wide char versions of Windows API functions
            target_compile_definitions(${breaklib} PRIVATE UNICODE)

            target_compile_definitions(${breaklib} PRIVATE $<$<CONFIG:Debug>:_DEBUG>)
        endforeach(breaklib)

        # WIN32 option prevents a console from being created for the process
        add_executable(crash_server WIN32 src/engine/crash_server/crash_server_windows.cpp)
        target_link_libraries(crash_server
            srclibs-breakpad-crash_generation_server srclibs-breakpad-common)
        target_compile_definitions(crash_server PRIVATE UNICODE)

        set(LIBS_ENGINE_BASE ${LIBS_ENGINE_BASE} srclibs-breakpad-exception_handler
            srclibs-breakpad-crash_generation_client srclibs-breakpad-common)

    elseif (LINUX)
        add_library(srclibs-breakpad EXCLUDE_FROM_ALL ${BREAKPAD_LIST})
        set_target_properties(srclibs-breakpad PROPERTIES POSITION_INDEPENDENT_CODE 1 FOLDER "libs")
        set(LIBS_ENGINE ${LIBS_ENGINE} srclibs-breakpad)

        add_executable(crash_server src/engine/crash_server/crash_server_linux.cpp)
        target_link_libraries(crash_server srclibs-breakpad)
    endif()
endif()

option(PREFER_EXTERNAL_LIBS "Tries to use system libs where possible." ON)

macro(prefer_package LIB_NAME LIB_CMAKE)
    if (PREFER_EXTERNAL_LIBS AND NOT NACL)
        find_package(${LIB_NAME})

        if (NOT ${LIB_NAME}_FOUND)
            message(WARNING "PREFER_EXTERNAL_LIBS is enabled but external ${LIB_NAME} is not found, falling back to vendored ${LIB_NAME}.")
        endif()
    endif()

    if (NOT ${LIB_NAME}_FOUND)
        include(${LIB_CMAKE})
    endif()
endmacro()

if (BUILD_CLIENT)
    find_package(Ogg REQUIRED)
    include_directories(${OGG_INCLUDE_DIRS})
    set(LIBS_CLIENT ${LIBS_CLIENT} ${OGG_LIBRARIES})

    find_package(Vorbis REQUIRED)
    include_directories(${VORBIS_INCLUDE_DIRS})
    set(LIBS_CLIENT ${LIBS_CLIENT} ${VORBIS_LIBRARIES})

    find_package(Opus REQUIRED)
    include_directories(${OPUS_INCLUDE_DIRS})
    set(LIBS_CLIENT ${LIBS_CLIENT} ${OPUS_LIBRARIES})

    find_package(WebP REQUIRED)
    include_directories(${WEBP_INCLUDE_DIRS})
    set(LIBS_CLIENT ${LIBS_CLIENT} ${WEBP_LIBRARIES})

    find_package(JPEG REQUIRED)
    include_directories(${JPEG_INCLUDE_DIR})
    set(LIBS_CLIENT ${LIBS_CLIENT} ${JPEG_LIBRARIES})

    find_package(PNG REQUIRED)
    include_directories(${PNG_INCLUDE_DIRS})
    set(LIBS_CLIENT ${LIBS_CLIENT} ${PNG_LIBRARIES})

    prefer_package(Freetype ${DAEMON_DIR}/freetype.cmake)

    include_directories(${FREETYPE_INCLUDE_DIRS})
    set(LIBS_CLIENT ${LIBS_CLIENT} ${FREETYPE_LIBRARIES})

    find_package(GLEW REQUIRED)
    include_directories(${GLEW_INCLUDE_DIR})
    set(LIBS_CLIENT ${LIBS_CLIENT} ${GLEW_LIBRARIES})

    set(backup_find_framework ${CMAKE_FIND_FRAMEWORK})
    set(CMAKE_FIND_FRAMEWORK NEVER) # Mac's system OpenAL doesn't have efx.h
    find_package(OpenAL REQUIRED)
    set(CMAKE_FIND_FRAMEWORK ${backup_find_framework})
    include_directories(${OPENAL_INCLUDE_DIR})
    set(LIBS_CLIENT ${LIBS_CLIENT} ${OPENAL_LIBRARY})
endif()

if (BUILD_TESTS)
    set(BUILD_GMOCK ON)
    set(INSTALL_GTEST OFF)
    set(gtest_force_shared_crt ON)
    add_subdirectory(${LIB_DIR}/googletest EXCLUDE_FROM_ALL)
    unset(BUILD_GMOCK CACHE)
    unset(INSTALL_GTEST CACHE)
    set_target_properties(gmock PROPERTIES FOLDER "libs")
endif()

################################################################################
# Engine
################################################################################
macro(AddApplicationInternal Target Executable)
    add_executable(${Target} ${Sources})
    target_link_libraries(${Target} ${A_Target}-objects)

    if (DEPS_DIR)
        add_dependencies(${Target} runtime_deps)
    endif()

    set_property(TARGET ${Target} APPEND PROPERTY COMPILE_OPTIONS ${A_Flags})
    set_property(TARGET ${Target} APPEND PROPERTY INCLUDE_DIRECTORIES ${ENGINE_DIR} ${MOUNT_DIR} ${LIB_DIR})
    set_property(TARGET ${Target} APPEND PROPERTY COMPILE_DEFINITIONS ${A_Definitions})
    set_target_properties(${Target} PROPERTIES OUTPUT_NAME "${Executable}" PREFIX "" FOLDER "engine")

    # Append Windows specific manifests.
    # Adding the .manifest as a source works better for MSVC, but it has no
    # effect for MinGW (https://gitlab.kitware.com/cmake/cmake/-/issues/23244).
    if (MINGW)
        target_sources(${Target} PRIVATE ${ENGINE_DIR}/sys/windows-resource/manifest.rc)
    elseif (WIN32)
        target_sources(${Target} PRIVATE ${ENGINE_DIR}/sys/windows-resource/supported-os.manifest)
    endif()

    message(STATUS ${Target})
endmacro()

function(AddApplication)
    set(oneValueArgs Target ExecutableName)
    set(multiValueArgs ApplicationMain Definitions Flags Files Libs Tests)
    cmake_parse_arguments(A "" "${oneValueArgs}" "${multiValueArgs}" ${ARGN})

    # Reuse object files between the real application and the test one
    add_library(${A_Target}-objects OBJECT EXCLUDE_FROM_ALL ${A_Files} ${PCH_FILE})
    target_link_libraries(${A_Target}-objects engine-lib ${A_Libs} ${LIBS_BASE})
    set_property(TARGET ${A_Target}-objects APPEND PROPERTY COMPILE_OPTIONS ${A_Flags})
    set_property(TARGET ${A_Target}-objects APPEND PROPERTY INCLUDE_DIRECTORIES ${ENGINE_DIR} ${MOUNT_DIR} ${LIB_DIR})
    set_property(TARGET ${A_Target}-objects APPEND PROPERTY COMPILE_DEFINITIONS ${A_Definitions})
    set_target_properties(${A_Target}-objects PROPERTIES FOLDER "engine/objects")

    set(Sources WIN32 ${A_ApplicationMain})
    AddApplicationInternal(${A_Target} ${A_ExecutableName})
    if (BUILD_TESTS)
        # TODO disable dameon assert macros in test files
        set(A_Definitions ${A_Definitions} PRODUCE_TEST_APPLICATION DAEMON_SKIP_ASSERT_SHORTHANDS)
        # Note that unit tests must be added directly as sources to the executable, not via
        # a static library, because when GCC is linking a library, it drops
        # object files without a referenced symbol, unless you prevent it with
        # -Wl,--whole-archive -l<library path> -Wl,--no-whole-archive
        set(Sources ${A_ApplicationMain} ${A_Tests})
        AddApplicationInternal(test-${A_Target} test-${A_Target})
        target_link_libraries(test-${A_Target} GTest::gmock)
    endif()

    ADD_PRECOMPILED_HEADER(${A_Target}-objects)
endfunction()

daemon_write_buildinfo()

if (NOT NACL)
    add_library(engine-lib EXCLUDE_FROM_ALL ${PCH_FILE} ${BUILDINFOLIST} ${COMMONLIST} ${ENGINELIST})
    target_link_libraries(engine-lib ${LIBS_BASE} ${LIBS_ENGINE_BASE})
    set_property(TARGET engine-lib APPEND PROPERTY COMPILE_DEFINITIONS BUILD_ENGINE)
    set_property(TARGET engine-lib APPEND PROPERTY INCLUDE_DIRECTORIES ${ENGINE_DIR} ${MOUNT_DIR} ${LIB_DIR})
    set_property(TARGET engine-lib APPEND PROPERTY COMPILE_OPTIONS ${WARNINGS})
    set_target_properties(engine-lib PROPERTIES FOLDER "engine/objects")
    ADD_PRECOMPILED_HEADER(engine-lib)

    if (BUILD_DUMMY_APP)
        AddApplication(
            Target dummyapp
            ExecutableName dummyapp
            Definitions USELESS_DEFINITION_TO_AVOID_PCH_ISSUE
            ApplicationMain ${ENGINE_DIR}/null/NullApplication.cpp
            Flags ${WARNINGS}
            Files ${COMMON_DIR}/Util.h  # must be nonempty
            Tests ${ENGINETESTLIST}
        )
    endif()
endif()

if (BUILD_CLIENT)
    set(Definitions
        BUILD_ENGINE BUILD_GRAPHICAL_CLIENT
    )
    if (USE_SMP)
        list(APPEND Definitions USE_SMP)
    endif()
    AddApplication(
        Target client
        ExecutableName daemon
        ApplicationMain ${ENGINE_DIR}/client/ClientApplication.cpp
        Definitions ${Definitions}
        Flags ${WARNINGS}
        Files ${WIN_RC} ${BUILDINFOLIST} ${QCOMMONLIST} ${SERVERLIST} ${CLIENTBASELIST} ${CLIENTLIST}
        Libs ${LIBS_CLIENT} ${LIBS_CLIENTBASE} ${LIBS_ENGINE}
        Tests ${ENGINETESTLIST}
    )

    # generate glsl include files
    set(GLSL_SOURCE_DIR ${ENGINE_DIR}/renderer/glsl_source)
    set(EMBED_INCLUDE_DIR ${CMAKE_CURRENT_BINARY_DIR}/embed_data)
    file(MAKE_DIRECTORY ${EMBED_INCLUDE_DIR})
    set_property(TARGET client-objects APPEND PROPERTY INCLUDE_DIRECTORIES ${EMBED_INCLUDE_DIR})

    foreach(res ${GLSLSOURCELIST})
        get_filename_component(filename_no_ext ${res} NAME_WE)
        set(outpath ${EMBED_INCLUDE_DIR}/${filename_no_ext}.glsl.h)
        add_custom_command(
            OUTPUT ${outpath}
            COMMAND ${CMAKE_COMMAND} "-DINPUT_FILE=${res}" "-DOUTPUT_FILE=${outpath}"
                "-DVARIABLE_NAME=${filename_no_ext}_glsl" -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/EmbedText.cmake
            MAIN_DEPENDENCY ${res}
        )
        set_property(TARGET client-objects APPEND PROPERTY SOURCES ${outpath})
    endforeach()
endif()

if (BUILD_SERVER)
    AddApplication(
        Target server
        ExecutableName daemonded
        ApplicationMain ${ENGINE_DIR}/server/ServerApplication.cpp
        Definitions BUILD_ENGINE BUILD_SERVER
        Flags ${WARNINGS}
        Files ${WIN_RC} ${BUILDINFOLIST} ${QCOMMONLIST} ${SERVERLIST} ${DEDSERVERLIST}
        Libs ${LIBS_ENGINE}
        Tests ${ENGINETESTLIST}
    )
endif()

if (BUILD_TTY_CLIENT)
    AddApplication(
        Target ttyclient
        ExecutableName daemon-tty
        ApplicationMain ${ENGINE_DIR}/client/ClientApplication.cpp
        Definitions BUILD_ENGINE BUILD_TTY_CLIENT
        Flags ${WARNINGS}
        Files ${WIN_RC} ${BUILDINFOLIST} ${QCOMMONLIST} ${SERVERLIST} ${CLIENTBASELIST} ${TTYCLIENTLIST}
        Libs ${LIBS_CLIENTBASE} ${LIBS_ENGINE}
        Tests ${ENGINETESTLIST}
    )
endif()

################################################################################
# Runtime dependencies
################################################################################

if (DEPS_DIR AND (BUILD_CLIENT OR BUILD_TTY_CLIENT OR BUILD_SERVER OR BUILD_DUMMY_APP))
    add_custom_target(runtime_deps)
    set_target_properties(runtime_deps PROPERTIES FOLDER "CMakePlumbing")

    # The NaCl loader and IRT are required to load .nexe files
    add_custom_command(TARGET runtime_deps PRE_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            ${DEPS_DIR}/nacl_loader${CMAKE_EXECUTABLE_SUFFIX}
            ${FULL_OUTPUT_DIR}/nacl_loader${CMAKE_EXECUTABLE_SUFFIX}
    )

    add_custom_command(TARGET runtime_deps PRE_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            ${DEPS_DIR}/irt_core-${NACL_ARCH}.nexe
            ${FULL_OUTPUT_DIR}/irt_core-${NACL_ARCH}.nexe
    )

    # Linux uses a bootstrap program to reserve address space
    if (LINUX OR FREEBSD)
        if (ARCH STREQUAL "arm64")
            add_executable(nacl_helper_bootstrap-armhf tools/nacl_helper_bootstrap-armhf/nacl_helper_bootstrap-armhf.cpp)
            add_dependencies(runtime_deps nacl_helper_bootstrap-armhf)

            add_custom_command(TARGET runtime_deps PRE_BUILD
                COMMAND ${CMAKE_COMMAND} -E make_directory
                    ${FULL_OUTPUT_DIR}/lib-armhf
            )

            add_custom_command(TARGET runtime_deps PRE_BUILD
                COMMAND ${CMAKE_COMMAND} -E copy_directory
                    ${DEPS_DIR}/lib-armhf
                    ${FULL_OUTPUT_DIR}/lib-armhf
            )
        endif()

        add_custom_command(TARGET runtime_deps PRE_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                ${DEPS_DIR}/nacl_helper_bootstrap
                ${FULL_OUTPUT_DIR}/nacl_helper_bootstrap
        )
    endif()

    # Win32 requires nacl_loader_amd64.exe in order to run on Win64
    if (WIN32 AND ARCH STREQUAL i686)
        add_custom_command(TARGET runtime_deps PRE_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                ${DEPS_DIR}/nacl_loader-amd64${CMAKE_EXECUTABLE_SUFFIX}
                ${FULL_OUTPUT_DIR}/nacl_loader-amd64${CMAKE_EXECUTABLE_SUFFIX}
        )
        add_custom_command(TARGET runtime_deps PRE_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                ${DEPS_DIR}/irt_core-amd64.nexe
                ${FULL_OUTPUT_DIR}/irt_core-amd64.nexe
        )
    endif()

    # Mac requires some libraries from external_deps
    if (APPLE)
        add_custom_command(TARGET runtime_deps PRE_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_directory
                ${DEPS_DIR}/SDL2.framework
                ${FULL_OUTPUT_DIR}/SDL2.framework
        )
        file(GLOB RUNTIME_LIBS ${DEPS_DIR}/lib/*.dylib)
        foreach(RUNTIME_LIB ${RUNTIME_LIBS})
            add_custom_command(TARGET runtime_deps PRE_BUILD
                COMMAND ${CMAKE_COMMAND} -E copy_if_different
                    ${RUNTIME_LIB}
                    ${FULL_OUTPUT_DIR}
            )
        endforeach()
    endif()

    # Windows requires some libraries from external_deps
    if (WIN32)
        file(GLOB RUNTIME_LIBS ${DEPS_DIR}/bin/*.dll ${DEPS_DIR}/SDL2/lib/*/SDL2.dll)
        foreach(RUNTIME_LIB ${RUNTIME_LIBS})
            add_custom_command(TARGET runtime_deps PRE_BUILD
                COMMAND ${CMAKE_COMMAND} -E copy_if_different
                    ${RUNTIME_LIB}
                    ${FULL_OUTPUT_DIR}
            )
        endforeach()
    endif()
endif()

if (BUILD_DUMMY_GAMELOGIC)
    include(src/dummygame/CMakeLists.txt)
endif()
