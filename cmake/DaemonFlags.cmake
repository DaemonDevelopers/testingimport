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

include(CheckCCompilerFlag)
include(CheckCXXCompilerFlag)
include(DaemonBuildTypeGeneratorExpression)

add_definitions(-DDAEMON_BUILD_${CMAKE_BUILD_TYPE})

option(USE_COMPILER_INTRINSICS "Enable usage of compiler intrinsics" ON)
mark_as_advanced(USE_COMPILER_INTRINSICS)

if (USE_COMPILER_INTRINSICS)
    add_definitions(-DDAEMON_USE_COMPILER_INTRINSICS=1)
    message(STATUS "Enabling compiler intrinsics")
else()
    message(STATUS "Disabling compiler intrinsics")
endif()

option(USE_COMPILER_CUSTOMIZATION "Enable usage of compiler custom attributes and operators" ON)
mark_as_advanced(USE_COMPILER_CUSTOMIZATION)

if (USE_COMPILER_CUSTOMIZATION)
    add_definitions(-DDAEMON_USE_COMPILER_CUSTOMIZATION=1)
    message(STATUS "Enabling compiler custom attributes and operators")
else()
    message(STATUS "Disabling compiler custom attributes and operators")
endif()

option(USE_RECOMMENDED_CXX_STANDARD "Use recommended C++ standard" ON)
mark_as_advanced(USE_RECOMMENDED_CXX_STANDARD)

# Set flag without checking, optional argument specifies build type
macro(set_c_flag FLAG)
    if (${ARGC} GREATER 1)
        set(CMAKE_C_FLAGS_${ARGV1} "${CMAKE_C_FLAGS_${ARGV1}} ${FLAG}")
    else()
        set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} ${FLAG}")
    endif()
endmacro()
macro(set_cxx_flag FLAG)
    if (${ARGC} GREATER 1)
        set(CMAKE_CXX_FLAGS_${ARGV1} "${CMAKE_CXX_FLAGS_${ARGV1}} ${FLAG}")
    else()
        set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} ${FLAG}")
    endif()
endmacro()
macro(set_c_cxx_flag FLAG)
    set_c_flag(${FLAG} ${ARGN})
    set_cxx_flag(${FLAG} ${ARGN})
endmacro()

macro(set_exe_linker_flag FLAG)
	set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} ${FLAG}")
endmacro()

macro(set_linker_flag FLAG)
    set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} ${FLAG}")
    set(CMAKE_SHARED_LINKER_FLAGS "${CMAKE_SHARED_LINKER_FLAGS} ${FLAG}")
    set(CMAKE_MODULE_LINKER_FLAGS "${CMAKE_MODULE_LINKER_FLAGS} ${FLAG}")
endmacro()

function(try_flag LIST FLAG)
    string(REGEX REPLACE "[/=-]" "_" TEST ${FLAG})
    # Check if the flag begins with '/', meaning it is for MSVC.
    # check_CXX_compiler_flag apparently always fails for MSVC, so accept it without testing.
    # Other compilers might interpret it as a filename so reject without testing.
    string(SUBSTRING "${FLAG}" 0 1 FLAG_FIRST_CHAR)
    if ("${FLAG_FIRST_CHAR}" STREQUAL "/")
        if (MSVC)
            set(${TEST} 1)
        else()
            set(${TEST} 0)
        endif()
    else()
        set(CMAKE_REQUIRED_FLAGS "-Werror")
        check_CXX_compiler_flag("${FLAG}" ${TEST})
        set(CMAKE_REQUIRED_FLAGS "")
    endif()
    if (${TEST})
        set(${LIST} ${${LIST}} ${FLAG} PARENT_SCOPE)
    endif()
endfunction()

# Try flag and set if it works, optional argument specifies build type
macro(try_c_flag PROP FLAG)
    check_C_compiler_flag(${FLAG} FLAG_${PROP})
    if (FLAG_${PROP})
        set_c_flag(${FLAG} ${ARGV2})
    endif()
endmacro()
macro(try_cxx_flag PROP FLAG)
    check_CXX_compiler_flag(${FLAG} FLAG_${PROP})
    if (FLAG_${PROP})
        set_cxx_flag(${FLAG} ${ARGV2})
    endif()
endmacro()
macro(try_c_cxx_flag PROP FLAG)
    # Only try the flag once on the C++ compiler
    try_cxx_flag(${PROP} ${FLAG} ${ARGV2})
    if (FLAG_${PROP})
        set_c_flag(${FLAG} ${ARGV2})
    endif()
