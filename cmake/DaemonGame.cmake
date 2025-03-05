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

include_directories(${MOUNT_DIR} ${LIB_DIR} ${LIB_DIR}/zlib)

## About the different ways to host/play games:
## Native DLL: no sandboxing, no cleaning up but debugger support. Use for dev.
## NaCl exe: sandboxing, no leaks, slightly slower, hard to debug. Use for regular players.
## Native exe: no sandboxing, no leaks, hard to debug. Might be used by server owners for perf.
## See VirtualMachine.h for code.

# can be loaded by daemon with vm.[sc]game.type 3
option(BUILD_GAME_NATIVE_DLL "Build the shared library files, mostly useful for debugging changes locally." ON)

# can be loaded by daemon with vm.[sc]game.type 2
option(BUILD_GAME_NATIVE_EXE "Build native executable, which might be used for better performances by server owners" OFF)

include(DaemonBuildInfo)
include(DaemonPlatform)

# Do not report unused native compiler if native vms are not built.
# If only NACL vms are built, this will be reported in chainloaded build.
if (BUILD_GAME_NATIVE_DLL OR BUILD_GAME_NATIVE_EXE OR NACL)
    include(DaemonNacl)
    include(DaemonFlags)
endif()

# Source lists for src/shared
set(SHAREDLIST
    ${MOUNT_DIR}/shared/CommandBufferClient.cpp
    ${MOUNT_DIR}/shared/CommandBufferClient.h
    ${MOUNT_DIR}/shared/CommonProxies.cpp
    ${MOUNT_DIR}/shared/CommonProxies.h
    ${MOUNT_DIR}/shared/VMMain.cpp
    ${MOUNT_DIR}/shared/VMMain.h
)
set(SHAREDLIST_cgame
    ${MOUNT_DIR}/shared/client/cg_api.cpp ${MOUNT_DIR}/shared/client/cg_api.h
)
set(SHAREDLIST_sgame
    ${MOUNT_DIR}/shared/server/sg_api.cpp ${MOUNT_DIR}/shared/server/sg_api.h
)

# Function to setup all the Sgame/Cgame libraries
include(CMakeParseArguments)

# The NaCl SDK only runs on amd64 or i686.
if (CMAKE_SYSTEM_NAME STREQUAL CMAKE_HOST_SYSTEM_NAME
AND (ARCH STREQUAL "amd64" OR ARCH STREQUAL "i686"))
	# can be loaded by daemon with vm.[sc]game.type 0 or 1
	option(BUILD_GAME_NACL "Build the NaCl \"pexe\" and \"nexe\" gamelogic modules for enabled architecture targets, required to host mods." OFF)

	set(NACL_ALL_TARGETS "amd64;i686;armhf")
	set(BUILD_GAME_NACL_TARGETS "all" CACHE STRING "Enabled NaCl \"nexe\" architecture targets, values: ${NACL_ALL_TARGETS}, all, native, none.")
	mark_as_advanced(BUILD_GAME_NACL_TARGETS)

	if (BUILD_GAME_NACL_TARGETS STREQUAL "all")
		set(NACL_TARGETS "${NACL_ALL_TARGETS}")
	elseif (BUILD_GAME_NACL_TARGETS STREQUAL "native")
		set(NACL_TARGETS "${ARCH}")
	elseif (BUILD_GAME_NACL_TARGETS STREQUAL "none")
		set(NACL_TARGETS "")
	else()
		set(NACL_TARGETS "${BUILD_GAME_NACL_TARGETS}")
	endif()

	foreach(NACL_TARGET ${NACL_TARGETS})
		set(IS_NACL_VALID_TARGET OFF)
		foreach(NACL_VALID_TARGET ${NACL_ALL_TARGETS})
			if(NACL_TARGET STREQUAL NACL_VALID_TARGET)
				set(IS_NACL_VALID_TARGET ON)
			endif()
		endforeach()

		if (NOT IS_NACL_VALID_TARGET)
			message(FATAL_ERROR "Invalid NaCl target ${NACL_TARGET}, must be one of ${NACL_ALL_TARGETS}")
		endif()
	endforeach()
