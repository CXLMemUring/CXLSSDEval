cmake_minimum_required(VERSION 3.14)
project(CXLSSDEval VERSION 1.0.0 LANGUAGES CXX)

# Set C++ standard
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Set default build type
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
endif()

# Compiler flags
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wall -Wextra -Wpedantic")
set(CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_DEBUG} -g -O0 -DDEBUG")
set(CMAKE_CXX_FLAGS_RELEASE "${CMAKE_CXX_FLAGS_RELEASE} -O3 -march=native")

# Check for required CPU features
include(CheckCXXCompilerFlag)
check_cxx_compiler_flag("-mwaitpkg" COMPILER_SUPPORTS_WAITPKG)
if(COMPILER_SUPPORTS_WAITPKG)
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -mwaitpkg")
endif()

# Find required packages
find_package(Threads REQUIRED)

# Find or fetch spdlog
find_package(spdlog QUIET)
if(NOT spdlog_FOUND)
    include(FetchContent)
    FetchContent_Declare(
        spdlog
        GIT_REPOSITORY https://github.com/gabime/spdlog.git
        GIT_TAG v1.12.0
    )
    FetchContent_MakeAvailable(spdlog)
endif()

# Find or fetch Catch2
find_package(Catch2 3 QUIET)
if(NOT Catch2_FOUND)
    include(FetchContent)
    FetchContent_Declare(
        Catch2
        GIT_REPOSITORY https://github.com/catchorg/Catch2.git
        GIT_TAG v3.4.0
    )
    FetchContent_MakeAvailable(Catch2)
endif()

# Include directories
include_directories(${CMAKE_CURRENT_SOURCE_DIR}/include)

# Source files
set(LIB_SOURCES
    src/cxl_mwait.cpp
    src/cxl_ssd_common.cpp
)

# Create static library
add_library(cxlssd_static STATIC ${LIB_SOURCES})
target_link_libraries(cxlssd_static 
    PUBLIC 
        Threads::Threads
        spdlog::spdlog
)

# Create shared library
add_library(cxlssd SHARED ${LIB_SOURCES})
target_link_libraries(cxlssd 
    PUBLIC 
        Threads::Threads
        spdlog::spdlog
)
set_target_properties(cxlssd PROPERTIES
    VERSION ${PROJECT_VERSION}
    SOVERSION 1
)

# Test executable
add_executable(test_mwait tests/test_mwait.cpp)
target_link_libraries(test_mwait 
    PRIVATE 
        cxlssd_static
        Threads::Threads
)

# Example executables
add_executable(example_pmr_cache tests/example_pmr_cache.cpp)
target_link_libraries(example_pmr_cache 
    PRIVATE 
        cxlssd_static
        Threads::Threads
)

# Test for CXL open methods (devdax and NVMe)
add_executable(test_cxl_open_methods tests/test_cxl_open_methods.cpp)
target_link_libraries(test_cxl_open_methods
    PRIVATE
        cxlssd_static
        Threads::Threads
)

# Add DAX device support and FIO interception
add_library(fio_intercept SHARED src/fio_intercept.cpp)
target_link_libraries(fio_intercept PRIVATE ${CMAKE_DL_LIBS} Threads::Threads)
target_compile_options(fio_intercept PRIVATE -fPIC)
set_target_properties(fio_intercept PROPERTIES
    LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}
)

# Add DAX device test executable
add_executable(test_mwait_dax tests/test_mwait_dax.cpp)
target_link_libraries(test_mwait_dax PRIVATE Threads::Threads)

# Installation
install(TARGETS cxlssd cxlssd_static
    LIBRARY DESTINATION lib
    ARCHIVE DESTINATION lib
)

install(DIRECTORY include/
    DESTINATION include/cxlssd
)

# Package configuration
include(CMakePackageConfigHelpers)
write_basic_package_version_file(
    "${CMAKE_CURRENT_BINARY_DIR}/CXLSSDEvalConfigVersion.cmake"
    VERSION ${PROJECT_VERSION}
    COMPATIBILITY AnyNewerVersion
)

# Enable testing
enable_testing()
add_test(NAME basic_test COMMAND test_mwait --test basic)
add_test(NAME pmr_test COMMAND test_mwait --test pmr_latency)
add_test(NAME benchmark_test COMMAND benchmark --quick)

# Documentation
option(BUILD_DOCS "Build documentation" OFF)
if(BUILD_DOCS)
    find_package(Doxygen)
    if(DOXYGEN_FOUND)
        set(DOXYGEN_IN ${CMAKE_CURRENT_SOURCE_DIR}/Doxyfile.in)
        set(DOXYGEN_OUT ${CMAKE_CURRENT_BINARY_DIR}/Doxyfile)
        
        configure_file(${DOXYGEN_IN} ${DOXYGEN_OUT} @ONLY)
        
        add_custom_target(docs ALL
            COMMAND ${DOXYGEN_EXECUTABLE} ${DOXYGEN_OUT}
            WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
            COMMENT "Generating API documentation with Doxygen"
            VERBATIM
        )
    endif()
endif()

# Print configuration summary
message(STATUS "")
message(STATUS "CXLSSDEval Configuration Summary:")
message(STATUS "  Build Type:        ${CMAKE_BUILD_TYPE}")
message(STATUS "  C++ Standard:      ${CMAKE_CXX_STANDARD}")
message(STATUS "  Install Prefix:    ${CMAKE_INSTALL_PREFIX}")
message(STATUS "  WAITPKG Support:   ${COMPILER_SUPPORTS_WAITPKG}")
message(STATUS "  Build Docs:        ${BUILD_DOCS}")
message(STATUS "")