endmacro()
# Clang prints a warning when if it doesn't support a flag, so use -Werror to detect
macro(try_cxx_flag_werror PROP FLAG)
    set(CMAKE_REQUIRED_FLAGS "-Werror")
    check_CXX_compiler_flag(${FLAG} FLAG_${PROP})
    set(CMAKE_REQUIRED_FLAGS "")
    if (FLAG_${PROP})
        set_cxx_flag(${FLAG} ${ARGV2})
    endif()
endmacro()
macro(try_c_cxx_flag_werror PROP FLAG)
    try_cxx_flag_werror(${PROP} ${FLAG} ${ARGV2})
    if (FLAG_${PROP})
        set_c_flag(${FLAG} ${ARGV2})
    endif()
endmacro()

macro(try_linker_flag PROP FLAG)
    # Check it with the C compiler
    set(CMAKE_REQUIRED_FLAGS ${FLAG})
    check_C_compiler_flag(${FLAG} FLAG_${PROP})
    set(CMAKE_REQUIRED_FLAGS "")
    if (FLAG_${PROP})
        set_linker_flag(${FLAG} ${ARGN})
    endif()
endmacro()

macro(try_exe_linker_flag PROP FLAG)
	# Check it with the C compiler
	set(CMAKE_REQUIRED_FLAGS ${FLAG})
	check_C_compiler_flag(${FLAG} FLAG_${PROP})
	set(CMAKE_REQUIRED_FLAGS "")

	if (FLAG_${PROP})
		set_exe_linker_flag(${FLAG} ${ARGN})
	endif()
endmacro()

if (BE_VERBOSE)
    set(WARNMODE "no-error=")
else()
    set(WARNMODE "no-")
endif()

# Compiler options
option(USE_FLOAT_EXCEPTIONS "Use floating point exceptions with common.floatException.* cvars" OFF)
option(USE_FAST_MATH "Use fast math" OFF)

if (USE_FLOAT_EXCEPTIONS)
    add_definitions(-DDAEMON_USE_FLOAT_EXCEPTIONS)
endif()

if (MSVC)
    set_c_cxx_flag("/MP")

    if (USE_FAST_MATH)
        set_c_cxx_flag("/fp:fast")
    else()
        # Don't switch on C4305 "truncation from 'double' to 'float'" every
        # time an unsuffixed decimal constant is used
        set_c_cxx_flag("/wd4305")
    endif()

    if (USE_FLOAT_EXCEPTIONS)
        set_c_cxx_flag("/fp:strict")
    endif()

    set_c_cxx_flag("/d2Zi+" RELWITHDEBINFO)

    # https://devblogs.microsoft.com/cppblog/msvc-now-correctly-reports-__cplusplus/
    set_cxx_flag("/Zc:__cplusplus")

    # At least Ninja doesn't remove the /W3 flag when we add /W4|/Wall one, which
    # leads to compilation warnings.  Remove /W3 entirely, as /W4|/Wall be used.
    foreach(flag_var
        CMAKE_C_FLAGS CMAKE_C_FLAGS_DEBUG CMAKE_C_FLAGS_RELEASE
        CMAKE_C_FLAGS_MINSIZEREL CMAKE_C_FLAGS_RELWITHDEBINFO
        CMAKE_CXX_FLAGS CMAKE_CXX_FLAGS_DEBUG CMAKE_CXX_FLAGS_RELEASE
        CMAKE_CXX_FLAGS_MINSIZEREL CMAKE_CXX_FLAGS_RELWITHDEBINFO)
      if (${flag_var} MATCHES "/W3")
        string(REGEX REPLACE "/W3" "" ${flag_var} "${${flag_var}}")
      endif()
    endforeach()

    set_c_cxx_flag("/W4")

    # These warnings need to be disabled for both Daemon and Unvanquished since they are triggered in shared headers
    try_flag(WARNINGS "/wd4201")  # nonstandard extension used: nameless struct / union
    try_flag(WARNINGS "/wd4244")  # 'XXX': conversion from 'YYY' to 'ZZZ', possible loss of data
    try_flag(WARNINGS "/wd4267")  # 'initializing' : conversion from 'size_t' to 'int', possible loss of data

    # This warning is garbage because it doesn't go away if you parenthesize the expression
    try_flag(WARNINGS "/wd4706")  # assignment within conditional expression

    # Turn off warning C4996:, e.g:
    # warning C4996: 'open': The POSIX name for this item is deprecated. Instead, use the ISO C++ conformant name: _open. See online help for details.    set_c_cxx_flag("/wd4996")
    # open seems far more popular than _open not to mention nicer. There doesn't seem to be any reason or will to change to _open.
    # So until there is a specific plan to tackle all of these type of warnings it's best to turn them off to the distraction.
    set_c_cxx_flag("/wd4996")

	if (USE_WERROR)
		try_flag(WARNINGS "/WX")
	endif()

    set_linker_flag("/LARGEADDRESSAWARE")

    if (USE_LTO)
        set_c_cxx_flag("/GL" MINSIZEREL)
        set_c_cxx_flag("/GL" RELWITHDEBINFO)
        set_c_cxx_flag("/GL" RELEASE)
        set_linker_flag("/LTCG" MINSIZEREL)
        set_linker_flag("/LTCG" RELWITHDEBINFO)
        set_linker_flag("/LTCG" RELEASE)
    endif()
