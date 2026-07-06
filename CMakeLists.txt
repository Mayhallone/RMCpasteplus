# CMakeList.txt : CMake project for Paster-plus-plus, include source and define
# project specific logic here.
#
cmake_minimum_required (VERSION 3.10)

project ("Paster-plus-plus" VERSION 1.0 LANGUAGES C CXX RC)

message(STATUS "Source directory: ${CMAKE_SOURCE_DIR}")
message(STATUS "Build directory: ${CMAKE_BINARY_DIR}")

if (NOT CMAKE_BUILD_TYPE)
	set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

option(PASTER_PASTE_LOG "Enable per-character paste diagnostic log" OFF)
if (PASTER_PASTE_LOG)
	add_compile_definitions(PASTER_PASTE_LOG=1)
endif()

option(PASTER_NO_PROGRESS "Disable progress-bar updates during paste (diagnostic)" OFF)
if (PASTER_NO_PROGRESS)
	add_compile_definitions(PASTER_NO_PROGRESS=1)
endif()

#set(TOOLCHAIN_PREFIX x86_64-w64-mingw32-g++)
if (MINGW)
	message(STATUS " ** MINGW detected.. **")
	set(CMAKE_CXX_FLAGS "-municode -mwindows -static ${CMAKE_CXX_FLAGS}")
endif()

set (CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR})
set (SOURCES src/paster-plus-plus.cpp)
set (HEADERS src/paster-plus-plus.h)
set (RESOURCES src/paster-plus-plus.rc)
# Add source to this project's executable.
add_executable (Paster-plus-plus WIN32 ${SOURCES} ${HEADERS} ${RESOURCES})

target_include_directories (Paster-plus-plus PRIVATE
	${CMAKE_SOURCE_DIR}/src
	${CMAKE_SOURCE_DIR}   # so the .rc can reference pasteicon.ico at repo root
)

# MinGW's windres does not automatically pick up target_include_directories,
# so pass the repo root explicitly via --include-dir for the RC compiler.
if (MINGW)
	set (CMAKE_RC_FLAGS "${CMAKE_RC_FLAGS} --include-dir ${CMAKE_SOURCE_DIR}")
endif()

# winmm is required for timeBeginPeriod/timeEndPeriod (~1ms Sleep resolution).
# shell32 provides Shell_NotifyIcon for the tray-icon integration.
target_link_libraries (Paster-plus-plus PRIVATE winmm shell32)

# TODO: Add tests and install targets if needed.