else()
	set(BUILD_GAME_NACL OFF)
	set(NACL_TARGETS "")
endif()

daemon_write_buildinfo("Game")

function(buildGameModule module_slug)
	set(module_target "${GAMEMODULE_NAME}-${module_slug}")

	set(module_target_args "${module_target}" ${PCH_FILE} ${GAMEMODULE_FILES} ${SHAREDLIST_${GAMEMODULE_NAME}} ${SHAREDLIST} ${BUILDINFOLIST} ${COMMONLIST})

	if (module_slug STREQUAL "native-dll")
		add_library(${module_target_args})
		set_target_properties(${module_target} PROPERTIES
			PREFIX ""
			COMPILE_DEFINITIONS "BUILD_VM_IN_PROCESS")
	else()
		add_executable(${module_target_args})
	endif()

	set_target_properties(${module_target} PROPERTIES
		COMPILE_DEFINITIONS "VM_NAME=${GAMEMODULE_NAME};${GAMEMODULE_DEFINITIONS};BUILD_VM"
		COMPILE_OPTIONS "${GAMEMODULE_FLAGS}"
		FOLDER ${GAMEMODULE_NAME}
	)

	if (module_slug STREQUAL "nacl")
		set_target_properties(${module_target} PROPERTIES
			OUTPUT_NAME "${GAMEMODULE_NAME}"
			SUFFIX "${PLATFORM_EXE_SUFFIX}")
	endif()

	target_link_libraries(${module_target} ${GAMEMODULE_LIBS} ${LIBS_BASE})

	ADD_PRECOMPILED_HEADER(${module_target})
endfunction()

function(gameSubProject)
	ExternalProject_Add(${VMS_PROJECT}
		SOURCE_DIR ${CMAKE_CURRENT_SOURCE_DIR}
		BINARY_DIR ${CMAKE_CURRENT_BINARY_DIR}/${VMS_PROJECT}
		CMAKE_GENERATOR ${VM_GENERATOR}
		CMAKE_ARGS
			-DFORK=2
			-DDAEMON_DIR=${Daemon_SOURCE_DIR}
			-DDEPS_DIR=${DEPS_DIR}
			-DBUILD_CLIENT=OFF
			-DBUILD_TTY_CLIENT=OFF
			-DBUILD_SERVER=OFF
			${ARGV}
			${INHERITED_OPTION_ARGS}
		INSTALL_COMMAND ""
	)

	# Force the rescan and rebuild of the subproject.
	ExternalProject_Add_Step(${VMS_PROJECT} forcebuild
		COMMAND ${CMAKE_COMMAND} -E remove
			${CMAKE_CURRENT_BINARY_DIR}/${VMS_PROJECT}-prefix/src/${VMS_PROJECT}-stamp/${VMS_PROJECT}-configure
		COMMENT "Forcing build step for '${VMS_PROJECT}'"
		DEPENDEES build
		ALWAYS 1
	)
endfunction()