else()
	# Minimum language standards supported.

	option(USE_RECOMMENDED_C_STANDARD "Use recommended C standard" ON)
	mark_as_advanced(USE_RECOMMENDED_C_STANDARD)

	if (USE_RECOMMENDED_C_STANDARD)
		# GNU89 or later standard is required when building gzip or the compiler
		# will complain about implicitly defined lseek, read, write and close.
		# GNU99 or later standard is required when building lua or lua will
		# complain that the compiler doesn't support 'long long'.
		try_c_flag(GNU99 "-std=gnu99")
		if (NOT FLAG_GNU99)
			message(FATAL_ERROR "GNU99 is not supported by the compiler")
		endif()
	endif()

	if (USE_RECOMMENDED_CXX_STANDARD)
		# PNaCl only defines isascii if __STRICT_ANSI__ is not defined,
		# always prefer GNU dialect.
		try_cxx_flag(GNUXX14 "-std=gnu++14")
		if (NOT FLAG_GNUXX14)
			try_cxx_flag(GNUXX1Y "-std=gnu++1y")
			if (NOT FLAG_GNUXX1Y)
				message(FATAL_ERROR "GNU++14 is not supported by the compiler")
			endif()
		endif()
	endif()

	if (NACL AND USE_NACL_SAIGO AND SAIGO_ARCH STREQUAL "arm")
		# This should be set for every build type because build type flags
		# are set after the other custom flags and then have the last word.
		# DEBUG should already use -O0 anyway.
		# See: https://github.com/Unvanquished/Unvanquished/issues/3297
		set_c_cxx_flag("-O0" DEBUG)
		set_c_cxx_flag("-O0" RELEASE)
		set_c_cxx_flag("-O0" RELWITHDEBINFO)
		set_c_cxx_flag("-O0" MINSIZEREL)
	endif()

	# Extra debug flags.
	set_c_cxx_flag("-g3" RELWITHDEBINFO)
	set_c_cxx_flag("-g3" DEBUG)

	if (USE_DEBUG_OPTIMIZE)
		try_c_cxx_flag(OPTIMIZE_DEBUG "-Og" DEBUG)
	endif()

	# Optimizations.
	if (USE_FAST_MATH)
		set_c_cxx_flag("-ffast-math")
	endif()

	if (USE_FLOAT_EXCEPTIONS)
		# Floating point exceptions requires trapping math
		# to avoid false positives on architectures with SSE.
		set_c_cxx_flag("-ftrapping-math")
		# GCC prints noisy warnings saying -ftrapping-math implies this.
		set_c_cxx_flag("-fno-associative-math")
		# Other optimizations from -ffast-math can be kept.
	endif()

	# Use hidden symbol visibility if possible.
	try_c_cxx_flag(FVISIBILITY_HIDDEN "-fvisibility=hidden")

	# Disable strict aliasing.
	set_c_cxx_flag("-fno-strict-aliasing")

	# Warning options.
	try_flag(WARNINGS "-Wall")
	try_flag(WARNINGS "-Wextra")

	if (USE_PEDANTIC)
		try_flag(WARNINGS "-pedantic")
	endif()

	if (USE_WERROR)
		try_flag(WARNINGS "-Werror")
	endif()

	if (NACL AND NOT USE_NACL_SAIGO)
		# PNaCl only supports libc++ as standard library.
		set_c_cxx_flag("-stdlib=libc++")
		set_c_cxx_flag("--pnacl-allow-exceptions")
	endif()

	# Prevent the generation of STB_GNU_UNIQUE symbols
	# on templated functions on Linux.
	# STB_GNU_UNIQUE renders dlclose() inoperative.
	try_cxx_flag(FNO_GNU_UNIQUE "-fno-gnu-unique")

	# Use MSVC-compatible bitfield layout
	if (WIN32)
		set_c_cxx_flag("-mms-bitfields")
	endif()

	# Linker flags
	if (NOT APPLE)
		try_linker_flag(LINKER_O1 "-Wl,-O1")
		try_linker_flag(LINKER_SORT_COMMON "-Wl,--sort-common")
		try_linker_flag(LINKER_AS_NEEDED "-Wl,--as-needed")

		if (NOT USE_ADDRESS_SANITIZER)
			try_linker_flag(LINKER_NO_UNDEFINED "-Wl,--no-undefined")
		endif()

		try_linker_flag(LINKER_Z_RELRO "-Wl,-z,relro")
		try_linker_flag(LINKER_Z_NOW "-Wl,-z,now")
	endif()

	if (WIN32)
		try_linker_flag(LINKER_DYNAMICBASE "-Wl,--dynamicbase")
		try_linker_flag(LINKER_NXCOMPAT "-Wl,--nxcompat")
		try_linker_flag(LINKER_LARGE_ADDRESS_AWARE "-Wl,--large-address-aware")
		try_linker_flag(LINKER_HIGH_ENTROPY_VA "-Wl,--high-entropy-va")
	endif()

	if (MINGW AND USE_BREAKPAD)
	    set_linker_flag("-Wl,--build-id")
	endif()

	# The -pthread flag sets some preprocessor defines,
	# it is also used to link with libpthread on Linux.
	if (NOT APPLE)
		try_c_cxx_flag(PTHREAD "-pthread")
	endif()

	if (USE_ADDRESS_SANITIZER)
		set_cxx_flag("-fsanitize=address")
		set_linker_flag("-fsanitize=address")
	endif()

	# Hardening.
	if (USE_HARDENING OR NOT MINGW)
		# MinGW with _FORTIFY_SOURCE and without -fstack-protector
		# causes unsatisfied dependency on libssp.
		# https://github.com/msys2/MINGW-packages/issues/5868
		set_c_cxx_flag("-D_FORTIFY_SOURCE=2" RELEASE)
		set_c_cxx_flag("-D_FORTIFY_SOURCE=2" RELWITHDEBINFO)
		set_c_cxx_flag("-D_FORTIFY_SOURCE=2" MINSIZEREL)
		# Don't set _FORTIFY_SOURCE in debug builds.
	endif()

	try_c_cxx_flag(FPIC "-fPIC")

	if (USE_HARDENING)
		# PNaCl accepts the flags but does not define __stack_chk_guard and __stack_chk_fail.
		if (NOT NACL)
			try_c_cxx_flag(FSTACK_PROTECTOR_STRONG "-fstack-protector-strong")

			if (NOT FLAG_FSTACK_PROTECTOR_STRONG)
				try_c_cxx_flag(FSTACK_PROTECTOR_ALL "-fstack-protector-all")
			endif()
		endif()

		try_c_cxx_flag(FNO_STRICT_OVERFLOW "-fno-strict-overflow")
		try_c_cxx_flag(WSTACK_PROTECTOR "-Wstack-protector")

		if (NOT NACL OR (NACL AND GAME_PIE))
			# The -pie flag requires -fPIC:
			# > ld: error: relocation R_X86_64_64 cannot be used against local symbol; recompile with -fPIC
			# This flag isn't used on macOS:
			# > clang: warning: argument unused during compilation: '-pie' [-Wunused-command-line-argument]
			if (FLAG_FPIC AND NOT APPLE)
				try_exe_linker_flag(LINKER_PIE "-pie")
			endif()
		endif()

		if ("${FLAG_LINKER_PIE}" AND MINGW)
			# https://github.com/msys2/MINGW-packages/issues/4100
			if (ARCH STREQUAL "i686")
				set_linker_flag("-Wl,-e,_mainCRTStartup")
			elseif(ARCH STREQUAL "amd64")
				set_linker_flag("-Wl,-e,mainCRTStartup")
			else()
				message(FATAL_ERROR "Unsupported architecture ${ARCH}")
			endif()
		endif()
	endif()

	# Link-time optimizations. It should be done at the very end because
	# it copies all compiler flags to the linker flags.

	# PNaCl accepts the flag but does nothing with it, underlying clang doesn't support it.
	# Saigo NaCl compiler doesn't support LTO, the flag is accepted but linking fails
	# with “unable to pass LLVM bit-code files to linker” error.
	if (USE_LTO AND NOT NACL)
		try_c_cxx_flag(LTO_AUTO "-flto=auto")

		if (NOT FLAG_LTO_AUTO)
			try_c_cxx_flag(LTO "-flto")
		endif()

		if (FLAG_LTO_AUTO OR FLAG_LTO)
			# Pass all compile flags to the linker.
			set_linker_flag("${CMAKE_CXX_FLAGS}")

			# Use gcc-ar and gcc-ranlib instead of ar and ranlib so that we can use
			# slim LTO objects. This requires a recent version of GCC and binutils.
			if ("${CMAKE_CXX_COMPILER_ID}" STREQUAL GNU)
				if (USE_SLIM_LTO)
					string(REGEX MATCH "^([0-9]+.[0-9]+)" _version "${CMAKE_CXX_COMPILER_VERSION}")
					get_filename_component(COMPILER_BASENAME "${CMAKE_C_COMPILER}" NAME)
					if (COMPILER_BASENAME MATCHES "^(.+-)g?cc(-[0-9]+\\.[0-9]+\\.[0-9]+)?(\\.exe)?$")
						set(TOOLCHAIN_PREFIX ${CMAKE_MATCH_1})
					endif()

					find_program(GCC_AR
						NAMES
							"${TOOLCHAIN_PREFIX}gcc-ar"
							"${TOOLCHAIN_PREFIX}gcc-ar-${_version}"
						DOC "gcc provided wrapper for ar which adds the --plugin option"
					)

					find_program(GCC_RANLIB
						NAMES
							"${TOOLCHAIN_PREFIX}gcc-ranlib"
							"${TOOLCHAIN_PREFIX}gcc-ranlib-${_version}"
						DOC "gcc provided wrapper for ranlib which adds the --plugin option"
					)

					mark_as_advanced(GCC_AR GCC_RANLIB)

					# Override standard ar and ranlib with the gcc- versions.
					if (GCC_AR)
						set(CMAKE_AR ${GCC_AR})
					endif()

					if (GCC_RANLIB)
						set(CMAKE_RANLIB ${GCC_RANLIB})
					endif()

					try_c_cxx_flag(NO_FAT_LTO_OBJECTS "-fno-fat-lto-objects")
				else()
					try_c_cxx_flag(FAT_LTO_OBJECTS "-ffat-lto-objects")
				endif()
			endif()
		endif()
	endif()
