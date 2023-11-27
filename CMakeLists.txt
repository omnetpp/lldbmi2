cmake_minimum_required(VERSION 3.14)

# Set project name and version
project(lldbmi2 VERSION 0.1.0 LANGUAGES CXX)

# Define options
option(BUILD_TESTS "Build the test programs" OFF)

# Set C++ standard
set(CMAKE_CXX_STANDARD 14)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_OSX_ARCHITECTURES "arm64;x86_64" CACHE STRING "Build architectures for OSX" FORCE)

# Get version from git log
find_package(Git QUIET)
if(GIT_FOUND)
    execute_process(COMMAND ${GIT_EXECUTABLE} log --oneline
                    COMMAND wc -l
                    COMMAND tr -d " "
                    COMMAND tr -d "\n"
                    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
                    OUTPUT_VARIABLE version_revision)
else()
    set(version_revision "unknown")
endif()

configure_file(${CMAKE_CURRENT_SOURCE_DIR}/templates/version.h.in ${CMAKE_CURRENT_BINARY_DIR}/generated/version.h @ONLY)

# Find LLDB library
if(USE_LIB_PATH)
    find_library(LLDB_LIBRARY NAMES lldb LLDB PATHS ${USE_LIB_PATH})
else()
    if(APPLE)
        find_library(LLDB_LIBRARY NAMES LLDB lldb PATHS "/Applications/Xcode.app/Contents/SharedFrameworks" "/Library/Developer/CommandLineTools/Library/PrivateFrameworks")
    else()
        find_library(LLDB_LIBRARY NAMES lldb-19 lldb-18 lldb-17 lldb-16 lldb-15 lldb-14 lldb-13 lldb-12 lldb-11 lldb-10 lldb PATHS /usr/lib /usr/local/lib /usr/lib64 /usr/local/lib64 REQUIRED)
    endif()
endif()

mark_as_advanced(LLDB_LIBRARY)
get_filename_component(LLDB_LIBRARY_PATH ${LLDB_LIBRARY} DIRECTORY)
set(CMAKE_BUILD_RPATH "/Applications/Xcode.app/Contents/SharedFrameworks;/Library/Developer/CommandLineTools/Library/PrivateFrameworks")

message(STATUS "LLDB_LIBRARY: ${LLDB_LIBRARY} @ ${USE_LIB_PATH} ^ ${LLDB_LIBRARY_PATH}")

# Include directories
include_directories(${CMAKE_CURRENT_SOURCE_DIR}/includes)
include_directories(${CMAKE_CURRENT_BINARY_DIR}/generated)

# Add source files
file(GLOB_RECURSE lldbmi2_sources ${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp)

# Add executable
add_executable(${PROJECT_NAME} ${lldbmi2_sources})
target_compile_options(${PROJECT_NAME} PRIVATE -Wall -pedantic -pthread)
# target_link_libraries(${PROJECT_NAME} PRIVATE ${LLDB_LIBRARY})

# Test programs
if(BUILD_TESTS)
    enable_testing()
    file(GLOB_RECURSE tests_paths ${CMAKE_CURRENT_SOURCE_DIR}/tests/src/*.cpp)
    foreach(test_path IN LISTS tests_paths)
        get_filename_component(test_name ${test_path} NAME_WE)
        add_executable(${test_name} ${test_path})
        add_test(NAME ${test_name} COMMAND ${test_name})
    endforeach()
endif()

# Install rules
install(TARGETS ${PROJECT_NAME} DESTINATION bin)

# Unset cache variables
unset(USE_LIB_PATH CACHE)
unset(LLDB_LIBRARY CACHE)