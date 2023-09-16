cmake_minimum_required(VERSION 3.5)

project(dmb CXX)

# project setup

if(DEFINED CMAKE_CONFIGURATION_TYPES)
	message(STATUS "Detected multi configuration build: ${CMAKE_CONFIGURATION_TYPES}")

	if(DEFINED CMAKE_BUILD_TYPE)
		message(WARNING "Both CMAKE_CONFIGURATION_TYPES and CMAKE_BUILD_TYPE are set! Please unset CMAKE_BUILD_TYPE to avoid confusion.")
	endif()
elseif(DEFINED CMAKE_BUILD_TYPE)
	if(CMAKE_BUILD_TYPE STREQUAL "")
		message(STATUS "Detected single configuration build")
	else()
		message(STATUS "Detected single configuration build: ${CMAKE_BUILD_TYPE}")
	endif()
else()
	message(FATAL_ERROR "Neither CMAKE_CONFIGURATION_TYPES or CMAKE_BUILD_TYPE are set!")
endif()

if(NOT CMAKE_CXX_STANDARD)
	set(CMAKE_CXX_STANDARD 11)
	message(STATUS "C++ standard ${CMAKE_CXX_STANDARD} (default)")
elseif(CMAKE_CXX_STANDARD LESS 11)
	message(FATAL_ERROR "Require C++ standard >= 11")
else()
	message(STATUS "C++ standard ${CMAKE_CXX_STANDARD}")
endif()
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# project information

message(STATUS "Using C++ compiler ${CMAKE_CXX_COMPILER}")
message(STATUS "System is ${CMAKE_SYSTEM_NAME}_${CMAKE_SYSTEM_PROCESSOR}")

# project options

option(DMB_BUILD_BENCHMARKS "Build benchmarks" OFF)

# subdirectory setup

add_subdirectory(src)