endif()

option(USE_CPU_RECOMMENDED_FEATURES "Use some common hardware features like SSE2, NEON, VFP, MCX16, etc." ON)

# Target options.
if (MSVC)
    if (ARCH STREQUAL "i686")
        if (USE_CPU_RECOMMENDED_FEATURES)
            set_c_cxx_flag("/arch:SSE2") # This is the default
        else()
            set_c_cxx_flag("/arch:IA32") # minimum
        endif()
    endif()
elseif (NOT NACL)
	# Among the required hardware features, the NX bit (No eXecute bit)
	# feature may be required for NativeClient to work. Some early
	# Intel EM64T processors are known to not implement the NX bit.
	# Some ARM CPUs may not implement the similar “execute never” feature.
	# While this is an hardware feature, this isn't a build option because it's
	# not used by the engine itself but by the NaCl loader. The engine will be
	# built but the game may or may not run. The NX bit feature may also be
	# disabled in the BIOS while the hardware supports it, so even if we would
	# detect the lack of it such knowledge would be useless at build time.
	# Running a server with a native executable game is also a valid usage
	# not requiring the NX bit.

	if (ARCH STREQUAL "amd64")
		# K8 or EM64T minimum: AMD Athlon 64 ClawHammer, Intel Xeon Nocona, Intel Pentium 4 model F (Prescott revision EO), VIA Nano.
		if (DAEMON_CXX_COMPILER_ICC)
			set(GCC_GENERIC_ARCH "pentium4")
		elseif (DAEMON_CXX_COMPILER_Zig)
			set(GCC_GENERIC_ARCH "x86_64")
		else()
			set(GCC_GENERIC_ARCH "x86-64")
		endif()
		set(GCC_GENERIC_TUNE "generic")
	elseif (ARCH STREQUAL "i686")
		# P6 or K6 minimum: Intel Pentium Pro, AMD K6, Via Cyrix III, Via C3.
		set(GCC_GENERIC_ARCH "i686")
		set(GCC_GENERIC_TUNE "generic")
	elseif (ARCH STREQUAL "arm64")
		# Armv8-A minimum: Cortex-A50.
		set(GCC_GENERIC_ARCH "armv8-a")
		set(GCC_GENERIC_TUNE "generic")
	elseif (ARCH STREQUAL "armhf")
		# Armv7-A minimum with VFPv3 and optional NEONv1: Cortex-A5.
		# Hard float ABI (mainstream 32-bit ARM Linux distributions).
		# An FPU should be explicitly set on recent compilers or this
		# error would be raised:
		#   cc1: error: ‘-mfloat-abi=hard’: selected architecture
		#   lacks an FPU
		set(GCC_GENERIC_ARCH "armv7-a+fp")
		set(GCC_GENERIC_TUNE "generic-armv7-a")
	elseif (ARCH STREQUAL "armel")
		# Armv6 minimum with optional VFP: ARM11.
		# Soft float ABI (previous mainstream 32-bit ARM Linux
		# distributions, mainstream 32-bit ARM Android distributions).
		set(GCC_GENERIC_ARCH "armv6")
		# There is no generic tuning option for armv6.
		unset(GCC_GENERIC_TUNE)
	else()
		message(WARNING "Unknown architecture ${ARCH}")
	endif()

	if ("${DAEMON_CXX_COMPILER_NAME}" STREQUAL "Zig")
		unset(GCC_GENERIC_TUNE)
	endif()

	option(USE_CPU_GENERIC_ARCHITECTURE "Enforce generic -march and -mtune compiler options" ON)
	if (USE_CPU_GENERIC_ARCHITECTURE)
		try_c_cxx_flag_werror(MARCH "-march=${GCC_GENERIC_ARCH}")

		if (GCC_GENERIC_TUNE)
			try_c_cxx_flag_werror(MTUNE "-mtune=${GCC_GENERIC_TUNE}")
		endif()
	endif()

	if (USE_CPU_RECOMMENDED_FEATURES)
		if (ARCH STREQUAL "amd64")
			# CMPXCHG16B minimum (x86-64-v2): AMD64 revision F.
			try_c_cxx_flag_werror(MCX16 "-mcx16")
		elseif (ARCH STREQUAL "i686")
			# SSE2 minimum: Intel Pentium 4 (Prescott),
			# Intel Pentium M (Banias), AMD K8, Via C7.
			try_c_cxx_flag_werror(MSSE2 "-msse2")
			try_c_cxx_flag_werror(MFPMATH_SSE "-mfpmath=sse")
		elseif (ARCH STREQUAL "armhf")
			# NEONv1 minimum.
			try_c_cxx_flag_werror(MFPU_NEON "-mfpu=neon")
		elseif (ARCH STREQUAL "armel")
			# VFP minimum, hard float with soft float ABI.
			try_c_cxx_flag_werror(MFPU_VFP "-mfpu=vfp")
			try_c_cxx_flag_werror(MFLOAT_ABI_SOFTFP "-mfloat-abi=softfp")
		endif()
	endif()
