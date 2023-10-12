include(bnc/CMakeLists.txt)
include(gen/CMakeLists.txt)
include(lib/CMakeLists.txt)

add_executable(dmbgen "")
target_sources(dmbgen PRIVATE ${DMBGEN_SOURCES} ${DMBGEN_HEADERS} ${DMBGEN_SECRETS})
target_compile_options(dmbgen PRIVATE
	$<$<OR:$<CXX_COMPILER_ID:Clang>,$<CXX_COMPILER_ID:GNU>>:
		-Wall -Wextra -Wconversion -Werror -march=native -masm=intel>
	$<$<CXX_COMPILER_ID:MSVC>:
		/W4 /WX /Zc:rvalueCast>
	)
target_compile_definitions(dmbgen PRIVATE
	$<$<CXX_COMPILER_ID:MSVC>:
		_CRT_SECURE_NO_WARNINGS>
	)

file(MAKE_DIRECTORY "${CMAKE_SOURCE_DIR}/gen")
add_custom_command(OUTPUT "${CMAKE_SOURCE_DIR}/gen/overhead.hpp"
	COMMAND dmbgen
	WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}/gen")

add_library(dmblib STATIC)
target_sources(dmblib PRIVATE ${DMBLIB_SOURCES} ${DMBLIB_HEADERS} ${DMBLIB_SECRETS} "${CMAKE_SOURCE_DIR}/gen/overhead.hpp")
target_include_directories(dmblib PUBLIC "lib" "${CMAKE_SOURCE_DIR}/gen")
target_compile_options(dmblib PRIVATE
	$<$<OR:$<CXX_COMPILER_ID:Clang>,$<CXX_COMPILER_ID:GNU>>:
		-Wall -Wextra -Wconversion -Werror -nostdlib -fno-exceptions -fno-rtti -march=native>
		# -nostartfiles
	$<$<CXX_COMPILER_ID:MSVC>:
		/W4 /WX /Zl /GS- /GR- /Zc:rvalueCast>
	)
target_compile_definitions(dmblib PRIVATE
	$<$<CXX_COMPILER_ID:MSVC>:
		NOMINMAX UNICODE _UNICODE>
	)
if(MSVC)
	set_target_properties(dmblib PROPERTIES
		STATIC_LIBRARY_FLAGS "/NODEFAULTLIB"
		)
endif()
install(TARGETS dmblib DESTINATION lib)
install(FILES ${DMBLIB_HEADERS} DESTINATION include/dmb)

if(DMB_BUILD_BENCHMARKS)
	add_executable(dmbbnc "")
	target_sources(dmbbnc PRIVATE ${DMBBNC_SOURCES} ${DMBBNC_HEADERS})
	target_include_directories(dmbbnc PRIVATE "bnc")
	target_link_libraries(dmbbnc PRIVATE
		$<$<CXX_COMPILER_ID:MSVC>:msvc_ignored_options>
		dmblib
		)
	target_compile_options(dmbbnc PRIVATE
		$<$<OR:$<CXX_COMPILER_ID:Clang>,$<CXX_COMPILER_ID:GNU>>:
			-Wall -Wextra -Wconversion -Werror -march=native -masm=intel>
		$<$<CXX_COMPILER_ID:MSVC>:
			/W4 /WX /Zc:rvalueCast>
		)
	target_compile_definitions(dmbbnc PRIVATE
		$<$<CXX_COMPILER_ID:MSVC>:
			_CRT_SECURE_NO_WARNINGS>
		)

	if(${CMAKE_MAJOR_VERSION}.${CMAKE_MINOR_VERSION} VERSION_GREATER 3.7)
		set_target_properties(dmbbnc PROPERTIES VS_DEBUGGER_WORKING_DIRECTORY "${CMAKE_HOME_DIRECTORY}")
	endif()

	set_property(DIRECTORY PROPERTY VS_STARTUP_PROJECT dmbbnc)
endif()
