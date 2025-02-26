set(DAEMON_BUILDINFO_HEADER "// Automatically generated, do not modify!\n")
set(DAEMON_BUILDINFO_PARENT "${CMAKE_CURRENT_BINARY_DIR}/GeneratedSource")
set(DAEMON_BUILDINFO_DIR "DaemonBuildInfo")
set(DAEMON_BUILDINFO_DIR_FULLPATH "${DAEMON_BUILDINFO_PARENT}/${DAEMON_BUILDINFO_DIR}")
set(DAEMON_BUILDINFO_CPP_EXT ".cpp")
set(DAEMON_BUILDINFO_H_EXT ".h")
set(BUILDINFOLIST)

file(MAKE_DIRECTORY "${DAEMON_BUILDINFO_DIR_FULLPATH}")
include_directories("${DAEMON_BUILDINFO_PARENT}")

foreach(kind CPP H)
	set(DAEMON_BUILDINFO_${kind} "${DAEMON_BUILDINFO_HEADER}")
endforeach()

macro(daemon_add_buildinfo TYPE NAME VALUE)
	set(DAEMON_BUILDINFO_CPP "${DAEMON_BUILDINFO_CPP}const ${TYPE} ${NAME}=${VALUE};\n")
	set(DAEMON_BUILDINFO_H "${DAEMON_BUILDINFO_H}extern const ${TYPE} ${NAME};\n")
endmacro()

macro(daemon_write_buildinfo NAME)
	foreach(kind CPP H)
		set(DAEMON_BUILDINFO_${kind}_NAME "${NAME}${DAEMON_BUILDINFO_${kind}_EXT}")
		set(DAEMON_BUILDINFO_${kind}_FILE "${DAEMON_BUILDINFO_DIR}/${DAEMON_BUILDINFO_${kind}_NAME}")
		set(DAEMON_BUILDINFO_${kind}_FILE_FULLPATH "${DAEMON_BUILDINFO_PARENT}/${DAEMON_BUILDINFO_${kind}_FILE}")
		list(APPEND BUILDINFOLIST "${DAEMON_BUILDINFO_${kind}_FILE_FULLPATH}")

		if (EXISTS "${DAEMON_BUILDINFO_${kind}_FILE_FULLPATH}")
			file(READ "${DAEMON_BUILDINFO_${kind}_FILE_FULLPATH}" DAEMON_BUILDINFO_${kind}_READ)
		endif()

		if (NOT "${DAEMON_BUILDINFO_${kind}}" STREQUAL "${DAEMON_BUILDINFO_${kind}_READ}")
			message(STATUS "Generating ${DAEMON_BUILDINFO_${kind}_FILE}")
			file(WRITE "${DAEMON_BUILDINFO_${kind}_FILE_FULLPATH}" "${DAEMON_BUILDINFO_${kind}}")
		endif()
	endforeach()
endmacro()