endif()

# Windows-specific definitions
if (WIN32)
    add_definitions(
        -DWINVER=0x501  # Minimum Windows version: XP
        -DWIN32         # Define WIN32 for compatibility (compiler defines _WIN32)
        -DNOMINMAX      # Define NOMINMAX to prevent conflics between std::min/max and the min/max macros in WinDef.h
        -DSTRICT        # Enable STRICT type checking for windows.h
    )
    set(CMAKE_FIND_LIBRARY_PREFIXES ${CMAKE_FIND_LIBRARY_PREFIXES} "" "lib")
endif()

if (MSVC)
    add_definitions(-D_CRT_SECURE_NO_WARNINGS)
endif()

# Mac-specific definitions
if (APPLE)
    add_definitions(-DMACOS_X)
    set(CMAKE_INSTALL_RPATH "@executable_path")
    set(CMAKE_BUILD_WITH_INSTALL_RPATH ON)
endif()

# Configuration specific definitions

# This stupid trick to define THIS_IS_NOT_A_DEBUG_BUILD (rather than nothing) in the non-debug case
# is so that it doesn't break the hacky gcc/clang PCH code which reads all the definitions
# and prefixes "-D" to them.
set_property(DIRECTORY APPEND PROPERTY COMPILE_DEFINITIONS
             $<$<NOT:${DEBUG_GENEXP_COND}>:THIS_IS_NOT_A_>DEBUG_BUILD)