function(GAMEMODULE)
	# ParseArguments setup
	set(oneValueArgs NAME)
	set(multiValueArgs DEFINITIONS FLAGS FILES LIBS)
	cmake_parse_arguments(GAMEMODULE "" "${oneValueArgs}" "${multiValueArgs}" ${ARGN})

	if (NOT FORK)
		if (BUILD_GAME_NACL)
			set(FORK 1 PARENT_SCOPE)
		endif()

		if (BUILD_GAME_NATIVE_DLL)
			buildGameModule("native-dll")
		endif()

		if (BUILD_GAME_NATIVE_EXE)
			buildGameModule("native-exe")
		endif()
	endif()

	if (FORK EQUAL 1)
		if (CMAKE_GENERATOR MATCHES "Visual Studio")
			set(VM_GENERATOR "NMake Makefiles")
		else()
			set(VM_GENERATOR ${CMAKE_GENERATOR})
		endif()

		include(ExternalProject)
		set(INHERITED_OPTION_ARGS)

		foreach(inherited_option ${NACL_VM_INHERITED_OPTIONS})
			set(INHERITED_OPTION_ARGS ${INHERITED_OPTION_ARGS}
				"-D${inherited_option}=${${inherited_option}}")
		endforeach(inherited_option)

		if (BUILD_GAME_NACL)
			if (USE_NACL_SAIGO)
				add_custom_target(nacl-vms ALL)
				unset(VMS_PROJECTS)

				foreach(NACL_TARGET ${NACL_TARGETS})
					if (NACL_TARGET STREQUAL "i686")
						set(SAIGO_ARCH "i686")
					elseif (NACL_TARGET STREQUAL "amd64")
						set(SAIGO_ARCH "x86_64")
					elseif (NACL_TARGET STREQUAL "armhf")
						set(SAIGO_ARCH "arm")
					else()
						message(FATAL_ERROR "Unknown NaCl architecture ${NACL_TARGET}")
					endif()

					set(VMS_PROJECT nacl-vms-${NACL_TARGET})
					list(APPEND VMS_PROJECTS ${VMS_PROJECT})
					add_dependencies(nacl-vms ${VMS_PROJECT})

					gameSubProject(
						-DCMAKE_TOOLCHAIN_FILE=${Daemon_SOURCE_DIR}/cmake/toolchain-saigo.cmake
						-DBUILD_GAME_NACL=ON
						-DBUILD_GAME_NATIVE_DLL=OFF
						-DBUILD_GAME_NATIVE_EXE=OFF
						-DUSE_NACL_SAIGO=ON
						-DSAIGO_ARCH=${SAIGO_ARCH}
						-DNACL_TARGET=${NACL_TARGET}
					)
				endforeach()
			else()
				set(VMS_PROJECT nacl-vms)
				set(VMS_PROJECTS ${VMS_PROJECT})

				# Workaround a bug where CMake ExternalProject lists-as-args are cut on first “;”
				string(REPLACE ";" "," NACL_TARGETS_STRING "${NACL_TARGETS}")

				gameSubProject(
					-DCMAKE_TOOLCHAIN_FILE=${Daemon_SOURCE_DIR}/cmake/toolchain-pnacl.cmake
					-DBUILD_GAME_NACL=ON
					-DBUILD_GAME_NATIVE_DLL=OFF
					-DBUILD_GAME_NATIVE_EXE=OFF
					-DNACL_TARGETS_STRING=${NACL_TARGETS_STRING}
				)
			endif()
		endif()

		set(VMS_PROJECTS ${VMS_PROJECTS} PARENT_SCOPE)
	elseif (FORK EQUAL 2)
		if (BUILD_GAME_NACL)
			if (USE_NACL_SAIGO)
				set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR})
			else()
				# Put the .nexe and .pexe files in the same directory as the engine.
				set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/..)
			endif()

			buildGameModule("nacl")

			if (USE_NACL_SAIGO)
				# Finalize NaCl executables for supported architectures.
				saigo_finalize(${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/.. ${GAMEMODULE_NAME} ${NACL_TARGET})
			else()
				# Revert a workaround for a bug where CMake ExternalProject lists-as-args are cut on first “;”
				string(REPLACE "," ";" NACL_TARGETS "${NACL_TARGETS_STRING}")

				# Generate NaCl executables for supported architectures.
				foreach(NACL_TARGET ${NACL_TARGETS})
					pnacl_finalize(${CMAKE_RUNTIME_OUTPUT_DIRECTORY} ${GAMEMODULE_NAME} ${NACL_TARGET})
				endforeach()
			endif()
		endif()
	endif()
endfunction()
